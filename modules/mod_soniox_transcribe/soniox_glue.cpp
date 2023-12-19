#include <cstdlib>
#include <algorithm>
#include <future>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "soniox/speech_service.grpc.pb.h"

namespace soniox_asr = soniox::speech_service;

#include "mod_soniox_transcribe.h"
#include "simple_buffer.h"

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

  void createInitMessage() {
    switch_channel_t *channel = switch_core_session_get_channel(m_session);

    std::shared_ptr<grpc::Channel> grpcChannel ;
    auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
    grpcChannel = grpc::CreateChannel("api.soniox.com:443", channelCreds);

    if (!grpcChannel) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p failed creating grpc channel\n", this);	
      throw std::runtime_error(std::string("Error creating grpc channel"));
    }

    m_stub = std::move(soniox_asr::SpeechService::NewStub(grpcChannel));

    const char* var = switch_channel_get_variable(channel, "SONIOX_API_KEY");

    /* set configuration parameters which are carried in the RecognitionInitMessage */
    m_request.set_api_key(var);
    auto config = m_request.mutable_config();
    config->set_audio_format("pcm_s16le");
    config->set_sample_rate_hertz(8000);
    config->set_num_audio_channels(1);
    config->set_include_nonfinal(m_interim);

    /* model */
    if ((var = switch_channel_get_variable(channel, "SONIOX_MODEL"))) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "set model %s\n",var);
      config->set_model(var);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "set model precision_ivr\n",var);
      config->set_model("precision_ivr");
    }

    /* profanity filter */
    if (switch_true(switch_channel_get_variable(channel, "SONIOX_PROFANITY_FILTER"))) {
      config->set_enable_profanity_filter(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable profanity filter\n",var);
    }
    else config->set_enable_profanity_filter(false);

    /* endpointing */
    config->set_enable_endpoint_detection(true);

    /* hints */
    const char* hints = switch_channel_get_variable(channel, "SONIOX_HINTS");
    if (hints) {
      float boost = -1;
      auto* speech_context = config->mutable_speech_context();

      // hints are either a simple comma-separated list of phrases, or a json array of objects
      // containing a phrase and a boost value
      auto *jHint = cJSON_Parse((char *) hints);
      if (jHint) {
        int i = 0;
        cJSON *jPhrase = NULL;
        cJSON_ArrayForEach(jPhrase, jHint) {
          cJSON *jItem = cJSON_GetObjectItem(jPhrase, "phrase");
          if (jItem) {
            auto* speech_context_entry = speech_context->add_entries();
            auto *phrase = cJSON_GetStringValue(jItem);
            speech_context_entry->add_phrases(phrase);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "hint: %s\n", phrase);
            if (cJSON_GetObjectItem(jPhrase, "boost")) {
              float boost = (float) cJSON_GetObjectItem(jPhrase, "boost")->valuedouble;
              speech_context_entry->set_boost(boost);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "boost value: %f\n", boost);
            }
            i++;
          }
        }
        cJSON_Delete(jHint);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added %d hints\n", i);
      }
      else {
        // single set of hints 
        char *phrases[500] = { 0 };
        int argc = switch_separate_string((char *) hints, ',', phrases, 500);
        auto* speech_context_entry = speech_context->add_entries();
        for (int i = 0; i < argc; i++) {
          speech_context_entry->add_phrases(phrases[i]);
        }
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added %d hints\n", argc);
        const char* boost_str = switch_channel_get_variable(channel, "SONIOX_HINTS_BOOST");
        if (boost_str) {
          float boost = (float) atof(boost_str);
          speech_context_entry->set_boost(boost);
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "boost value: %f\n", boost);
        }
      }
    }

    var = switch_channel_get_variable(channel, "SONIOX_STORAGE_ID");
    if (var) {
      auto* storage_config = config->mutable_storage_config();
      storage_config->set_object_id(var);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "set storage id %s\n", var);

      var = switch_channel_get_variable(channel, "SONIOX_STORAGE_TITLE");
      if (var) {
        storage_config->set_title(var);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "set title %s\n", var);
      }

      if (switch_true(switch_channel_get_variable(channel, "SONIOX_STORAGE_DISABLE_AUDIO"))) {
        storage_config->set_disable_store_audio(true);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "disable audio storage\n");
      }

      else storage_config->set_disable_store_audio(false);
      if (switch_true(switch_channel_get_variable(channel, "SONIOX_STORAGE_DISABLE_TRANSCRIPT"))) {
        storage_config->set_disable_store_transcript(true);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "disable transcript storage\n");
      }
          
      else storage_config->set_disable_store_transcript(false);
      if (switch_true(switch_channel_get_variable(channel, "SONIOX_STORAGE_DISABLE_SEARCH"))) {
        storage_config->set_disable_search(true);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "disable search\n");
      }    
      else storage_config->set_disable_search(false);
    }
    /* speaker diarization */
    /*
    if (switch_true(switch_channel_get_variable(channel, "SONIOX_SPEAKER_DIARIZATION"))) {
      soniox_asr::SpeakerDiarizationConfig* diarization_config = config->mutable_diarization_config();
      diarization_config->set_enable_speaker_diarization(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable diarization\n", var);

      if ((var = switch_channel_get_variable(channel, "SONIOX_DIARIZATION_SPEAKER_COUNT"))) {
        int max_speaker_count = atoi(var);
        diarization_config->set_max_speaker_count(max_speaker_count);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "set max speaker count %d\n", max_speaker_count);
      }
    }
    */
  }

  void connect() {
    assert(!m_connected);
    // Begin a stream.

    createInitMessage();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p creating streamer\n", this);	
  	m_streamer = m_stub->TranscribeStream(&m_context);
    m_connected = true;

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
    m_request.set_audio(data, datalen);
    bool ok = m_streamer->Write(m_request);
    return ok;
  }

	uint32_t nextMessageSize(void) {
		uint32_t size = 0;
		m_streamer->NextMessageSize(&size);
		return size;
	}

	bool read(soniox_asr::TranscribeStreamResponse* response) {
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
	std::unique_ptr<soniox_asr::SpeechService::Stub> m_stub;
  soniox_asr::TranscribeStreamRequest m_request;
	std::unique_ptr< grpc::ClientReaderWriterInterface<soniox_asr::TranscribeStreamRequest, soniox_asr::TranscribeStreamResponse> > m_streamer;
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
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "soniox transcribe grpc read thread exiting since we didnt connect\n") ;
    return nullptr;
  }

  // Read responses.
  soniox_asr::TranscribeStreamResponse response;
  while (streamer->read(&response)) {  // Returns false when no more to read.
    if (!response.has_result()) continue;
    count++;
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (!session) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: session %s is gone!\n", cb->sessionId) ;
      return nullptr;
    }

    const auto& result = response.result();
    int nWords = result.words_size();
    if (0 == nWords) {
      switch_core_session_rwunlock(session);
      continue;
    }

    auto final_proc_time_ms = result.final_proc_time_ms();
    auto total_proc_time_ms = result.total_proc_time_ms();
    auto channel = result.channel();

    cJSON * jResult = cJSON_CreateObject();
    cJSON * jWords = cJSON_CreateArray();
    cJSON * jFinalProcTime = cJSON_CreateNumber(final_proc_time_ms);
    cJSON * jTotalProcTime = cJSON_CreateNumber(total_proc_time_ms);
    cJSON * jChannel = cJSON_CreateNumber(channel);
    cJSON_AddItemToObject(jResult, "words", jWords);
    cJSON_AddItemToObject(jResult, "channel", jChannel);
    cJSON_AddItemToObject(jResult, "final_proc_time", jFinalProcTime);
    cJSON_AddItemToObject(jResult, "total_proc_time", jTotalProcTime);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%d: received response with %d words\n", count, nWords) ;

    for (int i = 0; i < nWords; ++i) {
      auto& word = result.words(i);
      auto& text = word.text();
      auto& orig_text = word.orig_text();
      auto start_ms = word.start_ms();
      auto duration_ms = word.duration_ms();
      auto is_final = word.is_final();
      auto confidence = word.confidence();

      cJSON * jWord = cJSON_CreateObject();
      cJSON_AddStringToObject(jWord, "text", text.c_str());
      cJSON_AddStringToObject(jWord, "orig_text", text.c_str());
      cJSON_AddNumberToObject(jWord, "start_ms", start_ms);
      cJSON_AddNumberToObject(jWord, "duration_ms", duration_ms);
      cJSON_AddBoolToObject(jWord, "is_final", is_final);
      cJSON_AddNumberToObject(jWord, "confidence", confidence);

      cJSON_AddItemToArray(jWords, jWord);
    }
    char* json = cJSON_PrintUnformatted(jResult);
    cb->responseHandler(session, (const char *) json, cb->bugname, NULL);
    free(json);

    cJSON_Delete(jResult);

    switch_core_session_rwunlock(session);
  }
  return nullptr;
}
extern "C" {

    switch_status_t soniox_speech_init() {
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t soniox_speech_cleanup() {
      return SWITCH_STATUS_SUCCESS;
    }
    switch_status_t soniox_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
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
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_init:  initializing resampler\n");
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
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_init:  initializing vad\n");
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
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_init:  allocating streamer\n");
        streamer = new GStreamer(session, channels, lang, interim);
        cb->streamer = streamer;
      } catch (std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
          switch_channel_get_name(channel), e.what());
        return SWITCH_STATUS_FALSE;
      }

      if (!cb->vad) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_init:  no vad so connecting to soniox immediately\n");
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

    switch_status_t soniox_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
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

          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_cleanup: GStreamer (%p) waiting for read thread to complete\n", (void*)streamer);
          switch_status_t st;
          switch_thread_join(&st, cb->thread);
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_cleanup:  GStreamer (%p) read thread completed\n", (void*)streamer);

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

			  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "soniox_speech_session_cleanup: Closed stream\n");

			  switch_mutex_unlock(cb->mutex);


			  return SWITCH_STATUS_SUCCESS;
      }

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
      return SWITCH_STATUS_FALSE;
    }

    switch_bool_t soniox_speech_frame(switch_media_bug_t *bug, void* user_data) {
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
