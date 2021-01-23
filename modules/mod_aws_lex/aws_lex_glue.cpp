#include <cstdlib>

#include <switch.h>
#include <switch_json.h>

#include <string.h>
#include <mutex>
#include <thread>
#include <condition_variable>

#include <fstream>
#include <string>
#include <sstream>
#include <map>

#include <float.h>

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/logging/DefaultLogSystem.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/lexv2-runtime/LexRuntimeV2Client.h>
#include <aws/lexv2-runtime/model/StartConversationRequest.h>

#include "mod_aws_lex.h"
#include "parser.h"

using namespace Aws;
using namespace Aws::Utils;
using namespace Aws::Auth;
using namespace Aws::LexRuntimeV2;
using namespace Aws::LexRuntimeV2::Model;


const char ALLOC_TAG[] = "drachtio";

static uint64_t playCount = 0;
static std::multimap<std::string, std::string> audioFiles;
static bool hasDefaultCredentials = false;
static bool awsLoggingEnabled = false;
static const char *endpointOverride = std::getenv("AWS_LEX_ENDPOINT_OVERRIDE");
static std::vector<Aws::String> locales{"en_AU", "en_GB", "en_US", "fr_CA", "fr_FR", "es_ES", "es_US", "it_IT"};

static switch_status_t hanguphook(switch_core_session_t *session) {
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_channel_state_t state = switch_channel_get_state(channel);

	if (state == CS_HANGUP || state == CS_ROUTING) {
		char * sessionId = switch_core_session_get_uuid(session);
		auto range = audioFiles.equal_range(sessionId);
		for (auto it = range.first; it != range.second; it++) {
			std::string filename = it->second;
			std::remove(filename.c_str());
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
				"aws_lex_session_cleanup: removed audio file %s\n", filename.c_str());
		}
		audioFiles.erase(sessionId);
		switch_core_event_hook_remove_state_change(session, hanguphook);
	}
	return SWITCH_STATUS_SUCCESS;
}

static bool parseMetadata(Aws::Map<Aws::String, Slot>& slots, Aws::Map<Aws::String, Aws::String>& attributes, char* metadata) {
	cJSON* json = cJSON_Parse(metadata);
	if (json) {
		int numItems = cJSON_GetArraySize(json);
		for (int i = 0; i < numItems; i++) {
			cJSON* item = cJSON_GetArrayItem(json, i);
			if (0 == strcmp("slots", item->string)) {
				// pre-fill slots
				if (cJSON_Object == item->type) {
					int numSlots = cJSON_GetArraySize(item);
					for (int j = 0; j < numSlots; j++) {
						Slot slot;
						Value value;
						cJSON* jSlot = cJSON_GetArrayItem(item, j);
						switch (jSlot->type) {
							case cJSON_False:
								value.SetInterpretedValue("false");
								slot.SetValue(value);
								slots[jSlot->string] = slot;
								break;
							case cJSON_True:
								value.SetInterpretedValue("true");
								slot.SetValue(value);
								slots[jSlot->string] = slot;
								break;
							case cJSON_Number:
							{
								double d = jSlot->valuedouble;
								char scratch[16];
								if ((fabs(((double)jSlot->valueint) - d) <= DBL_EPSILON) && (d <= INT_MAX) && (d >= INT_MIN)) {
									sprintf(scratch, "%d", jSlot->valueint);
								}
								else {
									sprintf(scratch, "%f", jSlot->valuedouble);
								}
								value.SetInterpretedValue(scratch);
								slot.SetValue(value);
								slots[jSlot->string] = slot;
							}
								break;
							case cJSON_String:
								value.SetInterpretedValue(jSlot->valuestring);
								slot.SetValue(value);
								slots[jSlot->string] = slot;
								break;
							default:
							break;
						}							
					}
				}
			}
			else if (0 == strcmp("context", item->string) && cJSON_Object == item->type) {
				char buf[4096];

				// special case: json string passed as x-amz-lex:channels:context to bot

				if (!cJSON_PrintPreallocated(item, buf, 4096, 0)) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "parse metadata fails due to excessive length\n");		
				}
				else {
					attributes["x-amz-lex:channels:context-format"] = "json";
					attributes["x-amz-lex:channels:context"] = buf;
				}
			}
			else {
				// attributes
				switch (item->type) {
					case cJSON_False:
						attributes[item->string] = false;
						break;
					case cJSON_True:
						attributes[item->string] = true;
						break;
					case cJSON_Number:
					{
						double d = item->valuedouble;
						 if ((fabs(((double)item->valueint) - d) <= DBL_EPSILON) && (d <= INT_MAX) && (d >= INT_MIN)) {
							attributes[item->string] = item->valueint;
						 }
						 else {
							attributes[item->string] = d;
						 }
					}
						break;
					case cJSON_String:
						attributes[item->string] = item->valuestring;
						break;
					default:
						break;
				}
			}
		}
		int count = slots.size() + attributes.size();

		cJSON_Delete(json);

		return count > 0;
	}
	return false;
}

class GStreamer {
public:
	GStreamer(const char *sessionId, 
		char* bot, 
		char* alias, 
		char* region,
		char *locale,
		char *intentName, 
		char *metadata,
		const char* awsAccessKeyId, 
		const char* awsSecretAccessKey,
		responseHandler_t responseHandler,
		errorHandler_t  errorHandler) : 
	m_bot(bot), m_alias(alias), m_region(region), m_sessionId(sessionId), m_finished(false), m_finishing(false), m_packets(0),
	m_pStream(nullptr), m_bPlayDone(false), m_bDiscardAudio(false)
	{
		Aws::String key(awsAccessKeyId);
		Aws::String secret(awsSecretAccessKey);
		Aws::String awsLocale(locale);
		Aws::Client::ClientConfiguration config;
		config.region = region;
		if (endpointOverride) config.endpointOverride = endpointOverride;
		char keySnippet[20];

		strncpy(keySnippet, awsAccessKeyId, 4);
		for (int i = 4; i < 20; i++) keySnippet[i] = 'x';
		keySnippet[19] = '\0';

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p ACCESS_KEY_ID %s\n", this, keySnippet);		
		if (*awsAccessKeyId && *awsSecretAccessKey) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "using AWS creds %s %s\n", awsAccessKeyId, awsSecretAccessKey);	
			m_client = Aws::MakeUnique<LexRuntimeV2Client>(ALLOC_TAG, AWSCredentials(awsAccessKeyId, awsSecretAccessKey), config);
		}
		else {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "No AWS credentials so using default credentials\n");	
			m_client = Aws::MakeUnique<LexRuntimeV2Client>(ALLOC_TAG, config);
		}
	
    m_handler.SetHeartbeatEventCallback([this](const HeartbeatEvent&)
    {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p recv heartbeat\n", this);	
    });

		m_handler.SetPlaybackInterruptionEventCallback([this, responseHandler](const PlaybackInterruptionEvent& ev) 
		{
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				cJSON* json = lex2Json(ev);
				char* data = cJSON_PrintUnformatted(json);

				responseHandler(psession, AWS_LEX_EVENT_PLAYBACK_INTERRUPTION, const_cast<char *>(data));

				free(data);
				cJSON_Delete(json);
				switch_core_session_rwunlock(psession);
			}
		});

    m_handler.SetTranscriptEventCallback([this, responseHandler](const TranscriptEvent& ev)
    {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				cJSON* json = lex2Json(ev);
				char* data = cJSON_PrintUnformatted(json);

				responseHandler(psession, AWS_LEX_EVENT_TRANSCRIPTION, const_cast<char *>(data));

				free(data);
				cJSON_Delete(json);
				switch_core_session_rwunlock(psession);
			}
    });

    m_handler.SetTextResponseEventCallback([this, responseHandler](const TextResponseEvent& ev){
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				cJSON* json = lex2Json(ev);
				char* data = cJSON_PrintUnformatted(json);

				responseHandler(psession, AWS_LEX_EVENT_TEXT_RESPONSE, data);

				free(data);
				cJSON_Delete(json);
				switch_core_session_rwunlock(psession);
			}
    });

    m_handler.SetAudioResponseEventCallback([this, responseHandler](const AudioResponseEvent& ev){
			if (m_bDiscardAudio) return;
	
			const Aws::Utils::ByteBuffer& audio = ev.GetAudioChunk();
			uint32_t bytes = audio.GetLength();
			auto contentType = ev.GetContentType();
			auto eventId = ev.GetEventId();
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				if (!m_f.is_open()) {
					if (0 == bytes) return;
						m_ostrCurrentPath.str("");
						m_ostrCurrentPath << SWITCH_GLOBAL_dirs.temp_dir << SWITCH_PATH_SEPARATOR << m_sessionId << "_" <<  ++playCount << ".mp3";
						switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "GStreamer %p: writing new audio file %s\n", this, m_ostrCurrentPath.str().c_str());
						m_f.open(m_ostrCurrentPath.str(), std::ofstream::binary);
						m_f.write((const char*) audio.GetUnderlyingData(), bytes);

						// add the file to the list of files played for this session, we'll delete when session closes
						audioFiles.insert(std::pair<std::string, std::string>(m_sessionId, m_ostrCurrentPath.str().c_str()));
				}
				else if (0 == bytes) {
					switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "GStreamer %p: closing audio file %s\n", this, m_ostrCurrentPath.str().c_str());
					m_f.flush();
					m_f.close();

					std::ostringstream s;
					s << "{\"path\": \"" << m_ostrCurrentPath.str() << "\"}";

					responseHandler(psession, AWS_LEX_EVENT_AUDIO_PROVIDED, const_cast<char *>(s.str().c_str()));
				}
				else {
					m_f.write((const char*) audio.GetUnderlyingData(), bytes);
				}
				switch_core_session_rwunlock(psession);
			}
    });

    m_handler.SetIntentResultEventCallback([this, responseHandler](const IntentResultEvent& ev)
    {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				cJSON* json = lex2Json(ev);
				char* data = cJSON_PrintUnformatted(json);

				responseHandler(psession, AWS_LEX_EVENT_INTENT, data);

				free(data);
				cJSON_Delete(json);
				switch_core_session_rwunlock(psession);
			}
   });

    m_handler.SetOnErrorCallback([this, errorHandler](const Aws::Client::AWSError<LexRuntimeV2Errors>& err)
    {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				cJSON* json = lex2Json(err);
				char* data = cJSON_PrintUnformatted(json);

 				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p stream got error: %s\n", this, data);

				errorHandler(psession, data);

				free(data);
				cJSON_Delete(json);
				switch_core_session_rwunlock(psession);
			}
    });

		if (locales.end() == std::find(locales.begin(), locales.end(), awsLocale)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p invalid locale %s provided, defaulting to en-US\n", this, locale);
			awsLocale = "en_US";
		}

    m_request.SetBotId(bot);
    m_request.SetBotAliasId(alias);
    m_request.SetSessionId(sessionId);
    m_request.SetEventStreamHandler(m_handler);
		m_request.SetLocaleId(awsLocale);

 		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p sessionId %s, botId %s, alias %s, region %s, locale %s \n", this, sessionId, bot, alias, region, awsLocale.c_str());

    auto OnStreamReady = [this, metadata, intentName](StartConversationRequestEventStream& stream)
    {
			switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
			if (psession) {
				switch_channel_t* channel = switch_core_session_get_channel(psession);
				Aws::Map<Aws::String, Aws::String> sessionAttributes;

				m_pStream = &stream;
				
				// check channel vars for lex session attributes
				bool bargein = false;
				const char* var;
				if (switch_channel_var_true(channel, "LEX_USE_TTS")) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer %p using tts so audio packets will be discarded\n", this);
					m_bDiscardAudio = true;
				}
				if (var = switch_channel_get_variable(channel, "x-amz-lex:audio:start-timeout-ms")) {
					sessionAttributes.insert({"x-amz-lex:audio:start-timeout-ms:*:*", var});
				}

				Aws::Map<Aws::String, Slot> slots;
				if (metadata) parseMetadata(slots, sessionAttributes, metadata);

				SessionState sessionState;
				sessionState.SetSessionAttributes(sessionAttributes);

				ConfigurationEvent configurationEvent;
				configurationEvent.SetResponseContentType("audio/mpeg");

				Intent intent;
				if (intentName && strlen(intentName) > 0) {
					DialogAction dialogAction;

					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting initial intent to '%s'\n", this, intentName);
					intent.SetName(intentName);

					for (auto const& pair : slots) {
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting slot %s\n", this, pair.first.c_str());
						intent.AddSlots(pair.first, pair.second);
					}

					sessionState.SetIntent(intent);
					dialogAction.SetType(DialogActionType::Delegate);
					sessionState.SetDialogAction(dialogAction);
				}
				else if (var = switch_channel_get_variable(channel, "LEX_WELCOME_MESSAGE")) {
					Message message;
					DialogAction dialogAction;

					dialogAction.SetType(DialogActionType::ElicitIntent);
					sessionState.SetDialogAction(dialogAction);
					message.SetContent(var);
					message.SetContentType(MessageContentType::PlainText);
					configurationEvent.AddWelcomeMessages(message);		

					// erase the channel var, so it is not reused in future intent
					switch_channel_set_variable(channel, "LEX_WELCOME_MESSAGE", nullptr);

					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting welcome message: %s\n", this, var);
				}
				configurationEvent.SetSessionState(sessionState);

				stream.WriteConfigurationEvent(configurationEvent);
				stream.flush();

				PlaybackCompletionEvent playbackCompletionEvent;
				stream.WritePlaybackCompletionEvent(playbackCompletionEvent);
				stream.flush();

				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p got stream ready\n", this);		
				switch_core_session_rwunlock(psession);
			}
    };
    auto OnResponseCallback = [&](const LexRuntimeV2Client* pClient,
            const StartConversationRequest& request,
            const StartConversationOutcome& outcome,
            const std::shared_ptr<const Aws::Client::AsyncCallerContext>&)
    {
 			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p stream got final response\n", this);
			if (!outcome.IsSuccess()) {				
				const LexRuntimeV2Error& err = outcome.GetError();
				auto message = err.GetMessage();
				auto exception = err.GetExceptionName();
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p stream got error response %s : %s\n", this, message.c_str(), exception.c_str());
			}

			std::lock_guard<std::mutex> lk(m_mutex);
			m_finished = true;
			m_cond.notify_one();
    };

 		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p starting conversation\n", this);
		m_client->StartConversationAsync(m_request, OnStreamReady, OnResponseCallback, nullptr/*context*/);
	}

	~GStreamer() {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::~GStreamer wrote %d packets %p\n", m_packets, this);		
	}

	void dtmf(char* dtmf) {
		if (m_finishing || m_finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::dtmf not writing because we are finished, %p\n", this);
			return;
		}

		DTMFInputEvent dtmfInputEvent;
		dtmfInputEvent.SetInputCharacter(dtmf);
		m_pStream->WriteDTMFInputEvent(dtmfInputEvent);
		m_pStream->flush();
	}

	void notify_play_done() {
		std::lock_guard<std::mutex> lk(m_mutex);
		m_bPlayDone = true;
		m_cond.notify_one();
	}

	bool write(void* data, uint32_t datalen) {
		if (m_finishing || m_finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::write not writing because we are finished, %p\n", this);
			return false;
		}
		//m_fOutgoingAudio.write((const char*) data, datalen);
		Aws::Utils::ByteBuffer audio((const unsigned char *) data, datalen);
		AudioInputEvent audioInputEvent;
		audioInputEvent.SetAudioChunk(audio);
		audioInputEvent.SetContentType("audio/lpcm; sample-rate=8000; sample-size-bits=16; channel-count=1; is-big-endian=false");
		m_pStream->WriteAudioInputEvent(audioInputEvent);
		m_pStream->flush();

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
				return  m_bPlayDone || m_finished  || (m_finishing && !shutdownInitiated);
			});

			// we have data to process or have been told we're done
			if (m_finished) return;
			if (m_finishing) {
				shutdownInitiated = true;
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::writing disconnect event %p\n", this);

				if (m_pStream) {
					m_pStream->WriteAudioInputEvent({}); // per the spec, we have to send an empty event (i.e. without a payload) at the end.
					DisconnectionEvent disconnectionEvent;
					m_pStream->WriteDisconnectionEvent(disconnectionEvent);

					m_pStream->flush();
					m_pStream->Close();
					m_pStream = nullptr;

					//m_fOutgoingAudio.flush();
					//m_fOutgoingAudio.close();

				}
			}
			else {
				if (m_bPlayDone) {
					m_bPlayDone = false;
					PlaybackCompletionEvent playbackCompletionEvent;
					m_pStream->WritePlaybackCompletionEvent(playbackCompletionEvent);
					m_pStream->flush();
				}
			}
		}
	}


private:
	std::string m_sessionId;
	std::string  m_bot;
	std::string  m_alias;
	std::string  m_region;
	Aws::UniquePtr<LexRuntimeV2Client> m_client;
	StartConversationRequestEventStream* m_pStream;
	StartConversationRequest m_request;
	StartConversationHandler m_handler;

	bool m_finishing;
	bool m_finished;
	uint32_t m_packets;
	std::mutex m_mutex;
	std::condition_variable m_cond;
	std::ofstream m_f;
	std::ostringstream m_ostrCurrentPath;
	//std::ofstream m_fOutgoingAudio;
	bool m_bPlayDone;
	bool m_bDiscardAudio;
};

static void *SWITCH_THREAD_FUNC lex_thread(switch_thread_t *thread, void *obj) {
	struct cap_cb *cb = (struct cap_cb *) obj;
	bool ok = true;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lex_thread: starting cb %p\n", (void *) cb);
	GStreamer* pStreamer = new GStreamer(cb->sessionId, cb->bot, cb->alias, cb->region, cb->locale, 
		cb->intent, cb->metadata, cb->awsAccessKeyId, cb->awsSecretAccessKey, 
		cb->responseHandler, cb->errorHandler);
	if (!pStreamer) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lex_thread: Error allocating streamer\n");
		return nullptr;
	}
	cb->streamer = pStreamer;

	pStreamer->processData();
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lex_thread: stopping cb %p\n", (void *) cb);
	delete pStreamer;
	cb->streamer = nullptr;
	return NULL;
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
	}
}




extern "C" {
	switch_status_t aws_lex_init() {
		const char* accessKeyId = std::getenv("AWS_ACCESS_KEY_ID");
		const char* secretAccessKey= std::getenv("AWS_SECRET_ACCESS_KEY");
		const char* awsTrace = std::getenv("AWS_TRACE");
		if (NULL == accessKeyId && NULL == secretAccessKey) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
				"\"AWS_ACCESS_KEY_ID\"  and/or \"AWS_SECRET_ACCESS_KEY\" env var not set; authentication will expect channel variables of same names to be set\n");
		}
		else {
			hasDefaultCredentials = true;

		}
    Aws::SDKOptions options;

		if (awsTrace && 0 == strcmp("1", awsTrace)) {
			awsLoggingEnabled = true;
			options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;

			Aws::Utils::Logging::InitializeAWSLogging(
					Aws::MakeShared<Aws::Utils::Logging::DefaultLogSystem>(
						ALLOC_TAG, Aws::Utils::Logging::LogLevel::Trace, "aws_sdk_"));
		}

    Aws::InitAPI(options);



		return SWITCH_STATUS_SUCCESS;
	}
	
	switch_status_t aws_lex_cleanup() {
		Aws::SDKOptions options;
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_aws_lex: shutting down API");
		if (awsLoggingEnabled) {
			options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;
			Aws::Utils::Logging::ShutdownAWSLogging();
		}
	
    Aws::ShutdownAPI(options);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_aws_lex: shutdown API complete");

		return SWITCH_STATUS_SUCCESS;
	}

	// start lex on a channel
	switch_status_t aws_lex_session_init(
		switch_core_session_t *session, 
		responseHandler_t responseHandler, 
		errorHandler_t errorHandler, 
		uint32_t samples_per_second, 
		char* bot, 
		char* alias,
		char* region, 
		char* locale,
		char* intent, 
		char* metadata,
		struct cap_cb **ppUserData
	) {
		switch_status_t status = SWITCH_STATUS_SUCCESS;
		switch_channel_t *channel = switch_core_session_get_channel(session);
		int err;
		switch_threadattr_t *thd_attr = NULL;
		switch_memory_pool_t *pool = switch_core_session_get_pool(session);
		struct cap_cb* cb = (struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
		memset(cb, sizeof(cb), 0);
		const char* awsAccessKeyId = switch_channel_get_variable(channel, "AWS_ACCESS_KEY_ID");
		const char* awsSecretAccessKey = switch_channel_get_variable(channel, "AWS_SECRET_ACCESS_KEY");

		if (!hasDefaultCredentials && (!awsAccessKeyId || !awsSecretAccessKey)) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
				"missing credentials: AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY must be suuplied either as an env variable or channel variable\n");
			status = SWITCH_STATUS_FALSE;
			goto done;
		}

		strncpy(cb->sessionId, switch_core_session_get_uuid(session), 256);

		if (awsAccessKeyId && awsSecretAccessKey) {
			strncpy(cb->awsAccessKeyId, awsAccessKeyId, 128);
			strncpy(cb->awsSecretAccessKey, awsSecretAccessKey, 128);
		}
		else {
			strncpy(cb->awsAccessKeyId, std::getenv("AWS_ACCESS_KEY_ID"), 128);
			strncpy(cb->awsSecretAccessKey, std::getenv("AWS_SECRET_ACCESS_KEY"), 128);		
		}

		cb->responseHandler = responseHandler;
		cb->errorHandler = errorHandler;

		if (switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing mutex\n");
			status = SWITCH_STATUS_FALSE;
			goto done; 
		}

		strncpy(cb->bot, bot, MAX_BOTNAME);
		strncpy(cb->alias, alias, MAX_BOTNAME);
		strncpy(cb->locale, locale, MAX_LOCALE);
		strncpy(cb->region, region, MAX_REGION);
		if (intent) strncpy(cb->intent, intent, MAX_INTENT);
		if (metadata) strncpy(cb->metadata, metadata, MAX_METADATA);
		cb->resampler = speex_resampler_init(1, 8000, /*16000*/ 8000, SWITCH_RESAMPLE_QUALITY, &err);
		if (0 != err) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n", 
						switch_channel_get_name(channel), speex_resampler_strerror(err));
			status = SWITCH_STATUS_FALSE;
			goto done;
		}

		// hangup hook to clear temp audio files
		switch_core_event_hook_add_state_change(session, hanguphook);

		// create a thread to service the http/2 connection to lex
		switch_threadattr_create(&thd_attr, pool);
		switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
		switch_thread_create(&cb->thread, thd_attr, lex_thread, cb, pool);

		*ppUserData = cb;
	
	done:
		return status;
	}

	switch_status_t aws_lex_session_dtmf(switch_core_session_t *session, char* dtmf) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_mutex_lock(cb->mutex);
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_lex_session_dtmf: sending dtmf %s\n", dtmf);
				streamer->dtmf(dtmf);
			}
			switch_mutex_unlock(cb->mutex);

			return SWITCH_STATUS_SUCCESS;
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
		return SWITCH_STATUS_FALSE;
	}

	switch_status_t aws_lex_session_play_done(switch_core_session_t *session) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_mutex_lock(cb->mutex);
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_lex_session_play_done: sending play done\n");
				streamer->notify_play_done();
			}
			switch_mutex_unlock(cb->mutex);

			return SWITCH_STATUS_SUCCESS;
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
		return SWITCH_STATUS_FALSE;
	}

	switch_status_t aws_lex_session_stop(switch_core_session_t *session, int channelIsClosing) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_lex_session_cleanup: acquiring lock\n");
			switch_mutex_lock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_lex_session_cleanup: acquired lock\n");
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_lex_session_cleanup: sending writesDone..\n");
				streamer->finish();
			}
			if (cb->thread) {
				switch_status_t retval;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "aws_lex_session_cleanup: waiting for read thread to complete\n");
				switch_thread_join(&retval, cb->thread);
				cb->thread = NULL;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "aws_lex_session_cleanup: read thread completed\n");
			}
			killcb(cb);

			switch_channel_set_private(channel, MY_BUG_NAME, NULL);
			if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

			switch_mutex_unlock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "aws_lex_session_cleanup: Closed aws session\n");

			return SWITCH_STATUS_SUCCESS;
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
		return SWITCH_STATUS_FALSE;
	}
	
	switch_bool_t aws_lex_frame(switch_media_bug_t *bug, void* user_data) {
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
						
						speex_resampler_process_interleaved_int(cb->resampler, (const spx_int16_t *) frame.data, (spx_uint32_t *) &in_len, &out[0], &out_len);
						
						streamer->write( &out[0], sizeof(spx_int16_t) * out_len);
					}
				}
			}
			else {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
					"aws_lex_frame: not sending audio because aws channel has been closed\n");
			}
			switch_mutex_unlock(cb->mutex);
		}
		else {
			//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
			//	"aws_lex_frame: not sending audio since failed to get lock on mutex\n");
		}
		return SWITCH_TRUE;
	}

	void destroyChannelUserData(struct cap_cb* cb) {
		killcb(cb);
	}

}
