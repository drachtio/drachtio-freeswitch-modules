#include <cstdlib>
#include <algorithm>
#include <future>
#include <random>
#include <string>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "nuance/rpc/status.grpc.pb.h"
#include "nuance/rpc/status_code.grpc.pb.h"
#include "nuance/rpc/error_details.grpc.pb.h"
#include "nuance/asr/v1/result.grpc.pb.h"
#include "nuance/asr/v1/resource.grpc.pb.h"
#include "nuance/asr/v1/recognizer.grpc.pb.h"

#include "mod_nuance_transcribe.h"
#include "simple_buffer.h"

using nuance::asr::v1::Recognizer;
using nuance::asr::v1::RecognitionRequest;
using nuance::asr::v1::RecognitionResponse;
using nuance::asr::v1::RecognitionInitMessage;
using nuance::asr::v1::EnumUtteranceDetectionMode;
using nuance::asr::v1::EnumResultType;
using nuance::asr::v1::RecognitionResource;
using nuance::asr::v1::EnumWeight;
using nuance::asr::v1::EnumResourceReuse;
using nuance::asr::v1::Status;
using nuance::asr::v1::Result;
using nuance::asr::v1::EnumResultType;
using nuance::asr::v1::Hypothesis;


#define CHUNKSIZE (320)

namespace {
  int case_insensitive_match(std::string s1, std::string s2) {
   std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
   std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
   if(s1.compare(s2) == 0)
      return 1; //The strings are same
   return 0; //not matched
  }
}

class GStreamer {
public:
	GStreamer(
    switch_core_session_t *session, uint32_t channels, char* lang, int interim) : 
      m_session(session), 
      m_writesDone(false), 
      m_connected(false), 
      m_language(lang),
      m_interim(interim),
      m_audioBuffer(CHUNKSIZE, 15) {
  
    const char* var;
    char sessionId[256];
    switch_channel_t *channel = switch_core_session_get_channel(session);
    strncpy(m_sessionId, switch_core_session_get_uuid(session), 256);

	}

	~GStreamer() {
		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStreamer::~GStreamer - deleting channel and stub: %p\n", (void*)this);
	}

  std::shared_ptr<grpc::Channel> createUniqueChannel(const std::string& target) {
    grpc::ChannelArguments channelArgs;

    // Generate a unique value for the custom argument
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000000);
    std::string uniqueArgValue = "unique_value_" + std::to_string(dis(gen));

    // Set a custom channel argument with the unique value
    channelArgs.SetString("unique_custom_arg", uniqueArgValue);

    return grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), channelArgs);
  }

  void createInitMessage() {
    switch_channel_t *channel = switch_core_session_get_channel(m_session);

    std::shared_ptr<grpc::Channel> grpcChannel ;
    const char* var = switch_channel_get_variable(channel, "NUANCE_KRYPTON_ENDPOINT");
    if (var) {
      //auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
      //grpcChannel = grpc::CreateChannel(var, channelCreds);
      // grpcChannel = grpc::CreateChannel(var, grpc::InsecureChannelCredentials());
      // Hosted Krypton endpoint does not allow different grpc thread re-use same tcp connection.
      // Use custom grpc channel to force grpc c/c++ lib open new tcp connection.
      grpcChannel = createUniqueChannel(var);
    }
    else {
      var = switch_channel_get_variable(channel, "NUANCE_ACCESS_TOKEN");
      assert(var); // we should not get here unless we have a valid access token

      auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
      auto callCreds = grpc::AccessTokenCredentials(var);
      auto creds = grpc::CompositeChannelCredentials(channelCreds, callCreds);
      grpcChannel = grpc::CreateChannel("asr.api.nuance.com:443", creds);
    }

    if (!grpcChannel) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p failed creating grpc channel to %s\n", this, var);	
      throw std::runtime_error(std::string("Error creating grpc channel to ") + var);
    }

    m_stub = std::move(Recognizer::NewStub(grpcChannel));

    /* set configuration parameters which are carried in the RecognitionInitMessage */
    auto msg = m_request.mutable_recognition_init_message();
    msg->set_user_id(m_sessionId);
    msg->mutable_parameters()->set_language(m_language);
    msg->mutable_parameters()->mutable_audio_format()->mutable_pcm()->set_sample_rate_hz(8000);


    if (var = switch_channel_get_variable(channel, "NUANCE_TOPIC")) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting topic %s\n", this, var);	
      msg->mutable_parameters()->set_topic(var);
    }
    if (var = switch_channel_get_variable(channel, "NUANCE_RESULT_TYPE")) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting result type %s\n", this, var);	
      if (0 == strcasecmp(var, "final")) {
        msg->mutable_parameters()->set_result_type(EnumResultType::FINAL);
      } else if (0 == strcasecmp(var, "partial")) {
        msg->mutable_parameters()->set_result_type(EnumResultType::PARTIAL);
      } else if (0 == strcasecmp(var, "immutable_partial")) {
        msg->mutable_parameters()->set_result_type(EnumResultType::IMMUTABLE_PARTIAL);
      } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p invalid result type %s\n", this, var);	
      }
    }
    if (var = switch_channel_get_variable(channel, "NUANCE_UTTERANCE_DETECTION_MODE")) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting detection mode %s\n", this, var);	
      if (0 == strcasecmp(var, "single")) {
        msg->mutable_parameters()->set_utterance_detection_mode(EnumUtteranceDetectionMode::SINGLE);
      } else if (0 == strcasecmp(var, "multiple")) {
        msg->mutable_parameters()->set_utterance_detection_mode(EnumUtteranceDetectionMode::MULTIPLE);
      } else if (0 == strcasecmp(var, "disabled")) {
        msg->mutable_parameters()->set_utterance_detection_mode(EnumUtteranceDetectionMode::DISABLED);
      } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p invalid detection mode %s\n", this, var);	
      }
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_PUNCTUATION"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting auto punctuate to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_auto_punctuate(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting auto punctuate to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_auto_punctuate(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_FILTER_PROFANITY"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting filter profanity to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_filter_profanity(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting filter profanity to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_filter_profanity(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_INCLUDE_TOKENIZATION"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting include tokenization to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_include_tokenization(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting include tokenization to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_include_tokenization(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_DISCARD_SPEAKER_ADAPTATION"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting discard speaker adaptation to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_discard_speaker_adaptation(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting discard speaker adaptation to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_discard_speaker_adaptation(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_SUPPRESS_CALL_RECORDING"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting suppress call recording to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_suppress_call_recording(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting suppress call recording to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_suppress_call_recording(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_MASK_LOAD_FAILURES"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting mask load failures to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_mask_load_failures(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting mask load failures to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_mask_load_failures(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_SUPPRESS_INITIAL_CAPITALIZATION"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting suppress initial capitalization to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_suppress_initial_capitalization(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting suppress initial capitalization to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_suppress_initial_capitalization(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_ALLOW_ZERO_BASE_LM_WEIGHT"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting allow zero base lm weight to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_allow_zero_base_lm_weight(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting allow zero base lm weight to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_allow_zero_base_lm_weight(false);
    }
    if (switch_true(switch_channel_get_variable(channel, "NUANCE_FILTER_WAKEUP_WORD"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting filter wakeup word to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_filter_wakeup_word(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting filter wakeup word to false\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_filter_wakeup_word(false);
    }

    if (switch_true(switch_channel_get_variable(channel, "NUANCE_STALL_TIMERS"))) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting stall timers to true\n", this);	
      msg->mutable_parameters()->mutable_recognition_flags()->set_stall_timers(true);
    }
    else {
      msg->mutable_parameters()->mutable_recognition_flags()->set_stall_timers(false);
    }

    if (var = switch_channel_get_variable(channel, "NUANCE_NO_INPUT_TIMEOUT_MS")) {
      int ms = atoi(var);
      if (ms > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting no input timeout to %dms\n", this, ms);	
        msg->mutable_parameters()->set_no_input_timeout_ms(ms);
      }
    }
    if (var = switch_channel_get_variable(channel, "NUANCE_UTTERANCE_END_SILENCE_MS")) {
      int ms = atoi(var);
      if (ms > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting utterance end silence timeout to %dms\n", this, ms);	
        msg->mutable_parameters()->set_utterance_end_silence_ms(ms);
      }
    }
    if (var = switch_channel_get_variable(channel, "NUANCE_RECOGNITION_TIMEOUT_MS")) {
      int ms = atoi(var);
      if (ms > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting recognition timeout to %dms\n", this, ms);	
        msg->mutable_parameters()->set_recognition_timeout_ms(ms);
      }
    }
    if (var = switch_channel_get_variable(channel, "NUANCE_MAX_HYPOTHESES")) {
      int count = atoi(var);
      if (count > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting max hypotheses to %d\n", this, count);	
        msg->mutable_parameters()->set_max_hypotheses(count);
      }
    }
    if (var = switch_channel_get_variable(channel, "NUANCE_SPEECH_DOMAIN")) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting speech domain to %s\n", this, var);	
      msg->mutable_parameters()->set_speech_domain(var);
    }

    msg->clear_resources();
    if (var = switch_channel_get_variable(channel, "NUANCE_RESOURCES")) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p setting resources %s\n", this, var);	
      cJSON* json = cJSON_Parse(var);
      if (json) {
        if (cJSON_IsArray(json)) {
          int count = cJSON_GetArraySize(json);
          for (int i = 0; i < count; i++) {
            cJSON* obj = cJSON_GetArrayItem(json, i);
            if (obj && cJSON_IsObject(obj)) {
              bool added = false;

              /* inline wordset */
              cJSON* cInlineWordSet = cJSON_GetObjectItem(obj, "inlineWordset");
              if (cInlineWordSet && cJSON_IsString(cInlineWordSet)) {
                const char* inlineWordSet = cJSON_GetStringValue(cInlineWordSet);
                cJSON* test = cJSON_Parse(inlineWordSet);
                if (test) {
                  added = true;
                  auto resource = msg->add_resources();
                  resource->set_inline_wordset(inlineWordSet);
                  cJSON_Delete(test);
                  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p adding inlineWordset: %s\n", this, inlineWordSet);	
                }
                else {
                  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p an inline wordset must be valid JSON: '%s' is not.\n", this, inlineWordSet);
                }
              }

              /* builtins */
              cJSON* cBuiltin = cJSON_GetObjectItem(obj, "builtin");
              if (cBuiltin && cJSON_IsString(cBuiltin)) {
                const char* builtin = cJSON_GetStringValue(cBuiltin);
                added = true;
                auto resource = msg->add_resources();
                resource->set_builtin(builtin);
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p adding builtin: %s\n", this, builtin);	
              }

              if (added) {
                auto idx = msg->resources_size() - 1;
                auto resource = msg->mutable_resources(idx);
                cJSON* cReuse = cJSON_GetObjectItem(obj, "reuse");
                if (cReuse && cJSON_IsString(cReuse)) {
                  const char* reuse = cJSON_GetStringValue(cReuse);
                  if (0 == strcmp(reuse, "low_reuse")) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p reuse to: %s\n", this, reuse);	
                    resource->set_reuse(EnumResourceReuse::LOW_REUSE);
                  }
                  else if (0 == strcmp(reuse, "high_reuse")) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p reuse to: %s\n", this, reuse);	
                    resource->set_reuse(EnumResourceReuse::HIGH_REUSE);
                  }
                }

                cJSON* cWeightName = cJSON_GetObjectItem(obj, "weightName");
                if (cWeightName && cJSON_IsString(cWeightName)) {
                  const char* weightName = cJSON_GetStringValue(cWeightName);
                  if (0 == strcmp(weightName, "defaultWeight")) {
                    resource->set_weight_enum(EnumWeight::DEFAULT_WEIGHT);
                  }
                  else if (0 == strcmp(weightName, "lowest")) {
                    resource->set_weight_enum(EnumWeight::LOWEST);                  
                  }
                  else if (0 == strcmp(weightName, "low")) {
                    resource->set_weight_enum(EnumWeight::LOW);
                  }
                  else if (0 == strcmp(weightName, "medium")) {
                    resource->set_weight_enum(EnumWeight::MEDIUM);
                    
                  }
                  else if (0 == strcmp(weightName, "high")) {
                    resource->set_weight_enum(EnumWeight::HIGH);
                    
                  }
                  else if (0 == strcmp(weightName, "highest")) {
                    resource->set_weight_enum(EnumWeight::HIGHEST);
                    
                  }
                  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p weightName to: %s\n", this, weightName);	
                }

                cJSON* cWeightValue = cJSON_GetObjectItem(obj, "weightValue");
                if (cWeightValue && cJSON_IsNumber(cWeightValue)) {
                  double weightValue = cWeightValue->valuedouble;
                  resource->set_weight_value(weightValue);
                  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p weightName to: %.2f\n", this, weightValue);	
                }
              }
            }
          }
        }
        else {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p resources must be an array: %s\n", this, var);
        }
        cJSON_Delete(json);
      }
      else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p invalid resources json: %s\n", this, var);
      }
    }    
  }

  void connect() {
    assert(!m_connected);
    // Begin a stream.

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p creating initial nuance message\n", this);	
    createInitMessage();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p creating streamer\n", this);	
  	m_streamer = m_stub->Recognize(&m_context);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p connected to nuance\n", this);	
    m_connected = true;

    // read thread is waiting on this
    m_promise.set_value();

  	// Write the first request, containing the config only.
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p sending initial message\n", this);	
  	m_streamer->Write(m_request);
    //m_request.clear_recognition_init_message();

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
    m_request.set_audio(data, datalen);
    bool ok = m_streamer->Write(m_request);
    return ok;
  }

	uint32_t nextMessageSize(void) {
		uint32_t size = 0;
		m_streamer->NextMessageSize(&size);
		return size;
	}

	bool read(RecognitionResponse* response) {
		return m_streamer->Read(response);
	}

	grpc::Status finish() {
		return m_streamer->Finish();
	}

  void startTimers() {
    RecognitionRequest request;
    auto msg = request.mutable_control_message()->mutable_start_timers_message();
    m_streamer->Write(request);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p sent start timers control message\n", this);	
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
	std::unique_ptr<Recognizer::Stub> m_stub;
  RecognitionInitMessage m_msg;
  RecognitionRequest m_request;
	std::unique_ptr< grpc::ClientReaderWriterInterface<RecognitionRequest, RecognitionResponse> > m_streamer;
  bool m_writesDone;
  bool m_connected;
  bool m_interim;
  std::string m_language;
  std::promise<void> m_promise;
  SimpleBuffer m_audioBuffer;
  char m_sessionId[256];
};

static void *SWITCH_THREAD_FUNC grpc_read_thread(switch_thread_t *thread, void *obj) {
  static int count;
	struct cap_cb *cb = (struct cap_cb *) obj;
	GStreamer* streamer = (GStreamer *) cb->streamer;

  bool connected = streamer->waitForConnect();
  if (!connected) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "nuance transcribe grpc read thread exiting since we didnt connect\n") ;
    return nullptr;
  }

  // Read responses.
  RecognitionResponse response;
  while (streamer->read(&response)) {  // Returns false when no more to read.
    count++;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "response counter:  %d\n",count) ;

    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (!session) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: session %s is gone!\n", cb->sessionId) ;
      return nullptr;
    }

    // 3 types of responses: status, start of speech, result
    bool processed = false;
    if (response.has_status()) {
      processed = true;
      Status status = response.status();
      uint32_t code = status.code();
      if (code <= 200) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p got status code %d\n", streamer, code);
        if (code == 200) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer %p transcription complete\n", streamer);
          cb->responseHandler(session, "end_of_transcription", cb->bugname, NULL);
        }
      }
      else {
        auto message = status.message();
        auto details = status.details();
        cJSON* jError = cJSON_CreateObject();
        cJSON_AddStringToObject(jError, "type", "error");
        cJSON_AddNumberToObject(jError, "code", code);
        cJSON_AddStringToObject(jError, "error", status.message().c_str());
        cJSON_AddStringToObject(jError, "details", status.details().c_str());        
        char* error = cJSON_PrintUnformatted(jError);

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer %p got non-success code %d - %s : %s\n", streamer, code, message.c_str(), details.c_str());
        cb->responseHandler(session, "error", cb->bugname, error);

        free(error);
        cJSON_Delete(jError);
      }
    }
    if (response.has_start_of_speech()) {
        processed = true;
        auto start_of_speech = response.start_of_speech();
        auto first_audio_to_start_of_speech_ms = start_of_speech.first_audio_to_start_of_speech_ms();
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer %p got start of speech %d\n", streamer, first_audio_to_start_of_speech_ms);	
        cb->responseHandler(session, "start_of_speech", cb->bugname, NULL);
    }
    if (response.has_result()){
      processed = true;
      const Result& result = response.result();
      EnumResultType type = result.result_type();
      bool is_final = type == EnumResultType::FINAL;
      int nAlternatives = result.hypotheses_size();

      cJSON * jResult = cJSON_CreateObject();
      cJSON * jAlternatives = cJSON_CreateArray();
      cJSON * jIsFinal = cJSON_CreateBool(is_final);

      cJSON_AddItemToObject(jResult, "is_final", jIsFinal);
      cJSON_AddItemToObject(jResult, "alternatives", jAlternatives);

      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p got a %s result with %d hypotheses\n", streamer, is_final ? "final" : "interim", nAlternatives);	
      for (int i = 0; i < nAlternatives; i++) {
        auto hypothesis = result.hypotheses(i);
        auto formatted_text = hypothesis.formatted_text();
        auto minimally_formatted_text = hypothesis.minimally_formatted_text();
        auto encrypted_tokenization = hypothesis.encrypted_tokenization();
        auto average_confidence = hypothesis.average_confidence();
        auto rejected = hypothesis.rejected();
        auto grammar_id = hypothesis.grammar_id();

        cJSON* jAlt = cJSON_CreateObject();
        cJSON* jConfidence = cJSON_CreateNumber(hypothesis.confidence());
        cJSON* jAverageConfidence = cJSON_CreateNumber(hypothesis.average_confidence());
        cJSON* jTranscript = cJSON_CreateString(hypothesis.formatted_text().c_str());
        cJSON* jMinimallyFormattedText = cJSON_CreateString(hypothesis.minimally_formatted_text().c_str());
        cJSON* jEncryptedTokenization = cJSON_CreateString(hypothesis.encrypted_tokenization().c_str());
        if (hypothesis.has_grammar_id()) {
          cJSON* jGrammarId = cJSON_CreateString(hypothesis.grammar_id().c_str());
          cJSON_AddItemToObject(jAlt, "grammar_id", jGrammarId);
        }
        cJSON* jRejected = cJSON_CreateBool(hypothesis.rejected());
        if (hypothesis.has_detected_wakeup_word()) {
          cJSON* jDetectedWakeupWord = cJSON_CreateString(hypothesis.detected_wakeup_word().c_str());
          cJSON_AddItemToObject(jAlt, "detectedWakeupWord", jDetectedWakeupWord);
        }

        cJSON_AddItemToObject(jAlt, "confidence", jConfidence);
        cJSON_AddItemToObject(jAlt, "averageConfidence", jAverageConfidence);
        cJSON_AddItemToObject(jAlt, "transcript", jTranscript);
        cJSON_AddItemToObject(jAlt, "rejected", jRejected);
        cJSON_AddItemToObject(jAlt, "minimallyFormattedText", jMinimallyFormattedText);
        if (!encrypted_tokenization.empty()) cJSON_AddItemToObject(jAlt, "encryptedTokenization", jEncryptedTokenization);
        cJSON_AddItemToArray(jAlternatives, jAlt);
      }
      char* json = cJSON_PrintUnformatted(jResult);
      cb->responseHandler(session, (const char *) json, cb->bugname, NULL);
      free(json);

      cJSON_Delete(jResult);
    }
    switch_core_session_rwunlock(session);
  }
  return nullptr;
}
extern "C" {

    switch_status_t nuance_speech_init() {
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t nuance_speech_cleanup() {
      return SWITCH_STATUS_SUCCESS;
    }
    switch_status_t nuance_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
      uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, void **ppUserData) {

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
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_init:  initializing resampler\n");
          cb->resampler = speex_resampler_init(channels, sampleRate, 8000, SWITCH_RESAMPLE_QUALITY, &err);
        if (0 != err) {
           switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n",
                                 switch_channel_get_name(channel), speex_resampler_strerror(err));
          return SWITCH_STATUS_FALSE;
        }
      } else {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s: no resampling needed for this call\n", switch_channel_get_name(channel));
      }
      cb->responseHandler = responseHandler;

      // allocate vad if we are delaying connecting to the recognizer until we detect speech
      if (switch_channel_var_true(channel, "START_RECOGNIZING_ON_VAD")) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_init:  initializing vad\n");
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
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_init:  allocating streamer\n");
        streamer = new GStreamer(session, channels, lang, interim);
        cb->streamer = streamer;
      } catch (std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
          switch_channel_get_name(channel), e.what());
        return SWITCH_STATUS_FALSE;
      }

      if (!cb->vad) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_init:  no vad so connecting to nuance immediately\n");
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

    switch_status_t nuance_speech_session_start_timers(switch_core_session_t *session, switch_media_bug_t *bug) {
      if (bug) {
        struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
        switch_mutex_lock(cb->mutex);
        GStreamer* streamer = (GStreamer *) cb->streamer;

        if (streamer) streamer->startTimers();
        switch_mutex_unlock(cb->mutex);
        return SWITCH_STATUS_SUCCESS;
      }
      return SWITCH_STATUS_FALSE;
    }

    switch_status_t nuance_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
      switch_channel_t *channel = switch_core_session_get_channel(session);

      if (bug) {
        struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
        switch_mutex_lock(cb->mutex);

        if (!switch_channel_get_private(channel, cb->bugname)) {
          // race condition
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached (race).\n", switch_channel_get_name(channel));
          switch_mutex_unlock(cb->mutex);
          return SWITCH_STATUS_FALSE;
        }
        switch_channel_set_private(channel, cb->bugname, NULL);

        // close connection and get final responses
        GStreamer* streamer = (GStreamer *) cb->streamer;

        if (streamer) {
          streamer->writesDone();

          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_cleanup: GStreamer (%p) waiting for read thread to complete\n", (void*)streamer);
          switch_status_t st;
          switch_thread_join(&st, cb->thread);
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_cleanup:  GStreamer (%p) read thread completed\n", (void*)streamer);

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

			  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "nuance_speech_session_cleanup: Closed stream\n");

			  switch_mutex_unlock(cb->mutex);


			  return SWITCH_STATUS_SUCCESS;
      }

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
      return SWITCH_STATUS_FALSE;
    }

    switch_bool_t nuance_speech_frame(switch_media_bug_t *bug, void* user_data) {
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
