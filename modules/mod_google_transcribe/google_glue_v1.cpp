#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "mod_google_transcribe.h"

#include "gstreamer.h"
#include "generic_google_glue.h"

#include "google/cloud/speech/v1p1beta1/cloud_speech.grpc.pb.h"

using google::cloud::speech::v1p1beta1::RecognitionConfig;
using google::cloud::speech::v1p1beta1::Speech;
using google::cloud::speech::v1p1beta1::SpeechContext;
using google::cloud::speech::v1p1beta1::StreamingRecognizeRequest;
using google::cloud::speech::v1p1beta1::StreamingRecognizeResponse;
using google::cloud::speech::v1p1beta1::SpeakerDiarizationConfig;
using google::cloud::speech::v1p1beta1::SpeechAdaptation;
using google::cloud::speech::v1p1beta1::PhraseSet;
using google::cloud::speech::v1p1beta1::PhraseSet_Phrase;
using google::cloud::speech::v1p1beta1::RecognitionMetadata;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_DISCUSSION;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_PRESENTATION;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_PHONE_CALL;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_VOICEMAIL;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_PROFESSIONALLY_PRODUCED;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_VOICE_SEARCH;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_VOICE_COMMAND;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_InteractionType_DICTATION;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_MicrophoneDistance_NEARFIELD;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_MicrophoneDistance_MIDFIELD;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_MicrophoneDistance_FARFIELD;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_OriginalMediaType_AUDIO;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_OriginalMediaType_VIDEO;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_RecordingDeviceType_SMARTPHONE;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_RecordingDeviceType_PC;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_RecordingDeviceType_PHONE_LINE;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_RecordingDeviceType_VEHICLE;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_RecordingDeviceType_OTHER_OUTDOOR_DEVICE;
using google::cloud::speech::v1p1beta1::RecognitionMetadata_RecordingDeviceType_OTHER_INDOOR_DEVICE;
using google::cloud::speech::v1p1beta1::StreamingRecognizeResponse_SpeechEventType_END_OF_SINGLE_UTTERANCE;
using google::rpc::Status;

typedef GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub> GStreamer_V1;

template<>
GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub>::GStreamer(
    switch_core_session_t *session, 
    uint32_t channels, 
    char* lang, 
    int interim, 
    uint32_t config_sample_rate,
		uint32_t samples_per_second,
    int single_utterance, 
    int separate_recognition,
		int max_alternatives, 
    int profanity_filter, 
    int word_time_offset, 
    int punctuation, 
    const char* model, 
    int enhanced, 
		const char* hints) : m_session(session), m_writesDone(false), m_connected(false), 
      m_audioBuffer(CHUNKSIZE, 15) {
  
    switch_channel_t *channel = switch_core_session_get_channel(session);
    m_channel = create_grpc_channel(channel);
  	m_stub = Speech::NewStub(m_channel);
  		
		auto* streaming_config = m_request.mutable_streaming_config();
		RecognitionConfig* config = streaming_config->mutable_config();

    streaming_config->set_interim_results(interim);
    if (single_utterance == 1) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_single_utterance\n");
      streaming_config->set_single_utterance(true);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_single_utterance is FALSE\n");
      streaming_config->set_single_utterance(false);
    }

		config->set_language_code(lang);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "transcribe language %s \n", lang);
    
  	config->set_sample_rate_hertz(config_sample_rate);

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
    if (profanity_filter == 1) {
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
    else {
      config->set_enable_automatic_punctuation(false);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "disable_automatic_punctuation\n");
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
      auto* adaptation = config->mutable_adaptation();
      auto* phrase_set = adaptation->add_phrase_sets();
      auto *context = config->add_speech_contexts();
      float boost = -1;

      // get boost setting for the phrase set in its entirety
      if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_HINTS_BOOST"))) {
     	  boost = (float) atof(switch_channel_get_variable(channel, "GOOGLE_SPEECH_HINTS_BOOST"));
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "boost value: %f\n", boost);
        phrase_set->set_boost(boost);
      }

      // hints are either a simple comma-separated list of phrases, or a json array of objects
      // containing a phrase and a boost value
      auto *jHint = cJSON_Parse((char *) hints);
      if (jHint) {
        int i = 0;
        cJSON *jPhrase = NULL;
        cJSON_ArrayForEach(jPhrase, jHint) {
          auto* phrase = phrase_set->add_phrases();
          cJSON *jItem = cJSON_GetObjectItem(jPhrase, "phrase");
          if (jItem) {
            phrase->set_value(cJSON_GetStringValue(jItem));
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "phrase: %s\n", phrase->value().c_str());
            if (cJSON_GetObjectItem(jPhrase, "boost")) {
              phrase->set_boost((float) cJSON_GetObjectItem(jPhrase, "boost")->valuedouble);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "boost value: %f\n", phrase->boost());
            }
            i++;
          }
        }
        cJSON_Delete(jHint);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added %d hints\n", i);
      }
      else {
        char *phrases[500] = { 0 };
        int argc = switch_separate_string((char *) hints, ',', phrases, 500);
        for (int i = 0; i < argc; i++) {
          auto* phrase = phrase_set->add_phrases();
          phrase->set_value(phrases[i]);
        }
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added %d hints\n", argc);
      }
    }

    const char* var;
    
    // alternative language
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES")) {
      char *alt_langs[3] = { 0 };
      int argc = switch_separate_string((char *) var, ',', alt_langs, 3);
      for (int i = 0; i < argc; i++) {
        config->add_alternative_language_codes(alt_langs[i]);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added alternative lang %s\n", alt_langs[i]);
      }
    }

    // speaker diarization
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION")) {
      auto* diarization_config = config->mutable_diarization_config();
      diarization_config->set_enable_speaker_diarization(true);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enabling speaker diarization\n", var);
      if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT")) {
        int count = std::max(atoi(var), 1);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting min speaker count to %d\n", count);
        diarization_config->set_min_speaker_count(count);
      }
      if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT")) {
        int count = std::max(atoi(var), 2);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting max speaker count to %d\n", count);
        diarization_config->set_max_speaker_count(count);
      }
    }

    // recognition metadata
    auto* metadata = config->mutable_metadata();
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_METADATA_INTERACTION_TYPE")) {
      if (case_insensitive_match("discussion", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_DISCUSSION);
      if (case_insensitive_match("presentation", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_PRESENTATION);
      if (case_insensitive_match("phone_call", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_PHONE_CALL);
      if (case_insensitive_match("voicemail", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_VOICEMAIL);
      if (case_insensitive_match("professionally_produced", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_PROFESSIONALLY_PRODUCED);
      if (case_insensitive_match("voice_search", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_VOICE_SEARCH);
      if (case_insensitive_match("voice_command", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_VOICE_COMMAND);
      if (case_insensitive_match("dictation", var)) metadata->set_interaction_type(RecognitionMetadata_InteractionType_DICTATION);
    }
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_METADATA_INDUSTRY_NAICS_CODE")) {
      metadata->set_industry_naics_code_of_audio(atoi(var));
    }
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_METADATA_MICROPHONE_DISTANCE")) {
      if (case_insensitive_match("nearfield", var)) metadata->set_microphone_distance(RecognitionMetadata_MicrophoneDistance_NEARFIELD);
      if (case_insensitive_match("midfield", var)) metadata->set_microphone_distance(RecognitionMetadata_MicrophoneDistance_MIDFIELD);
      if (case_insensitive_match("farfield", var)) metadata->set_microphone_distance(RecognitionMetadata_MicrophoneDistance_FARFIELD);
    }
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_METADATA_ORIGINAL_MEDIA_TYPE")) {
      if (case_insensitive_match("audio", var)) metadata->set_original_media_type(RecognitionMetadata_OriginalMediaType_AUDIO);
      if (case_insensitive_match("video", var)) metadata->set_original_media_type(RecognitionMetadata_OriginalMediaType_VIDEO);
    }
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_METADATA_RECORDING_DEVICE_TYPE")) {
      if (case_insensitive_match("smartphone", var)) metadata->set_recording_device_type(RecognitionMetadata_RecordingDeviceType_SMARTPHONE);
      if (case_insensitive_match("pc", var)) metadata->set_recording_device_type(RecognitionMetadata_RecordingDeviceType_PC);
      if (case_insensitive_match("phone_line", var)) metadata->set_recording_device_type(RecognitionMetadata_RecordingDeviceType_PHONE_LINE);
      if (case_insensitive_match("vehicle", var)) metadata->set_recording_device_type(RecognitionMetadata_RecordingDeviceType_VEHICLE);
      if (case_insensitive_match("other_outdoor_device", var)) metadata->set_recording_device_type(RecognitionMetadata_RecordingDeviceType_OTHER_OUTDOOR_DEVICE);
      if (case_insensitive_match("other_indoor_device", var)) metadata->set_recording_device_type(RecognitionMetadata_RecordingDeviceType_OTHER_INDOOR_DEVICE);
    }
}

static void *SWITCH_THREAD_FUNC grpc_read_thread(switch_thread_t *thread, void *obj) {
  static int count;
	struct cap_cb *cb = (struct cap_cb *) obj;
	GStreamer_V1* streamer = (GStreamer_V1 *) cb->streamer;

  bool connected = streamer->waitForConnect();
  if (!connected) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "google transcribe grpc read thread exiting since we didnt connect\n") ;
    return nullptr;
  }

  // Read responses.
  StreamingRecognizeResponse response;
  while (streamer->read(&response)) {  // Returns false when no more to read.
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (!session) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: session %s is gone!\n", cb->sessionId) ;
      return nullptr;
    }
    count++;
    auto speech_event_type = response.speech_event_type();
    if (response.has_error()) {
      Status status = response.error();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: error %s (%d)\n", status.message().c_str(), status.code()) ;
      cJSON* json = cJSON_CreateObject();
      cJSON_AddStringToObject(json, "type", "error");
      cJSON_AddStringToObject(json, "error", status.message().c_str());
      char* jsonString = cJSON_PrintUnformatted(json);
      cb->responseHandler(session, jsonString, cb->bugname);
      free(jsonString);
      cJSON_Delete(json);
    }
    
    if (cb->play_file == 1){
      cb->responseHandler(session, "play_interrupt", cb->bugname);
    }
    
    for (int r = 0; r < response.results_size(); ++r) {
      auto result = response.results(r);
      cJSON * jResult = cJSON_CreateObject();
      cJSON * jAlternatives = cJSON_CreateArray();
      cJSON * jStability = cJSON_CreateNumber(result.stability());
      cJSON * jIsFinal = cJSON_CreateBool(result.is_final());
      cJSON * jLanguageCode = cJSON_CreateString(result.language_code().c_str());
      cJSON * jChannelTag = cJSON_CreateNumber(result.channel_tag());

      auto duration = result.result_end_time();
      int32_t seconds = duration.seconds();
      int64_t nanos = duration.nanos();
      int span = (int) trunc(seconds * 1000. + ((float) nanos / 1000000.));
      cJSON * jResultEndTime = cJSON_CreateNumber(span);

      cJSON_AddItemToObject(jResult, "stability", jStability);
      cJSON_AddItemToObject(jResult, "is_final", jIsFinal);
      cJSON_AddItemToObject(jResult, "alternatives", jAlternatives);
      cJSON_AddItemToObject(jResult, "language_code", jLanguageCode);
      cJSON_AddItemToObject(jResult, "channel_tag", jChannelTag);
      cJSON_AddItemToObject(jResult, "result_end_time", jResultEndTime);

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
            int speaker_tag = words.speaker_tag();
            if (speaker_tag > 0) {
              cJSON_AddItemToObject(jWord, "speaker_tag", cJSON_CreateNumber(speaker_tag));
            }
            float confidence = words.confidence();
            if (confidence > 0.0) {
              cJSON_AddItemToObject(jWord, "confidence", cJSON_CreateNumber(confidence));
            }

            cJSON_AddItemToArray(jWords, jWord);
          }
          cJSON_AddItemToObject(jAlt, "words", jWords);
        }
        cJSON_AddItemToArray(jAlternatives, jAlt);
      }

      char* json = cJSON_PrintUnformatted(jResult);
      cb->responseHandler(session, (const char *) json, cb->bugname);
      free(json);

      cJSON_Delete(jResult);
    }

    if (speech_event_type == StreamingRecognizeResponse_SpeechEventType_END_OF_SINGLE_UTTERANCE) {
      // we only get this when we have requested it, and recognition stops after we get this
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: got end_of_utterance\n") ;
      cb->got_end_of_utterance = 1;
      cb->responseHandler(session, "end_of_utterance", cb->bugname);
      if (cb->wants_single_utterance) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: sending writesDone because we want only a single utterance\n") ;
        streamer->writesDone();
      }
    }
    switch_core_session_rwunlock(session);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: got %d responses\n", response.results_size());
  }

  {
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (session) {
      grpc::Status status = streamer->finish();
      if (11 == status.error_code()) {
        if (std::string::npos != status.error_message().find("Exceeded maximum allowed stream duration")) {
          cb->responseHandler(session, "max_duration_exceeded", cb->bugname);
        }
        else {
          cb->responseHandler(session, "no_audio", cb->bugname);
        }
      }
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: finish() status %s (%d)\n", status.error_message().c_str(), status.error_code()) ;
      switch_core_session_rwunlock(session);
    }
  }
  return nullptr;
}

template<>
bool GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub>::write(void* data, uint32_t datalen) {
  if (!m_connected) {
    if (datalen % CHUNKSIZE == 0) {
      m_audioBuffer.add(data, datalen);
    }
    return true;
  }
  m_request.set_audio_content(data, datalen);
  bool ok = m_streamer->Write(m_request);
  return ok;
}


template <>
void GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub>::connect() {
    assert(!m_connected);
    // Begin a stream.
    m_streamer = m_stub->StreamingRecognize(&m_context);
    m_connected = true;

    // read thread is waiting on this
    m_promise.set_value();

    // Write the first request, containing the config only.
    m_streamer->Write(m_request);

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

extern "C" {

    switch_status_t google_speech_session_cleanup_v1(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
      return google_speech_session_cleanup<GStreamer_V1>(session, channelIsClosing, bug);
    }

    switch_bool_t google_speech_frame_v1(switch_media_bug_t *bug, void* user_data) {
      return google_speech_frame<GStreamer_V1>(bug, user_data);
    }

    switch_status_t google_speech_session_init_v1(switch_core_session_t *session, responseHandler_t responseHandler,
		  uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, int single_utterence,
		  int separate_recognition, int max_alternatives, int profinity_filter, int word_time_offset, int punctuation, const char* model, int enhanced, 
		  const char* hints, char* play_file, void **ppUserData) {
      return google_speech_session_init<GStreamer_V1>(session, responseHandler, grpc_read_thread, to_rate, samples_per_second, channels,
        lang, interim, bugname, single_utterence, separate_recognition, max_alternatives, profinity_filter,
        word_time_offset, punctuation, model, enhanced, hints, play_file, ppUserData);
    }

}
