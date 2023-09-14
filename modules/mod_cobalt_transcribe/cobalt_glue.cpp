#include <cstdlib>
#include <algorithm>
#include <future>
#include <string>
#include <vector>
#include <sstream>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "cobaltspeech/transcribe/v5/transcribe.grpc.pb.h"

namespace cobalt_asr = cobaltspeech::transcribe::v5;

#include "mod_cobalt_transcribe.h"
#include "simple_buffer.h"

#define CHUNKSIZE (320)
#define DEFAULT_CONTEXT_TOKEN "unk:default"

namespace {
  int case_insensitive_match(std::string s1, std::string s2) {
   std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
   std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
   if(s1.compare(s2) == 0)
      return 1; //The strings are same
   return 0; //not matched
  }
  std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
      return "";
    }
    return str.substr(start, end - start + 1);
  }

  std::vector<std::string> splitAndTrim(const char* input, char delimiter) {
    std::string s(input);
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string token;

    while (getline(ss, token, delimiter)) {
      result.push_back(trim(token));
    }

    return result;
  }
  std::string base64_encode(const std::string &input) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
        encoded.push_back(chars[(val >> valb) & 0x3F]);
        valb -= 6;
      }
    }
    if (valb > -6) encoded.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (encoded.size() % 4) encoded.push_back('=');
    return encoded;
  }

  std::string base64_decode(const std::string &input) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[chars[i]] = i;

    std::string decoded;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
      if (T[c] == -1) break;
      val = (val << 6) + T[c];
      valb += 6;
      while (valb >= 0) {
          decoded.push_back(char((val >> valb) & 0xFF));
          valb -= 8;
      }
    }
    return decoded;
  }

  const char* compile_context_phrases(switch_core_session_t *session, const char* hostport, const char* model, const char* token, const char* phrases) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_event_t *event;

    grpc::ClientContext context;
    std::shared_ptr<grpc::Channel> grpcChannel ;
    grpcChannel = grpc::CreateChannel(hostport, grpc::InsecureChannelCredentials());

    if (!grpcChannel) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "failed creating grpc channel\n");	
      return nullptr;
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "compile context, model: %s, token: %s, phrases: %s\n", model, token, phrases);

    std::unique_ptr<cobalt_asr::TranscribeService::Stub> stub = std::move(cobalt_asr::TranscribeService::NewStub(grpcChannel));
    
    cobalt_asr::CompileContextRequest request;
    cobalt_asr::CompileContextResponse response;

    request.set_model_id(model);
    request.set_token(token);


    // hints are either a simple comma-separated list of phrases, or a json array of objects
    // containing a phrase and a boost value
    char* originalPhrases = strdup(phrases);
    request.clear_phrases();
    auto *jPhrases = cJSON_Parse((char *) phrases);
    if (jPhrases) {
      int i = 0;
      cJSON *jPhrase = NULL;
      cJSON_ArrayForEach(jPhrase, jPhrase) {
        auto* contextPhrase = request.add_phrases();
        cJSON *jItem = cJSON_GetObjectItem(jPhrase, "phrase");
        if (jItem) {
          auto text = cJSON_GetStringValue(jItem);
          contextPhrase->set_text(text);
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added text: %f\n", text);
          if (cJSON_GetObjectItem(jPhrase, "boost")) {
            float boost = (float) cJSON_GetObjectItem(jPhrase, "boost")->valuedouble;
            contextPhrase->set_boost(boost);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added boost value: %f\n", boost);
          }
          i++;
        }
      }
      cJSON_Delete(jPhrases);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added %d hints\n", i);
    }
    else {
      std::vector<std::string> tokens = splitAndTrim(phrases, ',');
      for (const std::string& token : tokens) {
        auto* contextPhrase = request.add_phrases();
        contextPhrase->set_text(token);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added: %s\n", token.c_str());
      }
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added %d hints\n", request.phrases_size());
    }

    stub->CompileContext(&context, request, &response);

    cJSON * jResult = cJSON_CreateObject();
    cJSON_AddBoolToObject(jResult, "has_context", response.has_context());
    auto& c = response.context();
    auto data = base64_encode(c.data());
    cJSON_AddItemToObject(jResult, "compiled_context", cJSON_CreateString(data.c_str()));
    cJSON_AddItemToObject(jResult, "phrases", cJSON_CreateString(phrases));

    char* json = cJSON_PrintUnformatted(jResult);

    switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_COMPILE_CONTEXT_RESPONSE);
    switch_channel_event_set_data(channel, event);
    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "cobalt");
    switch_event_add_body(event, "%s", json);
    switch_event_fire(&event);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "compile context response for cobalt speech: %s\n", json);	

    free(json);
    cJSON_Delete(jResult);

    free(originalPhrases);

    return response.has_context() ? c.data().c_str() : nullptr;
  }

}

class GStreamer {
public:
	GStreamer(
    switch_core_session_t *session, const char* hostport, const char* model, uint32_t channels, int interim) : 
      m_session(session), 
      m_writesDone(false), 
      m_connected(false), 
      m_interim(interim),
      m_hostport(hostport),
      m_model(model),
      m_channelCount(channels),
      m_audioBuffer(CHUNKSIZE, 15) {
  
    const char* var;
    char sessionId[256];
    switch_channel_t *channel = switch_core_session_get_channel(session);
    strncpy(m_sessionId, switch_core_session_get_uuid(session), 256);
	}

	~GStreamer() {
		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStreamer::~GStreamer - deleting channel and stub: %p\n", (void*)this);
	}

  std::shared_ptr<grpc::Channel> createGrpcConnection() {
    switch_channel_t *channel = switch_core_session_get_channel(m_session);

    std::shared_ptr<grpc::Channel> grpcChannel ;
    grpcChannel = grpc::CreateChannel(m_hostport, grpc::InsecureChannelCredentials());

    if (!grpcChannel) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p failed creating grpc channel\n", this);	
      throw std::runtime_error(std::string("Error creating grpc channel"));
    }

    m_stub = std::move(cobalt_asr::TranscribeService::NewStub(grpcChannel));
    return grpcChannel;
  }

  void connect() {
    const char* var;
    switch_channel_t *channel = switch_core_session_get_channel(m_session);

    assert(!m_connected);
    // Begin a stream.

    std::shared_ptr<grpc::Channel> grpcChannel = createGrpcConnection();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p creating streamer\n", this);	
  	m_streamer = m_stub->StreamingRecognize(&m_context);
    m_connected = true;

    /* set configuration parameters which are carried in the RecognitionInitMessage */
    auto config = m_request.mutable_config();
    auto format = config->mutable_audio_format_raw();
    config->set_model_id(m_model);
    format->set_encoding(cobalt_asr::AudioEncoding::AUDIO_ENCODING_SIGNED);
    format->set_bit_depth(16);
    format->set_sample_rate(8000);
    format->set_channels(m_channelCount);
    format->set_byte_order(cobalt_asr::ByteOrder::BYTE_ORDER_LITTLE_ENDIAN);

    // confusion network
    if (switch_true(switch_channel_get_variable(channel, "COBALT_ENABLE_CONFUSION_NETWORK"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p set_enable_confusion_network true\n", this);	
      config->set_enable_confusion_network(true);
    }
    // metadata
    if (var = switch_channel_get_variable(channel, "COBALT_METADATA")) {
      auto metadata = config->mutable_metadata();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p cobalt metadata %s\n", this, var);	
      metadata->set_custom_metadata(var);
    }

    // set_enable_word_details
    if (switch_true(switch_channel_get_variable(channel, "COBALT_ENABLE_WORD_TIME_OFFSETS"))) {
      config->set_enable_word_details(true);
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p enable word-level details\n", this);	
    }

    // compiled context data
    if (var = switch_channel_get_variable(channel, "COBALT_COMPILED_CONTEXT_DATA")) {
      auto data = base64_decode(var);
      config->mutable_context()->add_compiled()->set_data(data);
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p set compiled context %s\n", this, var);	
    }

    // read thread is waiting on this
    m_promise.set_value();

  	// Write the first request, containing the config only.
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p sending initial message\n", this);	
  	m_streamer->Write(m_request);
    m_request.clear_config();

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
  }

	bool write(void* data, uint32_t datalen) {
    if (!m_connected) {
      if (datalen % CHUNKSIZE == 0) {
        m_audioBuffer.add(data, datalen);
      }
      return true;
    }
    m_request.clear_audio();
    m_request.mutable_audio()->set_data(data, datalen);
    bool ok = m_streamer->Write(m_request);
    return ok;
  }

	uint32_t nextMessageSize(void) {
		uint32_t size = 0;
		m_streamer->NextMessageSize(&size);
		return size;
	}

	bool read(cobalt_asr::StreamingRecognizeResponse* response) {
		return m_streamer->Read(response);
	}

	grpc::Status finish() {
		return m_streamer->Finish();
	}

	void writesDone() {
    // grpc crashes if we call this twice on a stream
    if (!m_connected) {
      cancelConnect();
    }
    else if (!m_writesDone) {
      m_streamer->WritesDone();
      m_writesDone = true;
    }
	}

  bool waitForConnect() {
    std::shared_future<void> sf(m_promise.get_future());
    sf.wait();
    return m_connected;
  }

  void cancelConnect() {
    assert(!m_connected);
    m_promise.set_value();
  } 

  bool isConnected() {
    return m_connected;
  }

private:
	switch_core_session_t* m_session;
  grpc::ClientContext m_context;
	std::shared_ptr<grpc::Channel> m_channel;
	std::unique_ptr<cobalt_asr::TranscribeService::Stub> m_stub;
  cobalt_asr::StreamingRecognizeRequest m_request;
	std::unique_ptr< grpc::ClientReaderWriterInterface<cobalt_asr::StreamingRecognizeRequest, cobalt_asr::StreamingRecognizeResponse> > m_streamer;
  bool m_writesDone;
  bool m_connected;
  bool m_interim;
  std::string m_hostport;
  std::string m_model;
  std::promise<void> m_promise;
  SimpleBuffer m_audioBuffer;
  uint32_t m_channelCount;
  char m_sessionId[256];
};

static void *SWITCH_THREAD_FUNC grpc_read_thread(switch_thread_t *thread, void *obj) {
	struct cap_cb *cb = (struct cap_cb *) obj;
	GStreamer* streamer = (GStreamer *) cb->streamer;

  bool connected = streamer->waitForConnect();
  if (!connected) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "cobalt transcribe grpc read thread exiting since we didnt connect\n") ;
    return nullptr;
  }

  // Read responses.
  cobalt_asr::StreamingRecognizeResponse response;
  while (streamer->read(&response)) {  // Returns false when no more to read.
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (!session) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: session %s is gone!\n", cb->sessionId) ;
      return nullptr;
    }
    if (response.has_error()) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: error: %s\n", response.error().message().c_str()) ;
    }
    if (!response.has_result()) {
      switch_core_session_rwunlock(session);
      continue;
    }

    const auto& result = response.result();
    auto is_final = !result.is_partial();
    auto audio_channel = result.audio_channel();

    cJSON * jResult = cJSON_CreateObject();
    cJSON * jAlternatives = cJSON_CreateArray();
    cJSON_AddItemToObject(jResult, "is_final", cJSON_CreateBool(is_final));
    cJSON_AddItemToObject(jResult, "channel", cJSON_CreateNumber(audio_channel));
    cJSON_AddItemToObject(jResult, "alternatives", jAlternatives);

    for (int a = 0; a < result.alternatives_size(); ++a) {
      auto alternative = result.alternatives(a);
      cJSON* jAlt = cJSON_CreateObject();
      cJSON* jTranscriptRaw = cJSON_CreateString(alternative.transcript_raw().c_str());

      cJSON_AddItemToObject(jAlt, "confidence", cJSON_CreateNumber(alternative.confidence()));
      cJSON_AddItemToObject(jAlt, "transcript_formatted", cJSON_CreateString(alternative.transcript_formatted().c_str()));
      cJSON_AddItemToObject(jAlt, "transcript_raw", cJSON_CreateString(alternative.transcript_raw().c_str()));
      cJSON_AddItemToObject(jAlt, "start_time_ms", cJSON_CreateNumber(alternative.start_time_ms()));
      cJSON_AddItemToObject(jAlt, "duration_ms", cJSON_CreateNumber(alternative.duration_ms()));

      if (alternative.has_word_details()) {
        cJSON * jWords = cJSON_CreateArray();
        cJSON * jWordsRaw = cJSON_CreateArray();
        auto& word_details = alternative.word_details();
        for (int b = 0; b < word_details.formatted_size(); ++b) {
          cJSON* jWord = cJSON_CreateObject();
          auto& word_info = word_details.formatted(b);
          cJSON_AddItemToObject(jWord, "word", cJSON_CreateString(word_info.word().c_str()));
          cJSON_AddItemToObject(jWord, "confidence", cJSON_CreateNumber(word_info.confidence()));
          cJSON_AddItemToObject(jWord, "start_time_ms", cJSON_CreateNumber(word_info.start_time_ms()));
          cJSON_AddItemToObject(jWord, "duration_ms", cJSON_CreateNumber(word_info.duration_ms()));

          cJSON_AddItemToArray(jWords, jWord);
        }
        cJSON_AddItemToObject(jAlt, "formatted_words", jWords);

        for (int c = 0; c < word_details.raw_size(); ++c) {
          cJSON* jWord = cJSON_CreateObject();
          auto& word_info = word_details.raw(c);
          cJSON_AddItemToObject(jWord, "word", cJSON_CreateString(word_info.word().c_str()));
          cJSON_AddItemToObject(jWord, "confidence", cJSON_CreateNumber(word_info.confidence()));
          cJSON_AddItemToObject(jWord, "start_time_ms", cJSON_CreateNumber(word_info.start_time_ms()));
          cJSON_AddItemToObject(jWord, "duration_ms", cJSON_CreateNumber(word_info.duration_ms()));

          cJSON_AddItemToArray(jWordsRaw, jWord);
        }
        cJSON_AddItemToObject(jAlt, "raw_words", jWordsRaw);

      }
      cJSON_AddItemToArray(jAlternatives, jAlt);
    }
    char* json = cJSON_PrintUnformatted(jResult);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "cobalt models: %s\n", json) ;
    cb->responseHandler(session, (const char *) json, cb->bugname, NULL);
    free(json);

    cJSON_Delete(jResult);
  
    switch_core_session_rwunlock(session);
  }

  {
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (session) {
      grpc::Status status = streamer->finish();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: finish() status %s (%d)\n", status.error_message().c_str(), status.error_code()) ;
      switch_core_session_rwunlock(session);
    }
  }

  return nullptr;
}

extern "C" {

    switch_status_t cobalt_speech_get_version(switch_core_session_t *session, char* hostport) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
    	switch_event_t *event;

      grpc::ClientContext context;
      std::shared_ptr<grpc::Channel> grpcChannel ;
      grpcChannel = grpc::CreateChannel(hostport, grpc::InsecureChannelCredentials());

      if (!grpcChannel) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "failed creating grpc channel\n");	
        return SWITCH_STATUS_FALSE;
      }

      std::unique_ptr<cobalt_asr::TranscribeService::Stub> stub = std::move(cobalt_asr::TranscribeService::NewStub(grpcChannel));
      
      cobalt_asr::VersionResponse response;
      stub->Version(&context, cobalt_asr::VersionRequest(), &response);

      cJSON * jResult = cJSON_CreateObject();
      cJSON_AddItemToObject(jResult, "version", cJSON_CreateString(response.version().c_str()));

      char* json = cJSON_PrintUnformatted(jResult);

      switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_VERSION_RESPONSE);
      switch_channel_event_set_data(channel, event);
      switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "cobalt");
      switch_event_add_body(event, "%s", json);
    	switch_event_fire(&event);
      switch_event_destroy(&event);

      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "retrieved version for cobalt speech: %s\n", json);	

      free(json);
      cJSON_Delete(jResult);
      

      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t cobalt_speech_compile_context(switch_core_session_t *session, char* hostport, char* model, char* token, char* phrases) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
    	switch_event_t *event;

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "compile context, model: %s, token: %s, phrases: %s\n", model, token, phrases);

      return compile_context_phrases(session, hostport, model, token, phrases) != nullptr ?
        SWITCH_STATUS_SUCCESS :
        SWITCH_STATUS_FALSE;
    }


    switch_status_t cobalt_speech_list_models(switch_core_session_t *session, char* hostport) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
    	switch_event_t *event;

      grpc::ClientContext context;
      std::shared_ptr<grpc::Channel> grpcChannel ;
      grpcChannel = grpc::CreateChannel(hostport, grpc::InsecureChannelCredentials());

      if (!grpcChannel) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "failed creating grpc channel\n");	
        return SWITCH_STATUS_FALSE;
      }

      std::unique_ptr<cobalt_asr::TranscribeService::Stub> stub = std::move(cobalt_asr::TranscribeService::NewStub(grpcChannel));
      
      cobalt_asr::ListModelsResponse response;
      stub->ListModels(&context, cobalt_asr::ListModelsRequest(), &response);
      cJSON * jModels = cJSON_CreateArray();
      for (int i = 0; i < response.models_size(); i++) {
        auto model = response.models(i);
        cJSON* jModel = cJSON_CreateObject();
        cJSON * jAttributes = cJSON_CreateArray();

        cJSON_AddItemToArray(jModels, jModel);
        cJSON_AddItemToObject(jModel, "attributes", jAttributes);
        cJSON_AddItemToObject(jModel, "id", cJSON_CreateString(model.id().c_str()));
        cJSON_AddItemToObject(jModel, "name", cJSON_CreateString(model.name().c_str()));

        if (model.has_attributes()) {
          auto& attributes = model.attributes();
          cJSON* jAttr = cJSON_CreateObject();
          cJSON_AddItemToArray(jAttributes, jAttr);

          /* supported sample rates */
          cJSON * jSupportedSampleRates = cJSON_CreateArray();
          cJSON_AddItemToObject(jAttr, "supported_sample_rates", jSupportedSampleRates);
          for (int j = 0; j < attributes.supported_sample_rates_size(); j++) {
            cJSON_AddItemToObject(jSupportedSampleRates, "supported_sample_rates", cJSON_CreateNumber(attributes.supported_sample_rates(j)));
          }

          /* sample rate */
          cJSON_AddItemToObject(jAttr, "sample_rate", cJSON_CreateNumber(attributes.sample_rate()));

          /* context info */
          auto& context_info = attributes.context_info();
          cJSON * jContextInfo = cJSON_CreateObject();
          cJSON* jAllowedContextTokens = cJSON_CreateArray();
          cJSON_AddItemToObject(jAttr, "context_info", jContextInfo);
          cJSON_AddItemToObject(jContextInfo, "allowed_context_tokens", jAllowedContextTokens);
          for (int j = 0; j < context_info.allowed_context_tokens_size(); j++) {
            cJSON_AddItemToArray(jAllowedContextTokens, cJSON_CreateString(context_info.allowed_context_tokens(j).c_str()));
          }

          cJSON_AddBoolToObject(jContextInfo, "supports_context", context_info.supports_context());
        }
      }

      char* json = cJSON_PrintUnformatted(jModels);

      switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_MODEL_LIST_RESPONSE);
      switch_channel_event_set_data(channel, event);
      switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "cobalt");
      switch_event_add_body(event, "%s", json);
    	switch_event_fire(&event);
      switch_event_destroy(&event);

      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "retrieved %d models for cobalt speech: %s\n", response.models_size(), json);	

      free(json);
      cJSON_Delete(jModels);
      
      return SWITCH_STATUS_SUCCESS;
    }


    switch_status_t cobalt_speech_init() {
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t cobalt_speech_cleanup() {
      return SWITCH_STATUS_SUCCESS;
    }
    switch_status_t cobalt_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, char* hostport,
      uint32_t samples_per_second, uint32_t channels, char* model, int interim, char *bugname, void **ppUserData) {

      switch_channel_t *channel = switch_core_session_get_channel(session);
      auto read_codec = switch_core_session_get_read_codec(session);
      uint32_t sampleRate = read_codec->implementation->actual_samples_per_second;
      struct cap_cb *cb;
      int err;

      cb =(struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
      strncpy(cb->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
      strncpy(cb->bugname, bugname, MAX_BUG_LEN);
      cb->end_of_utterance = 0;
      
      switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
      if (sampleRate != 8000) {
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_init:  initializing resampler\n");
          cb->resampler = speex_resampler_init(channels, sampleRate, 8000, SWITCH_RESAMPLE_QUALITY, &err);
        if (0 != err) {
           switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n",
                                 switch_channel_get_name(channel), speex_resampler_strerror(err));
          return SWITCH_STATUS_FALSE;
        }
      } else {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s: no resampling needed for this call (bug: %s)\n", switch_channel_get_name(channel), bugname);
      }
      cb->responseHandler = responseHandler;

      // allocate vad if we are delaying connecting to the recognizer until we detect speech
      if (switch_channel_var_true(channel, "START_RECOGNIZING_ON_VAD")) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_init:  initializing vad\n");
        cb->vad = switch_vad_init(sampleRate, channels);
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
          if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_VOICE_MS")) {
            voice_ms = atoi(var);
          }
          switch_vad_set_mode(cb->vad, mode);
          switch_vad_set_param(cb->vad, "silence_ms", silence_ms);
          switch_vad_set_param(cb->vad, "voice_ms", voice_ms);
          switch_vad_set_param(cb->vad, "debug", debug);
        }
      }

      GStreamer *streamer = NULL;
      try {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_init:  allocating streamer\n");
        streamer = new GStreamer(session, hostport, model, channels, interim);
        cb->streamer = streamer;
      } catch (std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
          switch_channel_get_name(channel), e.what());
        return SWITCH_STATUS_FALSE;
      }

      if (!cb->vad) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_init:  no vad so connecting to cobalt immediately\n");
        streamer->connect();
      }

      // create the read thread
      switch_threadattr_t *thd_attr = NULL;
      switch_memory_pool_t *pool = switch_core_session_get_pool(session);

      switch_threadattr_create(&thd_attr, pool);
      switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
      switch_thread_create(&cb->thread, thd_attr, grpc_read_thread, cb, pool);

      *ppUserData = cb;
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t cobalt_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
      switch_channel_t *channel = switch_core_session_get_channel(session);

      if (bug) {
        struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
        switch_mutex_lock(cb->mutex);

        if (!switch_channel_get_private(channel, cb->bugname)) {
          // race condition
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug %s is not attached (race).\n", switch_channel_get_name(channel), cb->bugname);
          switch_mutex_unlock(cb->mutex);
          return SWITCH_STATUS_FALSE;
        }
        switch_channel_set_private(channel, cb->bugname, NULL);

        // close connection and get final responses
        GStreamer* streamer = (GStreamer *) cb->streamer;

        if (streamer) {
          streamer->writesDone();

          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_cleanup: GStreamer (%p) waiting for read thread to complete\n", (void*)streamer);
          switch_status_t st;
          switch_thread_join(&st, cb->thread);
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_cleanup:  GStreamer (%p) read thread completed\n", (void*)streamer);

          delete streamer;
          cb->streamer = NULL;
        }

        if (cb->resampler) {
          speex_resampler_destroy(cb->resampler);
        }
        if (cb->vad) {
          switch_vad_destroy(&cb->vad);
          cb->vad = nullptr;
        }
        if (!channelIsClosing) {
          switch_core_media_bug_remove(session, &bug);
        }

			  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "cobalt_speech_session_cleanup: Closed stream\n");

			  switch_mutex_unlock(cb->mutex);


			  return SWITCH_STATUS_SUCCESS;
      }

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
      return SWITCH_STATUS_FALSE;
    }

    switch_bool_t cobalt_speech_frame(switch_media_bug_t *bug, void* user_data) {
    	switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    	struct cap_cb *cb = (struct cap_cb *) user_data;
		  if (cb->streamer && !cb->end_of_utterance) {
        GStreamer* streamer = (GStreamer *) cb->streamer;
        uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = {};
        frame.data = data;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

        if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
          while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
            if (frame.datalen) {
              if (cb->vad && !streamer->isConnected()) {
                switch_vad_state_t state = switch_vad_process(cb->vad, (int16_t*) frame.data, frame.samples);
                if (state == SWITCH_VAD_STATE_START_TALKING) {
                  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "detected speech, connect to google speech now\n");
                  streamer->connect();
                  cb->responseHandler(session, "vad_detected", cb->bugname, NULL);
                }
              }

              if (cb->resampler) {
                spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
                spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
                spx_uint32_t in_len = frame.samples;
                size_t written;

                speex_resampler_process_interleaved_int(cb->resampler,
                  (const spx_int16_t *) frame.data,
                  (spx_uint32_t *) &in_len,
                  &out[0],
                  &out_len);
                streamer->write( &out[0], sizeof(spx_int16_t) * out_len);
              }
              else {
                streamer->write( frame.data, sizeof(spx_int16_t) * frame.samples);
              }
            }
          }
          switch_mutex_unlock(cb->mutex);
        }
      }
      return SWITCH_TRUE;
    }
}
