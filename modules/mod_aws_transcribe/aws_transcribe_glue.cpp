#include <cstdlib>

#include <switch.h>
#include <switch_json.h>

#include <string.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <string>
#include <sstream>
#include <deque>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/logging/DefaultLogSystem.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/transcribestreaming/TranscribeStreamingServiceClient.h>
#include <aws/transcribestreaming/model/StartStreamTranscriptionHandler.h>
#include <aws/transcribestreaming/model/StartStreamTranscriptionRequest.h>

#include "mod_aws_transcribe.h"
#include "simple_buffer.h"

#define BUFFER_SECS (3)
#define CHUNKSIZE (320)

using namespace Aws;
using namespace Aws::Utils;
using namespace Aws::Auth;
using namespace Aws::TranscribeStreamingService;
using namespace Aws::TranscribeStreamingService::Model;


const char ALLOC_TAG[] = "drachtio";

static bool hasDefaultCredentials = false;

class GStreamer {
public:
	GStreamer(
    const char *sessionId,
		u_int16_t channels,
    char *lang, 
    int interim,
		uint32_t samples_per_second,
		const char* region, 
		const char* awsAccessKeyId, 
		const char* awsSecretAccessKey,
		responseHandler_t responseHandler
  ) : m_sessionId(sessionId), m_finished(false), m_interim(interim), m_finishing(false), m_connected(false), m_connecting(false),
	 		m_packets(0), m_responseHandler(responseHandler), m_pStream(nullptr), 
			m_audioBuffer(320 * (samples_per_second == 8000 ? 1 : 2), 15) {
		Aws::String key(awsAccessKeyId);
		Aws::String secret(awsSecretAccessKey);
		Aws::Client::ClientConfiguration config;
		if (region != nullptr && strlen(region) > 0) config.region = region;
		char keySnippet[20];

		strncpy(keySnippet, awsAccessKeyId, 4);
		for (int i = 4; i < 20; i++) keySnippet[i] = 'x';
		keySnippet[19] = '\0';

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p ACCESS_KEY_ID %s, region %s\n", this, keySnippet, region);		
		if (*awsAccessKeyId && *awsSecretAccessKey) {
			m_client = Aws::MakeUnique<TranscribeStreamingServiceClient>(ALLOC_TAG, AWSCredentials(awsAccessKeyId, awsSecretAccessKey), config);
		}
		else {
			m_client = Aws::MakeUnique<TranscribeStreamingServiceClient>(ALLOC_TAG, config);
		}
	
    m_handler.SetTranscriptEventCallback([this](const TranscriptEvent& ev)
    {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				switch_channel_t* channel = switch_core_session_get_channel(psession);
				std::lock_guard<std::mutex> lk(m_mutex);
				m_transcript = ev;
				m_cond.notify_one();

				switch_core_session_rwunlock(psession);
			}
    });

		// not worth resampling to 16k if we get 8k ulaw or alaw in..
    m_request.SetMediaSampleRateHertz(samples_per_second > 8000 ? 16000 : 8000);
    m_request.SetLanguageCode(LanguageCodeMapper::GetLanguageCodeForName(lang));
    m_request.SetMediaEncoding(MediaEncoding::pcm);
    m_request.SetEventStreamHandler(m_handler);
		if (channels > 1) m_request.SetNumberOfChannels(channels);

		const char* var;
		switch_core_session_t* session = switch_core_session_locate(sessionId);
    switch_channel_t *channel = switch_core_session_get_channel(session);

		if (var = switch_channel_get_variable(channel, "AWS_SHOW_SPEAKER_LABEL")) {
			m_request.SetShowSpeakerLabel(true);
		}
		if (var = switch_channel_get_variable(channel, "AWS_ENABLE_CHANNEL_IDENTIFICATION")) {
			m_request.SetEnableChannelIdentification(true);
		}
		if (var = switch_channel_get_variable(channel, "AWS_VOCABULARY_NAME")) {
			m_request.SetVocabularyName(var);
		}
		if (var = switch_channel_get_variable(channel, "AWS_VOCABULARY_FILTER_NAME")) {
			m_request.SetVocabularyFilterName(var);
		}
		if (var = switch_channel_get_variable(channel, "AWS_VOCABULARY_FILTER_METHOD")) {
			m_request.SetVocabularyFilterMethod(VocabularyFilterMethodMapper::GetVocabularyFilterMethodForName(var));
		}
	}

	void connect() {
		if (m_connecting) return;
		m_connecting = true;

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer:connect %p connecting to aws speech..\n", this);

    auto OnStreamReady = [this](Model::AudioStream& stream)
    {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				switch_channel_t* channel = switch_core_session_get_channel(psession);

				m_pStream = &stream;
				m_connected = true;


				// send any buffered audio
				int nFrames = m_audioBuffer.getNumItems();
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p got stream ready, %d buffered frames\n", this, nFrames);	
				if (nFrames) {
					char *p;
					do {
						p = m_audioBuffer.getNextChunk();
						if (p) {
							write(p, CHUNKSIZE);
						}
					} while (p);
				}
	
				switch_core_session_rwunlock(psession);
			}
    };
    auto OnResponseCallback = [this](const TranscribeStreamingServiceClient* pClient, 
			const Model::StartStreamTranscriptionRequest& request, 
			const Model::StartStreamTranscriptionOutcome& outcome, 
			const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context)
    {
 			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p stream got final response\n", this);
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				if (!outcome.IsSuccess()) {				
					const TranscribeStreamingServiceError& err = outcome.GetError();
					auto message = err.GetMessage();
					auto exception = err.GetExceptionName();
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p stream got error response %s : %s\n", this, message.c_str(), exception.c_str());
				}

				std::lock_guard<std::mutex> lk(m_mutex);
				m_finished = true;
				m_cond.notify_one();

				switch_core_session_rwunlock(psession);
			}
    };

		m_client->StartStreamTranscriptionAsync(m_request, OnStreamReady, OnResponseCallback, nullptr);
  }


	~GStreamer() {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::~GStreamer wrote %u packets %p\n", m_packets, this);		
	}

	bool write(void* data, uint32_t datalen) {
		if (m_finishing || m_finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::write not writing because we are finished, %p\n", this);
			return false;
		}
    if (!m_connected) {
      if (datalen % CHUNKSIZE == 0) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::write queuing %d bytes\n", datalen);
        m_audioBuffer.add(data, datalen);
      }
      return true;
    }

		std::lock_guard<std::mutex> lk(m_mutex);

		const auto beg = static_cast<const unsigned char*>(data);
		const auto end = beg + datalen;
		Aws::Vector<unsigned char> bits { beg, end };
		m_deqAudio.push_back(bits);
		m_packets++;

		m_cond.notify_one();

		return true;
	}

	void finish() {
		if (m_finishing) return;
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::finish %p\n", this);
		std::lock_guard<std::mutex> lk(m_mutex);

		m_finishing = true;
		m_cond.notify_one();
	}

	void processData() {
		bool shutdownInitiated = false;
		while (true) {
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cond.wait(lk, [&, this] { 
				return (!m_deqAudio.empty() && !m_finishing)  || m_transcript.TranscriptHasBeenSet() || m_finished  || (m_finishing && !shutdownInitiated);
			});


			// we have data to process or have been told we're done
			if (m_finished || !m_connected) return;

			if (m_transcript.TranscriptHasBeenSet()) {
				switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
				if (psession) {

					//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::got a transcript to send out %p\n", this);
					bool isFinal = false;
					std::ostringstream s;
					s << "[";
					for (auto&& r : m_transcript.GetTranscript().GetResults()) {
						int count = 0;
						std::ostringstream t1;
						if (!isFinal && !r.GetIsPartial()) isFinal = true;
						t1 << "{\"is_final\": " << (r.GetIsPartial() ? "false" : "true") << ", \"alternatives\": [";
						for (auto&& alt : r.GetAlternatives()) {
							std::ostringstream t2;
							if (count++ == 0) t2 << "{\"transcript\": \"" << alt.GetTranscript() << "\"}";
							else t2 << ", {\"transcript\": \"" << alt.GetTranscript() << "\"}";
							t1 << t2.str();
						}
						t1 << "]}";
						s << t1.str();
					}
					s << "]";
					if (0 != s.str().compare("[]") && (isFinal || m_interim)) {
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::writing transcript %p: %s\n", this, s.str().c_str() );
						m_responseHandler(psession, s.str().c_str());
					}
					TranscriptEvent empty;
					m_transcript = empty; 

					switch_core_session_rwunlock(psession);
				}
			}
			if (m_finishing) {
				shutdownInitiated = true;
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::writing disconnect event %p\n", this);

				if (m_pStream) {
					m_pStream->flush();
					m_pStream->Close();
					m_pStream = nullptr;
				}
			}
			else {
				// send out any queued speech packets
				while (!m_deqAudio.empty()) {
					Aws::Vector<unsigned char>& bits = m_deqAudio.front();
					Aws::TranscribeStreamingService::Model::AudioEvent event(std::move(bits));
					m_pStream->WriteAudioEvent(event);
					m_deqAudio.pop_front();
				}
			}
		}
	}

	bool isConnecting() {
    return m_connecting;
  }

private:
	std::string m_sessionId;
	std::string  m_region;
	Aws::UniquePtr<TranscribeStreamingServiceClient> m_client;
	AudioStream* m_pStream;
	StartStreamTranscriptionRequest m_request;
	StartStreamTranscriptionHandler m_handler;
	TranscriptEvent m_transcript;
	responseHandler_t m_responseHandler;
	bool m_finishing;
	bool m_interim;
	bool m_finished;
	bool m_connected;
	bool m_connecting;
	uint32_t m_packets;
	std::mutex m_mutex;
	std::condition_variable m_cond;
	std::deque< Aws::Vector<unsigned char> > m_deqAudio;
	SimpleBuffer m_audioBuffer;
};

static void *SWITCH_THREAD_FUNC aws_transcribe_thread(switch_thread_t *thread, void *obj) {
	struct cap_cb *cb = (struct cap_cb *) obj;
	bool ok = true;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "transcribe_thread: starting cb %p\n", (void *) cb);
	GStreamer* pStreamer = new GStreamer(cb->sessionId, cb->channels, cb->lang, cb->interim, cb->samples_per_second, cb->region, cb->awsAccessKeyId, cb->awsSecretAccessKey, 
		cb->responseHandler);
	if (!pStreamer) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "transcribe_thread: Error allocating streamer\n");
		return nullptr;
	}
  if (!cb->vad) pStreamer->connect();
	cb->streamer = pStreamer;
	pStreamer->processData(); //blocks until done

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "transcribe_thread: stopping cb %p\n", (void *) cb);
	delete pStreamer;
	cb->streamer = nullptr;
	return nullptr;
}

static void killcb(struct cap_cb* cb) {
	if (cb) {
		if (cb->streamer) {
			GStreamer* p = (GStreamer *) cb->streamer;
			delete p;
			cb->streamer = nullptr;
		}
		if (cb->resampler) {
				speex_resampler_destroy(cb->resampler);
				cb->resampler = nullptr;
		}
		if (cb->vad) {
			switch_vad_destroy(&cb->vad);
			cb->vad = nullptr;
		}

	}
}

extern "C" {
	switch_status_t aws_transcribe_init() {
		const char* accessKeyId = std::getenv("AWS_ACCESS_KEY_ID");
		const char* secretAccessKey = std::getenv("AWS_SECRET_ACCESS_KEY");
		const char* region = std::getenv("AWS_REGION");
		if (NULL == accessKeyId && NULL == secretAccessKey) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
				"\"AWS_ACCESS_KEY_ID\"  and/or \"AWS_SECRET_ACCESS_KEY\" env var not set; authentication will expect channel variables of same names to be set\n");
		}
		else {
			hasDefaultCredentials = true;

		}
    Aws::SDKOptions options;
/*		
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;

		Aws::Utils::Logging::InitializeAWSLogging(
        Aws::MakeShared<Aws::Utils::Logging::DefaultLogSystem>(
           ALLOC_TAG, Aws::Utils::Logging::LogLevel::Trace, "aws_sdk_transcribe"));
*/
    Aws::InitAPI(options);

		return SWITCH_STATUS_SUCCESS;
	}
	
	switch_status_t aws_transcribe_cleanup() {
		Aws::SDKOptions options;
		/*
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;
		Aws::Utils::Logging::ShutdownAWSLogging();
		*/
    Aws::ShutdownAPI(options);

		return SWITCH_STATUS_SUCCESS;
	}

	// start transcribe on a channel
	switch_status_t aws_transcribe_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
          uint32_t samples_per_second, uint32_t channels, char* lang, int interim, void **ppUserData
	) {
		switch_status_t status = SWITCH_STATUS_SUCCESS;
		switch_channel_t *channel = switch_core_session_get_channel(session);
		int err;
		switch_threadattr_t *thd_attr = NULL;
		switch_memory_pool_t *pool = switch_core_session_get_pool(session);
		auto read_codec = switch_core_session_get_read_codec(session);
		uint32_t sampleRate = read_codec->implementation->actual_samples_per_second;

		struct cap_cb* cb = (struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
		memset(cb, sizeof(cb), 0);
		const char* awsAccessKeyId = switch_channel_get_variable(channel, "AWS_ACCESS_KEY_ID");
		const char* awsSecretAccessKey = switch_channel_get_variable(channel, "AWS_SECRET_ACCESS_KEY");
		const char* awsRegion = switch_channel_get_variable(channel, "AWS_REGION");
		cb->channels = channels;
		LanguageCode code = LanguageCodeMapper::GetLanguageCodeForName(lang);
		if(LanguageCode::NOT_SET == code) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "invalid language code %s\n", lang);
			status = SWITCH_STATUS_FALSE;
			goto done;
		}
		strncpy(cb->sessionId, switch_core_session_get_uuid(session), 256);

		if (awsAccessKeyId && awsSecretAccessKey && awsRegion) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Using channel vars for aws authentication\n");
			strncpy(cb->awsAccessKeyId, awsAccessKeyId, 128);
			strncpy(cb->awsSecretAccessKey, awsSecretAccessKey, 128);
			strncpy(cb->region, awsRegion, MAX_REGION);

		}
		else if (std::getenv("AWS_ACCESS_KEY_ID") &&
			std::getenv("AWS_SECRET_ACCESS_KEY") &&
			std::getenv("AWS_REGION")) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Using env vars for aws authentication\n");
			strncpy(cb->awsAccessKeyId, std::getenv("AWS_ACCESS_KEY_ID"), 128);
			strncpy(cb->awsSecretAccessKey, std::getenv("AWS_SECRET_ACCESS_KEY"), 128);		
			strncpy(cb->region, std::getenv("AWS_REGION"), MAX_REGION);
		}
		else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "No channel vars or env vars for aws authentication..will use default profile if found\n");
		}

		cb->responseHandler = responseHandler;

		if (switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing mutex\n");
			status = SWITCH_STATUS_FALSE;
			goto done; 
		}

		cb->interim = interim;
		strncpy(cb->lang, lang, MAX_LANG);
		cb->samples_per_second = sampleRate;
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "sample rate of rtp stream is %d\n", samples_per_second);
		if (sampleRate != 8000) {
			cb->resampler = speex_resampler_init(1, sampleRate, 16000, SWITCH_RESAMPLE_QUALITY, &err);
			if (0 != err) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n", 
							switch_channel_get_name(channel), speex_resampler_strerror(err));
				status = SWITCH_STATUS_FALSE;
				goto done;
			}
		}

		// allocate vad if we are delaying connecting to the recognizer until we detect speech
		if (switch_channel_var_true(channel, "START_RECOGNIZING_ON_VAD")) {
			cb->vad = switch_vad_init(sampleRate, 1);
			if (cb->vad) {
				const char* var;
				int mode = 2;
				int silence_ms = 150;
				int voice_ms = 250;
				int debug = 0;

				if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_MODE")) {
					mode = atoi(var);
				}
				if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_SILENCE_MS")) {
					silence_ms = atoi(var);
				}
				if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_VOICE_MS")) {
					voice_ms = atoi(var);
				}
				if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_DEBUG")) {
					debug = atoi(var);
				}
				switch_vad_set_mode(cb->vad, mode);
				switch_vad_set_param(cb->vad, "silence_ms", silence_ms);
				switch_vad_set_param(cb->vad, "voice_ms", voice_ms);
				switch_vad_set_param(cb->vad, "debug", debug);
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s: delaying connection until vad, voice_ms %d, mode %d\n", 
					switch_channel_get_name(channel), voice_ms, mode);
			}
		}

		// create a thread to service the http/2 connection to aws
		switch_threadattr_create(&thd_attr, pool);
		switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
		switch_thread_create(&cb->thread, thd_attr, aws_transcribe_thread, cb, pool);

		*ppUserData = cb;
	
	done:
		return status;
	}

	switch_status_t aws_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_mutex_lock(cb->mutex);
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "aws_transcribe_session_stop: finish..\n");
				streamer->finish();
			}
			if (cb->thread) {
				switch_status_t retval;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_transcribe_session_stop: waiting for read thread to complete\n");
				switch_thread_join(&retval, cb->thread);
				cb->thread = NULL;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_transcribe_session_stop: read thread completed\n");
			}
			killcb(cb);

			switch_channel_set_private(channel, MY_BUG_NAME, NULL);
			if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

			switch_mutex_unlock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_transcribe_session_stop: Closed aws session\n");

			return SWITCH_STATUS_SUCCESS;
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
		return SWITCH_STATUS_FALSE;
	}
	
	switch_bool_t aws_transcribe_frame(switch_media_bug_t *bug, void* user_data) {
		switch_core_session_t *session = switch_core_media_bug_get_session(bug);
		uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
		switch_frame_t frame = {};
		struct cap_cb *cb = (struct cap_cb *) user_data;

		frame.data = data;
		frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

		if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
					if (frame.datalen) {
						spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
						spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
						spx_uint32_t in_len = frame.samples;
						size_t written;

						if (cb->vad && !streamer->isConnecting()) {
							switch_vad_state_t state = switch_vad_process(cb->vad, (int16_t*) frame.data, frame.samples);
							if (state == SWITCH_VAD_STATE_START_TALKING) {
								switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "detected speech, connect to aws speech now\n");
								streamer->connect();
							}
						}

						if (cb->resampler) {
							speex_resampler_process_interleaved_int(cb->resampler, (const spx_int16_t *) frame.data, (spx_uint32_t *) &in_len, &out[0], &out_len);						
							streamer->write( &out[0], sizeof(spx_int16_t) * out_len);
						}
						else {
							streamer->write( frame.data, sizeof(spx_int16_t) * frame.samples);
						}
					}
				}
			}
			else {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
					"aws_transcribe_frame: not sending audio because aws channel has been closed\n");
			}
			switch_mutex_unlock(cb->mutex);
		}
		return SWITCH_TRUE;
	}
}
