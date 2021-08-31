#include <cstdlib>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>
#include <string.h>
#include <mutex>
#include <condition_variable>

#include <regex>

#include <fstream>
#include <string>
#include <sstream>
#include <map>

#include "google/cloud/dialogflow/v2beta1/session.grpc.pb.h"

#include "mod_dialogflow.h"
#include "parser.h"

using google::cloud::dialogflow::v2beta1::Sessions;
using google::cloud::dialogflow::v2beta1::StreamingDetectIntentRequest;
using google::cloud::dialogflow::v2beta1::StreamingDetectIntentResponse;
using google::cloud::dialogflow::v2beta1::AudioEncoding;
using google::cloud::dialogflow::v2beta1::InputAudioConfig;
using google::cloud::dialogflow::v2beta1::OutputAudioConfig;
using google::cloud::dialogflow::v2beta1::SynthesizeSpeechConfig;
using google::cloud::dialogflow::v2beta1::QueryInput;
using google::cloud::dialogflow::v2beta1::QueryResult;
using google::cloud::dialogflow::v2beta1::StreamingRecognitionResult;
using google::cloud::dialogflow::v2beta1::EventInput;
using google::rpc::Status;
using google::protobuf::Struct;
using google::protobuf::Value;
using google::protobuf::MapPair;

static uint64_t playCount = 0;
static std::multimap<std::string, std::string> audioFiles;
static bool hasDefaultCredentials = false;

static switch_status_t hanguphook(switch_core_session_t *session) {
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_channel_state_t state = switch_channel_get_state(channel);

	if (state == CS_HANGUP || state == CS_ROUTING) {
		char * sessionId = switch_core_session_get_uuid(session);
		typedef std::multimap<std::string, std::string>::iterator MMAPIterator;
		std::pair<MMAPIterator, MMAPIterator> result = audioFiles.equal_range(sessionId);
		for (MMAPIterator it = result.first; it != result.second; it++) {
			std::string filename = it->second;
			std::remove(filename.c_str());
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
				"google_dialogflow_session_cleanup: removed audio file %s\n", filename.c_str());
		}
		audioFiles.erase(sessionId);
		switch_core_event_hook_remove_state_change(session, hanguphook);
	}
	return SWITCH_STATUS_SUCCESS;
}

static  void parseEventParams(Struct* grpcParams, cJSON* json) {
	auto* map = grpcParams->mutable_fields();
	int count = cJSON_GetArraySize(json);
	for (int i = 0; i < count; i++) {
		cJSON* prop = cJSON_GetArrayItem(json, i);
		if (prop) {
			google::protobuf::Value v;
			switch (prop->type) {
				case cJSON_False:
				case cJSON_True:
					v.set_bool_value(prop->type == cJSON_True);
					break;

				case cJSON_Number:
					v.set_number_value(prop->valuedouble);
					break;

				case cJSON_String:
					v.set_string_value(prop->valuestring);
					break;

				case cJSON_Array:
				case cJSON_Object:
				case cJSON_Raw:
				case cJSON_NULL:
					continue;
			}
			map->insert(MapPair<std::string, Value>(prop->string, v));
		}
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "parseEventParams: added %d event params\n", map->size());
}

void tokenize(std::string const &str, const char delim, std::vector<std::string> &out) {
    size_t start = 0;
    size_t end = 0;
		bool finished = false;
		do {
			end = str.find(delim, start);
			if (end == std::string::npos) {
				finished = true;
				out.push_back(str.substr(start));
			}
			else {
				out.push_back(str.substr(start, end - start));
				start = ++end;
			}
		} while (!finished);
}

class GStreamer {
public:
	GStreamer(switch_core_session_t *session, const char* lang, char* projectId, char* event, char* text) : 
	m_lang(lang), m_sessionId(switch_core_session_get_uuid(session)), m_environment("draft"), m_regionId("us"),
	m_finished(false), m_packets(0) {
		const char* var;
		switch_channel_t* channel = switch_core_session_get_channel(session);
		std::vector<std::string> tokens;
		const char delim = ':';
		tokenize(projectId, delim, tokens);
		int idx = 0;
		for (auto &s: tokens) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer: token %d: '%s'\n", idx, s.c_str());
			if (0 == idx) m_projectId = s;
			else if (1 == idx && s.length() > 0) m_environment = s;
			else if (2 == idx && s.length() > 0) m_regionId = s;
			idx++;
		}

		std::string endpoint = "dialogflow.googleapis.com";
		if (0 != m_regionId.compare("us")) {
			endpoint = m_regionId;
			endpoint.append("-dialogflow.googleapis.com:443");
		}

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
			"GStreamer dialogflow endpoint is %s, region is %s, project is %s, environment is %s\n", 
			endpoint.c_str(), m_regionId.c_str(), m_projectId.c_str(), m_environment.c_str());		

		if (var = switch_channel_get_variable(channel, "GOOGLE_APPLICATION_CREDENTIALS")) {
				auto callCreds = grpc::ServiceAccountJWTAccessCredentials(var, INT64_MAX);
				auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
				auto creds = grpc::CompositeChannelCredentials(channelCreds, callCreds);
				m_channel = grpc::CreateChannel(endpoint, creds);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer json credentials are %s\n", var); 
		}
		else {
			auto creds = grpc::GoogleDefaultCredentials();
			m_channel = grpc::CreateChannel(endpoint, creds);
		}
		startStream(session, event, text);
	}

	~GStreamer() {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::~GStreamer wrote %ld packets %p\n", m_packets, this);		
	}

	void startStream(switch_core_session_t *session, const char* event, const char* text) {
		char szSession[256];

		m_request = std::make_shared<StreamingDetectIntentRequest>();
		m_context= std::make_shared<grpc::ClientContext>();
		m_stub = Sessions::NewStub(m_channel);

		snprintf(szSession, 256, "projects/%s/locations/%s/agent/environments/%s/users/-/sessions/%s", 
				m_projectId.c_str(), m_regionId.c_str(), m_environment.c_str(), m_sessionId.c_str());

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer::startStream session %s, event %s, text %s %p\n", szSession, event, text, this);

		m_request->set_session(szSession);
		auto* queryInput = m_request->mutable_query_input();
		if (event) {
			auto* eventInput = queryInput->mutable_event();
			eventInput->set_name(event);
			eventInput->set_language_code(m_lang.c_str());
			if (text) {
				cJSON* json = cJSON_Parse(text);
				if (!json) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer::startStream ignoring event params since it is not json %s\n", text);
				}
				else {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::startStream adding event params (JSON) %s\n", text);
					auto* eventParams = eventInput->mutable_parameters();
					parseEventParams(eventParams, json);
					cJSON_Delete(json);
				}
			}
		}
		else if (text) {
			auto* textInput = queryInput->mutable_text();
			textInput->set_text(text);
			textInput->set_language_code(m_lang.c_str());
		}
		else {
			auto* audio_config = queryInput->mutable_audio_config();
			audio_config->set_sample_rate_hertz(16000);
			audio_config->set_audio_encoding(AudioEncoding::AUDIO_ENCODING_LINEAR_16);
			audio_config->set_language_code(m_lang.c_str());
			audio_config->set_single_utterance(true);
		}

  	m_streamer = m_stub->StreamingDetectIntent(m_context.get());
  	m_streamer->Write(*m_request);
	}
	bool write(void* data, uint32_t datalen) {
		if (m_finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::write not writing because we are finished, %p\n", this);
			return false;
		}

		m_request->clear_query_input();
		m_request->clear_query_params();
		m_request->set_input_audio(data, datalen);

		m_packets++;
    return m_streamer->Write(*m_request);

	}
	bool read(StreamingDetectIntentResponse* response) {
		return m_streamer->Read(response);
	}
	grpc::Status finish() {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::finish %p\n", this);
		if (m_finished) {
			grpc::Status ok;
			return ok;
		}
		m_finished = true;
		return m_streamer->Finish();
	}
	void writesDone() {
		m_streamer->WritesDone();
	}

	bool isFinished() {
		return m_finished;
	}

private:
	std::string m_sessionId;
	std::shared_ptr<grpc::ClientContext> m_context;
	std::shared_ptr<grpc::Channel> m_channel;
	std::unique_ptr<Sessions::Stub> 	m_stub;
	std::unique_ptr< grpc::ClientReaderWriterInterface<StreamingDetectIntentRequest, StreamingDetectIntentResponse> > m_streamer;
	std::shared_ptr<StreamingDetectIntentRequest> m_request;
	std::string m_lang;
	std::string m_projectId;
	std::string m_environment;
	std::string m_regionId;
	bool m_finished;
	uint32_t m_packets;
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

static void *SWITCH_THREAD_FUNC grpc_read_thread(switch_thread_t *thread, void *obj) {
	struct cap_cb *cb = (struct cap_cb *) obj;
	GStreamer* streamer = (GStreamer *) cb->streamer;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: starting cb %p\n", (void *) cb);

	// Our contract: while we are reading, cb and cb->streamer will not be deleted

	// Read responses until there are no more
	StreamingDetectIntentResponse response;
	while (streamer->read(&response)) {  
		switch_core_session_t* psession = switch_core_session_locate(cb->sessionId);
		if (psession) {
			switch_channel_t* channel = switch_core_session_get_channel(psession);
			GRPCParser parser(psession);

			if (response.has_query_result() || response.has_recognition_result()) {
				cJSON* jResponse = parser.parse(response) ;
				char* json = cJSON_PrintUnformatted(jResponse);
				const char* type = DIALOGFLOW_EVENT_TRANSCRIPTION;

				if (response.has_query_result()) type = DIALOGFLOW_EVENT_INTENT;
				else {
					const StreamingRecognitionResult_MessageType& o = response.recognition_result().message_type();
					if (0 == StreamingRecognitionResult_MessageType_Name(o).compare("END_OF_SINGLE_UTTERANCE")) {
						type = DIALOGFLOW_EVENT_END_OF_UTTERANCE;
					}
				}

				cb->responseHandler(psession, type, json);

				free(json);
				cJSON_Delete(jResponse);
			}

			const std::string& audio = parser.parseAudio(response);
			bool playAudio = !audio.empty() ;

			// save audio
			if (playAudio) {
				std::ostringstream s;
				s << SWITCH_GLOBAL_dirs.temp_dir << SWITCH_PATH_SEPARATOR <<
					cb->sessionId << "_" <<  ++playCount;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "grpc_read_thread: received audio to play\n");

				if (response.has_output_audio_config()) {
					const OutputAudioConfig& cfg = response.output_audio_config();
					switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "grpc_read_thread: encoding is %d\n", cfg.audio_encoding());
					if (cfg.audio_encoding() == OutputAudioEncoding::OUTPUT_AUDIO_ENCODING_MP3) {
						s << ".mp3";
					}
					else if (cfg.audio_encoding() == OutputAudioEncoding::OUTPUT_AUDIO_ENCODING_OGG_OPUS) {
						s << ".opus";
					}
					else {
						s << ".wav";
					}
				}
				std::ofstream f(s.str(), std::ofstream::binary);
				f << audio;
				f.close();
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(psession), SWITCH_LOG_DEBUG, "grpc_read_thread: wrote audio to %s\n", s.str().c_str());

				// add the file to the list of files played for this session, 
				// we'll delete when session closes
				audioFiles.insert(std::pair<std::string, std::string>(cb->sessionId, s.str()));

				cJSON * jResponse = cJSON_CreateObject();
				cJSON_AddItemToObject(jResponse, "path", cJSON_CreateString(s.str().c_str()));
				char* json = cJSON_PrintUnformatted(jResponse);

				cb->responseHandler(psession, DIALOGFLOW_EVENT_AUDIO_PROVIDED, json);
				free(json);
				cJSON_Delete(jResponse);
			}
			switch_core_session_rwunlock(psession);
		}
		else {
			break;
		}
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "dialogflow read loop is done\n");

	// finish the detect intent session: here is where we may get an error if credentials are invalid
	switch_core_session_t* psession = switch_core_session_locate(cb->sessionId);
	if (psession) {
		grpc::Status status = streamer->finish();
		if (!status.ok()) {
			std::ostringstream s;
			s << "{\"msg\": \"" << status.error_message() << "\", \"code\": " << status.error_code();
			if (status.error_details().length() > 0) {
				s << ", \"details\": \"" << status.error_details() << "\"";
			}
			s << "}";
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "StreamingDetectIntentRequest finished with err %s (%d): %s\n", 
				status.error_message().c_str(), status.error_code(), status.error_details().c_str());
			cb->errorHandler(psession, s.str().c_str());
		}

		switch_core_session_rwunlock(psession);
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "dialogflow read thread exiting	\n");
	return NULL;
}

extern "C" {
	switch_status_t google_dialogflow_init() {
		const char* gcsServiceKeyFile = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
		if (NULL == gcsServiceKeyFile) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
				"\"GOOGLE_APPLICATION_CREDENTIALS\" environment variable is not set; authentication will use \"GOOGLE_APPLICATION_CREDENTIALS\" channel variable\n");
		}
		else {
			hasDefaultCredentials = true;
		}
		return SWITCH_STATUS_SUCCESS;
	}
	
	switch_status_t google_dialogflow_cleanup() {
		return SWITCH_STATUS_SUCCESS;
	}

	// start dialogflow on a channel
	switch_status_t google_dialogflow_session_init(
		switch_core_session_t *session, 
		responseHandler_t responseHandler, 
		errorHandler_t errorHandler, 
		uint32_t samples_per_second, 
		char* lang, 
		char* projectId, 
		char* event, 
		char* text,
		struct cap_cb **ppUserData
	) {
		switch_status_t status = SWITCH_STATUS_SUCCESS;
		switch_channel_t *channel = switch_core_session_get_channel(session);
		int err;
		switch_threadattr_t *thd_attr = NULL;
		switch_memory_pool_t *pool = switch_core_session_get_pool(session);
		struct cap_cb* cb = (struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));

		if (!hasDefaultCredentials && !switch_channel_get_variable(channel, "GOOGLE_APPLICATION_CREDENTIALS")) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
				"missing credentials: GOOGLE_APPLICATION_CREDENTIALS must be suuplied either as an env variable (path to file) or a channel variable (json string)\n");
			status = SWITCH_STATUS_FALSE;
			goto done; 
		}

		strncpy(cb->sessionId, switch_core_session_get_uuid(session), 256);
		cb->responseHandler = responseHandler;
		cb->errorHandler = errorHandler;

		if (switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing mutex\n");
			status = SWITCH_STATUS_FALSE;
			goto done; 
		}

		strncpy(cb->lang, lang, MAX_LANG);
		strncpy(cb->projectId, lang, MAX_PROJECT_ID);
		cb->streamer = new GStreamer(session, lang, projectId, event, text);
		cb->resampler = speex_resampler_init(1, 8000, 16000, SWITCH_RESAMPLE_QUALITY, &err);
		if (0 != err) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n", 
						switch_channel_get_name(channel), speex_resampler_strerror(err));
			status = SWITCH_STATUS_FALSE;
			goto done;
		}

		// hangup hook to clear temp audio files
		switch_core_event_hook_add_state_change(session, hanguphook);

		// create the read thread
		switch_threadattr_create(&thd_attr, pool);
		//switch_threadattr_detach_set(thd_attr, 1);
		switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
		switch_thread_create(&cb->thread, thd_attr, grpc_read_thread, cb, pool);

		*ppUserData = cb;
	
	done:
		if (status != SWITCH_STATUS_SUCCESS) {
			killcb(cb);
		}
		return status;
	}

	switch_status_t google_dialogflow_session_stop(switch_core_session_t *session, int channelIsClosing) {
		switch_channel_t *channel = switch_core_session_get_channel(session);
		switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

		if (bug) {
			struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
			switch_status_t st;

			// close connection and get final responses
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_dialogflow_session_cleanup: acquiring lock\n");
			switch_mutex_lock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_dialogflow_session_cleanup: acquired lock\n");
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_dialogflow_session_cleanup: sending writesDone..\n");
				streamer->writesDone();
				streamer->finish();
			}
			if (cb->thread) {
				switch_status_t retval;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "google_dialogflow_session_cleanup: waiting for read thread to complete\n");
				switch_thread_join(&retval, cb->thread);
				cb->thread = NULL;
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "google_dialogflow_session_cleanup: read thread completed\n");
			}
			killcb(cb);

			switch_channel_set_private(channel, MY_BUG_NAME, NULL);
			if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

			switch_mutex_unlock(cb->mutex);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "google_dialogflow_session_cleanup: Closed google session\n");

			return SWITCH_STATUS_SUCCESS;
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
		return SWITCH_STATUS_FALSE;
	}
	
	switch_bool_t google_dialogflow_frame(switch_media_bug_t *bug, void* user_data) {
		switch_core_session_t *session = switch_core_media_bug_get_session(bug);
		uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
		switch_frame_t frame = {};
		struct cap_cb *cb = (struct cap_cb *) user_data;

		frame.data = data;
		frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

		if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
			GStreamer* streamer = (GStreamer *) cb->streamer;
			if (streamer && !streamer->isFinished()) {
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
				//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
				//	"google_dialogflow_frame: not sending audio because google channel has been closed\n");
			}
			switch_mutex_unlock(cb->mutex);
		}
		else {
			//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, 
			//	"google_dialogflow_frame: not sending audio since failed to get lock on mutex\n");
		}
		return SWITCH_TRUE;
	}

	void destroyChannelUserData(struct cap_cb* cb) {
		killcb(cb);
	}

}
