#include <cstdlib>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "google/cloud/speech/v1/cloud_speech.grpc.pb.h"

#include "mod_google_transcribe.h"

#define BUFFER_SECS (3)

using google::cloud::speech::v1::RecognitionConfig;
using google::cloud::speech::v1::Speech;
using google::cloud::speech::v1::SpeechContext;
using google::cloud::speech::v1::StreamingRecognizeRequest;
using google::cloud::speech::v1::StreamingRecognizeResponse;
using google::cloud::speech::v1::StreamingRecognizeResponse_SpeechEventType_END_OF_SINGLE_UTTERANCE;
using google::rpc::Status;

class GStreamer;

class GStreamer {
public:
	GStreamer(switch_core_session_t *session, u_int16_t channels, char* lang, int interim, int single_utterence, int separate_recognition,
		int max_alternatives, int profinity_filter, int word_time_offset, int punctuation, char* model, int enhanced, 
		char* hints) : 

    m_session(session), m_writesDone(false) {
    const char* var;
    switch_channel_t *channel = switch_core_session_get_channel(session);

		if (var = switch_channel_get_variable(channel, "GOOGLE_APPLICATION_CREDENTIALS")) {
			auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
			auto callCreds = grpc::ServiceAccountJWTAccessCredentials(var);
			auto creds = grpc::CompositeChannelCredentials(channelCreds, callCreds);
			m_channel = grpc::CreateChannel("speech.googleapis.com", creds);
		}
		else {
			auto creds = grpc::GoogleDefaultCredentials();
			m_channel = grpc::CreateChannel("speech.googleapis.com", creds);
		}

  	m_stub = Speech::NewStub(m_channel);
  		
		auto* streaming_config = m_request.mutable_streaming_config();
		RecognitionConfig* config = streaming_config->mutable_config();

    streaming_config->set_interim_results(interim);
    if (single_utterence == 1) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_single_utterance\n");
      streaming_config->set_single_utterance(true);
    }

		config->set_language_code(lang);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "transcribe language %s \n", lang);
    
    int sample_rate = atoi(switch_channel_get_variable(channel, "read_rate"));
  	config->set_sample_rate_hertz(sample_rate);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "sample rate %d \n", sample_rate);

		config->set_encoding(RecognitionConfig::LINEAR16);

    // the rest of config comes from channel vars

    // number of channels in the audio stream (default: 1)
    if (channels > 1) {
      config->set_audio_channel_count(channels);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "audio_channel_count %d\n", channels);

      // transcribe each separately?
      if (separate_recognition == 1) {
        config->set_enable_separate_recognition_per_channel(true);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_separate_recognition_per_channel on\n");
      }
    }

    // max alternatives
    if (max_alternatives > 1) {
      config->set_max_alternatives(max_alternatives);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "max_alternatives %d\n", max_alternatives);
    }

    // profanity filter
    if (profinity_filter == 1) {
      config->set_profanity_filter(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "profanity_filter\n");
    }

    // enable word offsets
    if (word_time_offset == 1) {
      config->set_enable_word_time_offsets(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_word_time_offsets\n");
    }

    // enable automatic punctuation
    if (punctuation == 1) {
      config->set_enable_automatic_punctuation(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_automatic_punctuation\n");
    }

    // speech model
    if (model != NULL) {
      config->set_model(model);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "speech model %s\n", model);
    }

    // use enhanced model
    if (enhanced == 1) {
      config->set_use_enhanced(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "use_enhanced\n");
    }

    // hints  
    if (hints != NULL) {
      char *phrases[500] = { 0 };
      auto *context = config->add_speech_contexts();
      int argc = switch_separate_string((char *) hints, ',', phrases, 500);
      for (int i = 0; i < argc; i++) {
        context->add_phrases(phrases[i]);
      }
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added %d hints\n", argc);
    }

  	// Begin a stream.
  	m_streamer = m_stub->StreamingRecognize(&m_context);

  	// Write the first request, containing the config only.
  	m_streamer->Write(m_request);
	}

	~GStreamer() {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "GStreamer::~GStreamer - deleting channel and stub\n");
	}

	bool write(void* data, uint32_t datalen) {
    m_request.set_audio_content(data, datalen);
    bool ok = m_streamer->Write(m_request);
    return ok;
  }

	uint32_t nextMessageSize(void) {
		uint32_t size = 0;
		m_streamer->NextMessageSize(&size);
		return size;
	}

	bool read(StreamingRecognizeResponse* response) {
		return m_streamer->Read(response);
	}

	grpc::Status finish() {
		return m_streamer->Finish();
	}

	void writesDone() {
    // grpc crashes if we call this twice on a stream
    if (!m_writesDone) {
      m_streamer->WritesDone();
      m_writesDone = true;
    }
	}

protected:

private:
	switch_core_session_t* m_session;
  grpc::ClientContext m_context;
	std::shared_ptr<grpc::Channel> m_channel;
	std::unique_ptr<Speech::Stub> 	m_stub;
	std::unique_ptr< grpc::ClientReaderWriterInterface<StreamingRecognizeRequest, StreamingRecognizeResponse> > m_streamer;
	StreamingRecognizeRequest m_request;
  bool m_writesDone;
};

static void *SWITCH_THREAD_FUNC grpc_read_thread(switch_thread_t *thread, void *obj) {
  static int count;
	struct cap_cb *cb = (struct cap_cb *) obj;
	GStreamer* streamer = (GStreamer *) cb->streamer;

  // Read responses.
  StreamingRecognizeResponse response;
  while (streamer->read(&response)) {  // Returns false when no more to read.
    count++;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "response counter:  %d\n",count) ;
    auto speech_event_type = response.speech_event_type();
    Status st = response.error();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "speech_event_type: %d \n", response.speech_event_type()) ;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: error %s code: (%d)\n", st.message().c_str(), st.code()) ;
    if (response.has_error()) {
      Status status = response.error();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: error %s (%d)\n", status.message().c_str(), status.code()) ;
    }
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (!session) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: session %s is gone!\n", cb->sessionId) ;
      return nullptr;
    }
    
    if (cb->play_file == 1){
      cb->responseHandler(session, "play_interrupt");
    }
    
    for (int r = 0; r < response.results_size(); ++r) {
      auto result = response.results(r);
      cJSON * jResult = cJSON_CreateObject();
      cJSON * jAlternatives = cJSON_CreateArray();
      cJSON * jStability = cJSON_CreateNumber(result.stability());
      cJSON * jIsFinal = cJSON_CreateBool(result.is_final());

      cJSON_AddItemToObject(jResult, "stability", jStability);
      cJSON_AddItemToObject(jResult, "is_final", jIsFinal);
      cJSON_AddItemToObject(jResult, "alternatives", jAlternatives);

      for (int a = 0; a < result.alternatives_size(); ++a) {
        auto alternative = result.alternatives(a);
        cJSON* jAlt = cJSON_CreateObject();
        cJSON* jConfidence = cJSON_CreateNumber(alternative.confidence());
        cJSON* jTranscript = cJSON_CreateString(alternative.transcript().c_str());
        cJSON_AddItemToObject(jAlt, "confidence", jConfidence);
        cJSON_AddItemToObject(jAlt, "transcript", jTranscript);

        if (alternative.words_size() > 0) {
          cJSON * jWords = cJSON_CreateArray();
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: %d words\n", alternative.words_size()) ;
          for (int b = 0; b < alternative.words_size(); b++) {
            auto words = alternative.words(b);
            cJSON* jWord = cJSON_CreateObject();
            cJSON_AddItemToObject(jWord, "word", cJSON_CreateString(words.word().c_str()));
            if (words.has_start_time()) {
              cJSON_AddItemToObject(jWord, "start_time", cJSON_CreateNumber(words.start_time().seconds()));
            }
            if (words.has_end_time()) {
              cJSON_AddItemToObject(jWord, "end_time", cJSON_CreateNumber(words.end_time().seconds()));
            }
            cJSON_AddItemToArray(jWords, jWord);
          }
          cJSON_AddItemToObject(jAlt, "words", jWords);
        }
        cJSON_AddItemToArray(jAlternatives, jAlt);
      }

      char* json = cJSON_PrintUnformatted(jResult);
      cb->responseHandler(session, (const char *) json);
      free(json);

      cJSON_Delete(jResult);
    }

    if (speech_event_type == StreamingRecognizeResponse_SpeechEventType_END_OF_SINGLE_UTTERANCE) {
      // we only get this when we have requested it, and recognition stops after we get this
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: got end_of_utterance\n") ;
      cb->responseHandler(session, "end_of_utterance");
      cb->end_of_utterance = 1;
      streamer->writesDone();
    }
    switch_core_session_rwunlock(session);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: got %d responses\n", response.results_size());
  }

  {
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (session) {
      // if (1 == cb->end_of_utterance) {
      //   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "end of utterance/transcript");
      //   cb->responseHandler(session, "end_of_transcript");
      // }
      grpc::Status status = streamer->finish();
      if (11 == status.error_code()) {
        if (std::string::npos != status.error_message().find("Exceeded maximum allowed stream duration")) {
          cb->responseHandler(session, "max_duration_exceeded");
        }
        else {
          cb->responseHandler(session, "no_audio");
        }
      }
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: finish() status %s (%d)\n", status.error_message().c_str(), status.error_code()) ;
      switch_core_session_rwunlock(session);
    }
  }
  return nullptr;
}

extern "C" {
    switch_status_t google_speech_init() {
      const char* gcsServiceKeyFile = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
      if (gcsServiceKeyFile) {
        try {
          auto creds = grpc::GoogleDefaultCredentials();
        } catch (const std::exception& e) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, 
            "Error initializing google api with provided credentials in %s: %s\n", gcsServiceKeyFile, e.what());
          return SWITCH_STATUS_FALSE;
        }
      }
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t google_speech_cleanup() {
      return SWITCH_STATUS_SUCCESS;
    }
    switch_status_t google_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
          uint32_t samples_per_second, uint32_t channels, char* lang, int interim, int single_utterence,
          int separate_recognition, int max_alternatives, int profinity_filter, int word_time_offset,
          int punctuation, char* model, int enhanced, char* hints, char* play_file, void **ppUserData) {

      switch_channel_t *channel = switch_core_session_get_channel(session);
      struct cap_cb *cb;
      int err;

      cb =(struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
      strncpy(cb->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
      cb->end_of_utterance = 0;
      if (play_file != NULL){
        cb->play_file = 1;
      }
      
      switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));

      GStreamer *streamer = NULL;
      try {
        streamer = new GStreamer(session, channels, lang, interim, single_utterence, separate_recognition, max_alternatives,
         profinity_filter, word_time_offset, punctuation, model, enhanced, hints);
        cb->streamer = streamer;
      } catch (std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
          switch_channel_get_name(channel), e.what());
        return SWITCH_STATUS_FALSE;
      }

      cb->responseHandler = responseHandler;

      // create the read thread
      switch_threadattr_t *thd_attr = NULL;
      switch_memory_pool_t *pool = switch_core_session_get_pool(session);

      switch_threadattr_create(&thd_attr, pool);
      switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
      switch_thread_create(&cb->thread, thd_attr, grpc_read_thread, cb, pool);

      *ppUserData = cb;
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t google_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

      if (bug) {
        struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
        switch_mutex_lock(cb->mutex);

      // stop playback if available
       if (cb->play_file == 1){ 
		      switch_channel_set_flag_value(channel, CF_BREAK, 1);
        }

        // close connection and get final responses
        GStreamer* streamer = (GStreamer *) cb->streamer;

        if (streamer) {
          streamer->writesDone();

          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "google_speech_session_cleanup: waiting for read thread to complete\n");
          switch_status_t st;
          switch_thread_join(&st, cb->thread);
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "google_speech_session_cleanup: read thread completed\n");

          delete streamer;
          cb->streamer = NULL;
        }

        if (cb->resampler) {
          speex_resampler_destroy(cb->resampler);
        }

        switch_channel_set_private(channel, MY_BUG_NAME, NULL);
			  switch_mutex_unlock(cb->mutex);

        if (!channelIsClosing) {
          switch_core_media_bug_remove(session, &bug);
        }

			  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "google_speech_session_cleanup: Closed stream\n");

			  return SWITCH_STATUS_SUCCESS;
      }

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
      return SWITCH_STATUS_FALSE;
    }

    switch_bool_t google_speech_frame(switch_media_bug_t *bug, void* user_data) {
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
