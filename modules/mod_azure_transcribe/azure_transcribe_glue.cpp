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
#include "simple_buffer.h"

#define CHUNKSIZE (320)
#define DEFAULT_SPEECH_TIMEOUT "180000"

using namespace Microsoft::CognitiveServices::Speech;
using namespace Microsoft::CognitiveServices::Speech::Audio;

const char ALLOC_TAG[] = "drachtio";

static bool hasDefaultCredentials = false;
static bool sdkInitialized = false;
static const char* sdkLog = std::getenv("AZURE_SDK_LOGFILE");
static const char* proxyIP = std::getenv("JAMBONES_HTTP_PROXY_IP");
static const char* proxyPort = std::getenv("JAMBONES_HTTP_PROXY_PORT");
static const char* proxyUsername = std::getenv("JAMBONES_HTTP_PROXY_USERNAME");
static const char* proxyPassword = std::getenv("JAMBONES_HTTP_PROXY_PASSWORD");

class GStreamer {
public:
	GStreamer(
    const char *sessionId,
		const char *bugname,
		u_int16_t channels,
    char *lang, 
    int interim,
		uint32_t samples_per_second,
		const char* region, 
		const char* subscriptionKey, 
		responseHandler_t responseHandler
  ) : m_sessionId(sessionId), m_bugname(bugname), m_finished(false), m_stopped(false), m_interim(interim), 
	 m_connected(false), m_connecting(false), m_audioBuffer(320 * (samples_per_second == 8000 ? 1 : 2), 15),
	m_responseHandler(responseHandler) {

		switch_core_session_t* psession = switch_core_session_locate(sessionId);
		if (!psession) throw std::invalid_argument( "session id no longer active" );
		switch_channel_t *channel = switch_core_session_get_channel(psession);
 
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::GStreamer(%p) region %s, language %s\n", 
			this, region, lang);


		const char* endpoint = switch_channel_get_variable(channel, "AZURE_SERVICE_ENDPOINT");
		const char* endpointId = switch_channel_get_variable(channel, "AZURE_SERVICE_ENDPOINT_ID");

		auto sourceLanguageConfig = SourceLanguageConfig::FromLanguage(lang);
		auto format = AudioStreamFormat::GetWaveFormatPCM(8000, 16, channels);
		auto options = AudioProcessingOptions::Create(AUDIO_INPUT_PROCESSING_ENABLE_DEFAULT);
		auto speechConfig = nullptr != endpoint ? 
			(nullptr != subscriptionKey ?
				SpeechConfig::FromEndpoint(endpoint, subscriptionKey) :
				SpeechConfig::FromEndpoint(endpoint)) :
			SpeechConfig::FromSubscription(subscriptionKey, region);
		if (switch_true(switch_channel_get_variable(channel, "AZURE_USE_OUTPUT_FORMAT_DETAILED"))) {
			speechConfig->SetOutputFormat(OutputFormat::Detailed);
		}
		if (nullptr != endpointId) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "setting endpoint id: %s\n", endpointId);
			speechConfig->SetEndpointId(endpointId);
		}
		if (!sdkInitialized && sdkLog) {
			sdkInitialized = true;
			speechConfig->SetProperty(PropertyId::Speech_LogFilename, sdkLog);
		}
		if (switch_true(switch_channel_get_variable(channel, "AZURE_AUDIO_LOGGING"))) {
			speechConfig->EnableAudioLogging();
		}

    if (nullptr != proxyIP && nullptr != proxyPort) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "setting proxy: %s:%s\n", proxyIP, proxyPort);
      speechConfig->SetProxy(proxyIP, atoi(proxyPort), proxyUsername, proxyPassword);
    }

		m_pushStream = AudioInputStream::CreatePushStream(format);
		auto audioConfig = AudioConfig::FromStreamInput(m_pushStream);

    // alternative language
		const char* var;
    if (var = switch_channel_get_variable(channel, "AZURE_SPEECH_ALTERNATIVE_LANGUAGE_CODES")) {
			std::vector<std::string> languages;
			char *alt_langs[3] = { 0 };
      int argc = switch_separate_string((char *) var, ',', alt_langs, 3);

			languages.push_back(lang); // primary language
      for (int i = 0; i < argc; i++) {
				languages.push_back( alt_langs[i]);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "added alternative lang %s\n", alt_langs[i]);
      }
			auto autoDetectSourceLanguageConfig = AutoDetectSourceLanguageConfig::FromLanguages(languages);
			m_recognizer = SpeechRecognizer::FromConfig(speechConfig, autoDetectSourceLanguageConfig, audioConfig);
    }
		else {
			auto sourceLanguageConfig = SourceLanguageConfig::FromLanguage(lang);
			m_recognizer = SpeechRecognizer::FromConfig(speechConfig, sourceLanguageConfig, audioConfig);
		}


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
		else properties.SetProperty(PropertyId::SpeechServiceConnection_InitialSilenceTimeoutMs, DEFAULT_SPEECH_TIMEOUT);

    const char* segmentationInterval = switch_channel_get_variable(channel, "AZURE_SPEECH_SEGMENTATION_SILENCE_TIMEOUT_MS");
    if (segmentationInterval) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "setting segmentation interval to %s ms\n", segmentationInterval);
      properties.SetProperty(PropertyId::Speech_SegmentationSilenceTimeoutMs, segmentationInterval);
    }

		// recognition mode - readonly according to Azure docs: 
		// https://docs.microsoft.com/en-us/javascript/api/microsoft-cognitiveservices-speech-sdk/propertyid?view=azure-node-latest
		/*
		const char* recoMode = switch_channel_get_variable(channel, "AZURE_RECOGNITION_MODE");
		if (recoMode) {
			properties.SetProperty(PropertyId::SpeechServiceConnection_RecoMode, recoMode);
		}
		*/

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
				responseHandler(psession, TRANSCRIBE_EVENT_START_OF_UTTERANCE, NULL, m_bugname.c_str(), m_finished);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer start of speech\n");
				switch_core_session_rwunlock(psession);
			}
		};
		auto onSpeechEndDetected = [this, responseHandler](const RecognitionEventArgs& args) {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				auto sessionId = args.SessionId;
				responseHandler(psession, TRANSCRIBE_EVENT_END_OF_UTTERANCE, NULL, m_bugname.c_str(), m_finished);
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
						responseHandler(psession, TRANSCRIBE_EVENT_RESULTS, json.c_str(), m_bugname.c_str(), m_finished);
					break;
					case ResultReason::NoMatch:
						responseHandler(psession, TRANSCRIBE_EVENT_NO_SPEECH_DETECTED, json.c_str(), m_bugname.c_str(), m_finished);
					break;

					default:
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer unexpected result '%s': reason %d\n", 
							json.c_str(), reason);
            responseHandler(psession, TRANSCRIBE_EVENT_ERROR, json.c_str(), m_bugname.c_str(), m_finished);

					break;
				}
				switch_core_session_rwunlock(psession);
			}
		};

		auto onCanceled = [this, responseHandler](const SpeechRecognitionCanceledEventArgs& args) {
      if (m_finished) return;
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
        auto result = args.Result;
        auto details = args.ErrorDetails;
        auto code = args.ErrorCode;
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "error");
        cJSON_AddStringToObject(json, "error", details.c_str());
        char* jsonString = cJSON_PrintUnformatted(json);
        responseHandler(psession, TRANSCRIBE_EVENT_ERROR, jsonString, m_bugname.c_str(), m_finished);
        free(jsonString);
        cJSON_Delete(json);
				switch_core_session_rwunlock(psession);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer recognition canceled, error %d: %s\n", code, details.c_str());
      }
		};

		m_recognizer->SessionStopped += onSessionStopped;
		m_recognizer->SpeechStartDetected += onSpeechStartDetected;
		m_recognizer->SpeechEndDetected += onSpeechEndDetected;
		if (interim) m_recognizer->Recognizing += onRecognitionEvent;
		m_recognizer->Recognized += onRecognitionEvent;
		m_recognizer->Canceled += onCanceled;

		switch_core_session_rwunlock(psession);
	}

	~GStreamer() {
		//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::~GStreamer %p\n", this);		
	}

	void connect() {
		if (m_connecting) return;
		m_connecting = true;

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer:connect %p connecting to azure speech..\n", this);

		auto onSessionStarted = [this](const SessionEventArgs& args) {
			m_connected = true;
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				auto sessionId = args.SessionId;
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer got session started from microsoft\n");

				// send any buffered audio
				int nFrames = m_audioBuffer.getNumItems();
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p got session started from azure, %d buffered frames\n", this, nFrames);	
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
		m_recognizer->SessionStarted += onSessionStarted;
		m_recognizer->StartContinuousRecognitionAsync();

	}

	bool write(void* data, uint32_t datalen) {
		if (m_finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::write not writing because we are finished, %p\n", this);
			return false;
		}
		if (!m_connected) {
      if (datalen % CHUNKSIZE == 0) {
        m_audioBuffer.add(data, datalen);
      }
      return true;
    }

    m_pushStream->Write(static_cast<uint8_t*>(data), datalen);
		return true;
	}

	void finish() {
		if (m_finished) return;
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::finish - calling  StopContinuousRecognitionAsync (%p)\n", this);
		m_finished = true;
		m_recognizer->StopContinuousRecognitionAsync().get();
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer::finish - recognition has completed (%p)\n", this);
	}

	bool isStopped() {
		return m_stopped;
	}

	bool isConnecting() {
    return m_connecting;
  }

private:
	std::string m_sessionId;
	std::string m_bugname;
	std::string  m_region;
	std::shared_ptr<SpeechRecognizer> m_recognizer;
	std::shared_ptr<PushAudioInputStream> m_pushStream;

	responseHandler_t m_responseHandler;
	bool m_interim;
	bool m_finished;
	bool m_connected;
	bool m_connecting;
	bool m_stopped;
	SimpleBuffer m_audioBuffer;
};

static void reaper(struct cap_cb *cb) {
	std::shared_ptr<GStreamer> pStreamer;
	pStreamer.reset((GStreamer *)cb->streamer);
	cb->streamer = nullptr;

	std::thread t([pStreamer]{
		pStreamer->finish();
	});
	t.detach();
}

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
		if (cb->vad) {
			switch_vad_destroy(&cb->vad);
			cb->vad = nullptr;
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
          uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char* bugname, void **ppUserData
	) {
		GStreamer *streamer = NULL;
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
		strncpy(cb->sessionId, sessionId, MAX_SESSION_ID);
		strncpy(cb->bugname, bugname, MAX_BUG_LEN);

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

		try {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s: initializing gstreamer with %s\n", 
					switch_channel_get_name(channel), bugname);
			streamer = new GStreamer(sessionId, bugname, channels, lang, interim, sampleRate, cb->region, subscriptionKey, responseHandler);
			cb->streamer = streamer;
			if (!cb->vad) streamer->connect();
		} catch (std::exception& e) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
				switch_channel_get_name(channel), e.what());
			return SWITCH_STATUS_FALSE;
		}


		*ppUserData = cb;
	
	done:
		return status;
	}

	switch_status_t azure_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing, char* bugname) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, bugname);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_mutex_lock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "azure_transcribe_session_stop: locked session\n");

			switch_channel_set_private(channel, bugname, NULL);
			if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) reaper(cb);
			killcb(cb);
			switch_mutex_unlock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "azure_transcribe_session_stop: unlocked session\n");

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
						if (cb->vad && !streamer->isConnecting()) {
							switch_vad_state_t state = switch_vad_process(cb->vad, (int16_t*) frame.data, frame.samples);
							if (state == SWITCH_VAD_STATE_START_TALKING) {
								switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "detected speech, connect to azure speech now\n");
								streamer->connect();
								cb->responseHandler(session, TRANSCRIBE_EVENT_VAD_DETECTED, NULL, cb->bugname, 0);
							}
						}

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
