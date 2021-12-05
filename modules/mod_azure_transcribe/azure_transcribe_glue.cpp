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
#include <memory>

#include <speechapi_cxx.h>

#include "mod_azure_transcribe.h"

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;


const char ALLOC_TAG[] = "drachtio";

static bool hasDefaultCredentials = false;

class GStreamer {
public:
	GStreamer(
    const char *sessionId,
		u_int16_t channels,
    char *lang, 
    int interim,
		const char* region, 
		const char* subscriptionKey, 
		responseHandler_t responseHandler
  ) : m_sessionId(sessionId), m_finished(false), m_stopped(false), m_interim(interim), 
	m_responseHandler(responseHandler) {

		switch_core_session_t* psession = switch_core_session_locate(sessionId);
		if (!psession) throw std::invalid_argument( "session id no longer active" );
		switch_channel_t *channel = switch_core_session_get_channel(psession);
 
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::GStreamer(%p) region %s, language %s\n", 
			this, region, lang);

		auto sourceLanguageConfig = SourceLanguageConfig::FromLanguage(lang);
		auto format = AudioStreamFormat::GetWaveFormatPCM(8000, 16, 1);
		auto options = AudioProcessingOptions::Create(AUDIO_INPUT_PROCESSING_ENABLE_DEFAULT);
		auto speechConfig = SpeechConfig::FromSubscription(subscriptionKey, region);
		if (switch_true(switch_channel_get_variable(channel, "AZURE_USE_OUTPUT_FORMAT_DETAILED"))) {
			speechConfig->SetOutputFormat(OutputFormat::Detailed);
		}
		m_pushStream = AudioInputStream::CreatePushStream(format);
		auto audioConfig = AudioConfig::FromStreamInput(m_pushStream);
		m_recognizer = SpeechRecognizer::FromConfig(speechConfig, sourceLanguageConfig, audioConfig);

		// set properties 
		auto &properties = m_recognizer->Properties;

		// profanity options: Allowed values are "masked", "removed", and "raw".
		const char* profanity = switch_channel_get_variable(channel, "AZURE_PROFANITY_OPTION");
		if (profanity) {
			properties.SetProperty(PropertyId::SpeechServiceResponse_ProfanityOption, profanity);
		}
		// report signal-to-noise ratio
		if (switch_true(switch_channel_get_variable(channel, "AZURE_REQUEST_SNR"))) {
			properties.SetProperty(PropertyId::SpeechServiceResponse_RequestSnr, TrueString);
		}
		// initial speech timeout in milliseconds
		const char* timeout = switch_channel_get_variable(channel, "AZURE_INITIAL_SPEECH_TIMEOUT_MS");
		if (timeout) properties.SetProperty(PropertyId::SpeechServiceConnection_InitialSilenceTimeoutMs, timeout);

		// hints
		const char* hints = switch_channel_get_variable(channel, "AZURE_SPEECH_HINTS");
		if (hints) {
			auto grammar = PhraseListGrammar::FromRecognizer(m_recognizer);
			char *phrases[500] = { 0 };
      int argc = switch_separate_string((char *)hints, ',', phrases, 500);
      for (int i = 0; i < argc; i++) {
        grammar->AddPhrase(phrases[i]);
      }
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "added %d hints\n", argc);
		}

		auto onSessionStarted = [this](const SessionEventArgs& args) {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				auto sessionId = args.SessionId;
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer got session started from microsoft\n");
				switch_core_session_rwunlock(psession);
			}
		};
		auto onSessionStopped = [this](const SessionEventArgs& args) {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			m_stopped = true;
			if (psession) {
				auto sessionId = args.SessionId;
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer: got session stopped from microsoft\n");
				switch_core_session_rwunlock(psession);
			}
		};
		auto onSpeechStartDetected = [this, responseHandler](const RecognitionEventArgs& args) {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				auto sessionId = args.SessionId;
				responseHandler(psession, TRANSCRIBE_EVENT_START_OF_UTTERANCE, NULL);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer start of speech\n");
				switch_core_session_rwunlock(psession);
			}
		};
		auto onSpeechEndDetected = [this, responseHandler](const RecognitionEventArgs& args) {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				auto sessionId = args.SessionId;
				responseHandler(psession, TRANSCRIBE_EVENT_END_OF_UTTERANCE, NULL);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer end of speech\n");
				switch_core_session_rwunlock(psession);
			}
		};
		auto onRecognitionEvent = [this, responseHandler](const SpeechRecognitionEventArgs& args) {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				auto result = args.Result;
				auto reason = result->Reason;
				const auto& properties = result->Properties;
				auto json = properties.GetProperty(PropertyId::SpeechServiceResponse_JsonResult);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer onRecognitionEvent reason %d results: %s,\n", reason, json.c_str());

				switch (reason) {
					case ResultReason::RecognizingSpeech:
					case ResultReason::RecognizedSpeech:
						// note: interim results don't have "RecognitionStatus": "Success"
						responseHandler(psession, TRANSCRIBE_EVENT_RESULTS, json.c_str());
					break;
					case ResultReason::NoMatch:
						responseHandler(psession, TRANSCRIBE_EVENT_NO_SPEECH_DETECTED, json.c_str());
					break;

					default:
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer unexpected result '%s': reason %d\n", 
							json.c_str(), reason);
					break;
				}
				switch_core_session_rwunlock(psession);
			}
		};

		auto onCanceled = [this](const SpeechRecognitionCanceledEventArgs& args) {
			auto result = args.Result;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer recognition canceled\n");
		};

		m_recognizer->SessionStarted += onSessionStarted;
		m_recognizer->SessionStopped += onSessionStopped;
		m_recognizer->SpeechStartDetected += onSpeechStartDetected;
		m_recognizer->SpeechEndDetected += onSpeechEndDetected;
		if (interim) m_recognizer->Recognizing += onRecognitionEvent;
		m_recognizer->Recognized += onRecognitionEvent;
		m_recognizer->Canceled += onCanceled;

		m_recognizer->StartContinuousRecognitionAsync();

		switch_core_session_rwunlock(psession);
	}

	~GStreamer() {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::~GStreamer %p\n", this);		
	}

	bool write(void* data, uint32_t datalen) {
		if (m_finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::write not writing because we are finished, %p\n", this);
			return false;
		}
    m_pushStream->Write(static_cast<uint8_t*>(data), datalen);
		return true;
	}

	void finish() {
		if (m_finished) return;
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::finish - calling  StopContinuousRecognitionAsync%p\n", this);
		m_finished = true;
		//std::future<void> done = m_recognizer->StopContinuousRecognitionAsync();
		m_recognizer->StopContinuousRecognitionAsync().get();
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer::finish - recognition has completed%p\n", this);
	}

	bool isStopped() {
		return m_stopped;
	}

private:
	std::string m_sessionId;
	std::string  m_region;
	std::shared_ptr<SpeechRecognizer> m_recognizer;
	std::shared_ptr<PushAudioInputStream> m_pushStream;

	responseHandler_t m_responseHandler;
	bool m_interim;
	bool m_finished;
	bool m_stopped;
};

static void killcb(struct cap_cb* cb) {
	if (cb) {
		if (cb->streamer) {
			GStreamer* p = (GStreamer *) cb->streamer;
			delete p;
			cb->streamer = NULL;
		}
		if (cb->resampler) {
				speex_resampler_destroy(cb->resampler);
				cb->resampler = NULL;
		}
	}
}

extern "C" {
	switch_status_t azure_transcribe_init() {
		const char* subscriptionKey = std::getenv("AZURE_SUBSCRIPTION_KEY");
		const char* region = std::getenv("AZURE_REGION");
		if (NULL == subscriptionKey) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
				"\"AZURE_SUBSCRIPTION_KEY\"  env var not set; authentication will expect channel variables of same names to be set\n");
		}
		else {
			hasDefaultCredentials = true;

		}

		return SWITCH_STATUS_SUCCESS;
	}
	
	switch_status_t azure_transcribe_cleanup() {
		return SWITCH_STATUS_SUCCESS;
	}

	// start transcribe on a channel
	switch_status_t azure_transcribe_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
          uint32_t samples_per_second, uint32_t channels, char* lang, int interim, void **ppUserData
	) {
		switch_status_t status = SWITCH_STATUS_SUCCESS;
		switch_channel_t *channel = switch_core_session_get_channel(session);
		int err;
		switch_threadattr_t *thd_attr = NULL;
		switch_memory_pool_t *pool = switch_core_session_get_pool(session);
		auto read_codec = switch_core_session_get_read_codec(session);
		uint32_t sampleRate = read_codec->implementation->actual_samples_per_second;
		const char* sessionId = switch_core_session_get_uuid(session);
		struct cap_cb* cb = (struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
		memset(cb, sizeof(cb), 0);
		const char* subscriptionKey = switch_channel_get_variable(channel, "AZURE_SUBSCRIPTION_KEY");
		const char* region = switch_channel_get_variable(channel, "AZURE_REGION");
		cb->channels = channels;
		strncpy(cb->sessionId, sessionId, 256);

		if (subscriptionKey && region) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Using channel vars for azure authentication\n");
			strncpy(cb->subscriptionKey, subscriptionKey, MAX_SUBSCRIPTION_KEY_LEN);
			strncpy(cb->region, region, MAX_REGION);
		}
		else if (std::getenv("AZURE_SUBSCRIPTION_KEY") && std::getenv("AZURE_REGION")) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Using env vars for azure authentication\n");
			strncpy(cb->subscriptionKey, std::getenv("AZURE_SUBSCRIPTION_KEY"), MAX_SUBSCRIPTION_KEY_LEN);
			strncpy(cb->region, std::getenv("AZURE_REGION"), MAX_REGION);
		}
		else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "No channel vars or env vars for azure authentication..will use default profile if found\n");
		}

		GStreamer *streamer = NULL;
		try {
			streamer = new GStreamer(sessionId, channels, lang, interim, region, subscriptionKey, responseHandler);
			cb->streamer = streamer;
		} catch (std::exception& e) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
				switch_channel_get_name(channel), e.what());
			return SWITCH_STATUS_FALSE;
		}

		cb->responseHandler = responseHandler;

		if (switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing mutex\n");
			status = SWITCH_STATUS_FALSE;
			goto done; 
		}

		cb->interim = interim;
		strncpy(cb->lang, lang, MAX_LANG);

		/* determine if we need to resample the audio to 16-bit 8khz */
		if (sampleRate != 8000) {
			cb->resampler = speex_resampler_init(1, sampleRate, 8000, SWITCH_RESAMPLE_QUALITY, &err);
			if (0 != err) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n", 
							switch_channel_get_name(channel), speex_resampler_strerror(err));
				status = SWITCH_STATUS_FALSE;
				goto done;
			}
		}

		*ppUserData = cb;
	
	done:
		return status;
	}

	switch_status_t azure_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_mutex_lock(cb->mutex);

			switch_channel_set_private(channel, MY_BUG_NAME, NULL);
			if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "azure_transcribe_session_stop: calling finish..\n");

				// launch thread to stop recognition as MS will take a bit to eval final transcript
				std::thread t([streamer, cb]{
					streamer->finish();
					killcb(cb);
				});
				t.detach();
			}
			else killcb(cb);

			switch_mutex_unlock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "azure_transcribe_session_stop: done calling finish\n");

			return SWITCH_STATUS_SUCCESS;
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
		return SWITCH_STATUS_FALSE;
	}
	
	switch_bool_t azure_transcribe_frame(switch_media_bug_t *bug, void* user_data) {
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
						if (cb->resampler) {
							spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
							spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
							spx_uint32_t in_len = frame.samples;
							size_t written;
						
							speex_resampler_process_interleaved_int(
								cb->resampler,
								(const spx_int16_t *) frame.data,
								(spx_uint32_t *) &in_len, 
								&out[0],
								&out_len);
							streamer->write( &out[0], sizeof(spx_int16_t) * out_len);
						}
						else {
							streamer->write( frame.data, frame.datalen);
						}
					}
				}
			}
			switch_mutex_unlock(cb->mutex);
		}
		return SWITCH_TRUE;
	}
}
