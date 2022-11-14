#include <switch.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>

#include "mod_ibm_transcribe.h"
#include "simple_buffer.h"
#include "parser.hpp"
#include "audio_pipe.hpp"

#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000  320 /*which means each 20ms frame as 320 bytes at 8 khz (1 channel only)*/

namespace {
  static bool hasDefaultCredentials = false;
  static const char* defaultApiKey = nullptr;
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 1, 5));
  static unsigned int idxCallCount = 0;
  static uint32_t playCount = 0;


  static void destroy_tech_pvt(private_t *tech_pvt) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->sessionId, tech_pvt->id);
    if (tech_pvt) {
      if (tech_pvt->pAudioPipe) {
        ibm::AudioPipe* p = (ibm::AudioPipe *) tech_pvt->pAudioPipe;
        delete p;
        tech_pvt->pAudioPipe = nullptr;
      }
      if (tech_pvt->resampler) {
          speex_resampler_destroy(tech_pvt->resampler);
          tech_pvt->resampler = NULL;
      }

      /*
      if (tech_pvt->vad) {
        switch_vad_destroy(&tech_pvt->vad);
        tech_pvt->vad = nullptr;
      }
      */
    }
  }

  std::string encodeURIComponent(std::string decoded)
  {

      std::ostringstream oss;
      std::regex r("[!'\\(\\)*-.0-9A-Za-z_~:]");

      for (char &c : decoded)
      {
          if (std::regex_match((std::string){c}, r))
          {
              oss << c;
          }
          else
          {
              oss << "%" << std::uppercase << std::hex << (0xff & c);
          }
      }
      return oss.str();
  }

  std::string& constructPath(switch_core_session_t* session, std::string& path, 
    int sampleRate, int channels, const char* language, int interim) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    const char *var ;
    std::ostringstream oss;

    const char* instanceId = switch_channel_get_variable(channel, "IBM_SPEECH_INSTANCE_ID");

    oss << "/instances/" << instanceId << "/v1/recognize";

    // access token 
    if (var = switch_channel_get_variable(channel, "IBM_ACCESS_TOKEN")) {
     oss <<  "?access_token=" << var;
    }

    // model = voice
    if (var = switch_channel_get_variable(channel, "IBM_SPEECH_MODEL")) {
      oss <<  "&model=" <<  var;
    }
    else {
      oss <<  "&model=" << language;
    }

    if (var = switch_channel_get_variable(channel, "IBM_SPEECH_LANGUAGE_CUSTOMIZATION_ID")) {
      oss <<  "&language_customization_id=" <<  var;
    }
    if (var = switch_channel_get_variable(channel, "IBM_SPEECH_ACOUSTIC_CUSTOMIZATION_ID")) {
      oss <<  "&acoustic_customization_id=" <<  var;
    }
    if (var = switch_channel_get_variable(channel, "IBM_SPEECH_BASE_MODEL_VERSION")) {
      oss <<  "&base_model_version=" <<  var;
    }
    if (var = switch_channel_get_variable(channel, "IBM_SPEECH_WATSON_METADATA")) {
      oss <<  "&x-watson-metadata=" <<  var;
    }
    if (switch_true(switch_channel_get_variable(channel, "IBM_SPEECH_WATSON_LEARNING_OPT_OUT"))) {
      oss <<  "&x-watson-learning-opt-out=true";
    }

    path = oss.str();
    return path;
  }

  static void eventCallback(const char* sessionId, ibm::AudioPipe::NotifyEvent_t event, const char* message, bool finished) {
    switch_core_session_t* session = switch_core_session_locate(sessionId);
    if (session) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "received message %s\n", message);
      if (bug) {
        private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
        if (tech_pvt) {
          switch (event) {
            case ibm::AudioPipe::CONNECT_SUCCESS:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "connection successful\n");
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_SUCCESS, NULL, tech_pvt->bugname, finished);
            break;
            case ibm::AudioPipe::CONNECT_FAIL:
            {
              // first thing: we can no longer access the AudioPipe
              std::stringstream json;
              json << "{\"reason\":\"" << message << "\"}";
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_FAIL, (char *) json.str().c_str(), tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "connection failed: %s\n", message);
            }
            break;
            case ibm::AudioPipe::CONNECTION_DROPPED:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_DISCONNECT, NULL, tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection dropped from far end\n");
            break;
            case ibm::AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection closed gracefully\n");
            break;
            case ibm::AudioPipe::MESSAGE:
              if (NULL != strstr(message, "\"state\": \"listening\"")) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "ibm service is listening\n");
              }
              else tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_RESULTS, message, tech_pvt->bugname, finished);
            break;

            default:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "got unexpected msg from ibm %d:%s\n", event, message);
              break;
          }
        }
      }
      switch_core_session_rwunlock(session);
    }
  }
  switch_status_t fork_data_init(private_t *tech_pvt, switch_core_session_t *session, 
    int sampling, int desiredSampling, int channels, char *lang, int interim, 
    char* bugname, responseHandler_t responseHandler) {

    int err;
    switch_codec_implementation_t read_impl;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    const char* region = switch_channel_get_variable(channel, "IBM_SPEECH_REGION");
    const char* instanceId = switch_channel_get_variable(channel, "IBM_SPEECH_INSTANCE_ID");
    if (!region || !instanceId || !switch_channel_get_variable(channel, "IBM_ACCESS_TOKEN")) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
        "missing IBM_SPEECH_REGION or IBM_SPEECH_INSTANCE_ID or IBM_ACCESS_TOKEN\n");
      return SWITCH_STATUS_FALSE;
    }

    switch_core_session_get_read_impl(session, &read_impl);
  
    memset(tech_pvt, 0, sizeof(private_t));
  
    std::ostringstream oss;
    oss << "api." << region << ".speech-to-text.watson.cloud.ibm.com";
    std::string host = oss.str();
    std::string path;
    constructPath(session, path, desiredSampling, channels, lang, interim);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "host: %s, path: %s\n", host.c_str(), path.c_str());

    strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    strncpy(tech_pvt->host,host.c_str(), MAX_WS_URL_LEN);
    tech_pvt->port = 443;
    strncpy(tech_pvt->path, path.c_str(), MAX_PATH_LEN);    
    tech_pvt->sampling = desiredSampling;
    tech_pvt->responseHandler = responseHandler;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->buffer_overrun_notified = 0;
    
    size_t buflen = LWS_PRE + (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD * nAudioBufferSecs);

    ibm::AudioPipe* ap = new ibm::AudioPipe(tech_pvt->sessionId, tech_pvt->host, tech_pvt->port, tech_pvt->path, 
      buflen, read_impl.decoded_bytes_per_packet, eventCallback);
    if (!ap) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe\n");
      return SWITCH_STATUS_FALSE;
    }
    
    const char* access_token = switch_channel_get_variable(channel, "IBM_ACCESS_TOKEN");
    ap->setAccessToken(access_token);
    if (interim) ap->enableInterimTranscripts(true);

    tech_pvt->pAudioPipe = static_cast<void *>(ap);

    switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));

    if (desiredSampling != sampling) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) resampling from %u to %u\n", tech_pvt->id, sampling, desiredSampling);
      tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
      if (0 != err) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
        return SWITCH_STATUS_FALSE;
      }
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) no resampling needed for this call\n", tech_pvt->id);
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_data_init\n", tech_pvt->id);

    return SWITCH_STATUS_SUCCESS;
  }

  void lws_logger(int level, const char *line) {
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;

    switch (level) {
      case LLL_ERR: llevel = SWITCH_LOG_ERROR; break;
      case LLL_WARN: llevel = SWITCH_LOG_WARNING; break;
      case LLL_NOTICE: llevel = SWITCH_LOG_NOTICE; break;
      case LLL_INFO: llevel = SWITCH_LOG_INFO; break;
      break;
    }
	  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "%s\n", line);
  }
}


extern "C" {
  switch_status_t ibm_transcribe_init() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_ibm_transcribe: audio buffer (in secs):    %d secs\n", nAudioBufferSecs);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_ibm_transcribe: lws service threads:       %d\n", nServiceThreads);
 
    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE || LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;
    
    ibm::AudioPipe::initialize(nServiceThreads, logs, lws_logger);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AudioPipe::initialize completed\n");

		return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t ibm_transcribe_cleanup() {
    bool cleanup = false;
    cleanup = ibm::AudioPipe::deinitialize();
    if (cleanup == true) {
        return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }
	
  switch_status_t ibm_transcribe_session_init(switch_core_session_t *session, 
    responseHandler_t responseHandler, uint32_t samples_per_second, uint32_t channels, 
    char* lang, int interim, char* bugname, void **ppUserData)
  {    	
    int err;

    // allocate per-session data structure
    private_t* tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));
    if (!tech_pvt) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
      return SWITCH_STATUS_FALSE;
    }

    if (SWITCH_STATUS_SUCCESS != fork_data_init(tech_pvt, session, samples_per_second, /*8000*/ 16000, channels, lang, interim, bugname, responseHandler)) {
      destroy_tech_pvt(tech_pvt);
      return SWITCH_STATUS_FALSE;
    }

    *ppUserData = tech_pvt;

    ibm::AudioPipe *pAudioPipe = static_cast<ibm::AudioPipe *>(tech_pvt->pAudioPipe);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connecting now\n");
    pAudioPipe->connect();
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection in progress\n");
    return SWITCH_STATUS_SUCCESS;
  }

	switch_status_t ibm_transcribe_session_stop(switch_core_session_t *session,int channelIsClosing, char* bugname) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "ibm_transcribe_session_stop: no bug - websocket conection already closed\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    uint32_t id = tech_pvt->id;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) ibm_transcribe_session_stop\n", id);

    if (!tech_pvt) return SWITCH_STATUS_FALSE;
      
    // close connection and get final responses
    switch_mutex_lock(tech_pvt->mutex);
    switch_channel_set_private(channel, bugname, NULL);
    if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

    ibm::AudioPipe *pAudioPipe = static_cast<ibm::AudioPipe *>(tech_pvt->pAudioPipe);
    if (pAudioPipe) {
      pAudioPipe->close();
      tech_pvt->pAudioPipe = nullptr;
    }
    destroy_tech_pvt(tech_pvt);
    switch_mutex_unlock(tech_pvt->mutex);
    switch_mutex_destroy(tech_pvt->mutex);
    tech_pvt->mutex = nullptr;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) ibm_transcribe_session_stop\n", id);
    return SWITCH_STATUS_SUCCESS;
  }
	
	switch_bool_t ibm_transcribe_frame(switch_core_session_t *session, switch_media_bug_t *bug) {
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    size_t inuse = 0;
    bool dirty = false;
    char *p = (char *) "{\"msg\": \"buffer overrun\"}";

    if (!tech_pvt) return SWITCH_TRUE;
    
    if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
      if (!tech_pvt->pAudioPipe) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }
      ibm::AudioPipe *pAudioPipe = static_cast<ibm::AudioPipe *>(tech_pvt->pAudioPipe);
      if (pAudioPipe->getLwsState() != ibm::AudioPipe::LWS_CLIENT_CONNECTED) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }

      pAudioPipe->lockAudioBuffer();
      size_t available = pAudioPipe->binarySpaceAvailable();
      if (NULL == tech_pvt->resampler) {
        switch_frame_t frame = { 0 };
        frame.data = pAudioPipe->binaryWritePtr();
        frame.buflen = available;
        while (true) {

          // check if buffer would be overwritten; dump packets if so
          if (available < pAudioPipe->binaryMinSpace()) {
            if (!tech_pvt->buffer_overrun_notified) {
              tech_pvt->buffer_overrun_notified = 1;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
            }
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets!\n", 
              tech_pvt->id);
            pAudioPipe->binaryWritePtrResetToZero();

            frame.data = pAudioPipe->binaryWritePtr();
            frame.buflen = available = pAudioPipe->binarySpaceAvailable();
          }

          switch_status_t rv = switch_core_media_bug_read(bug, &frame, SWITCH_TRUE);
          if (rv != SWITCH_STATUS_SUCCESS) break;
          if (frame.datalen) {
            pAudioPipe->binaryWritePtrAdd(frame.datalen);
            frame.buflen = available = pAudioPipe->binarySpaceAvailable();
            frame.data = pAudioPipe->binaryWritePtr();
            dirty = true;
          }
        }
      }
      else {
        uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = { 0 };
        frame.data = data;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
          if (frame.datalen) {
            spx_uint32_t out_len = available >> 1;  // space for samples which are 2 bytes
            spx_uint32_t in_len = frame.samples;

            speex_resampler_process_interleaved_int(tech_pvt->resampler, 
              (const spx_int16_t *) frame.data, 
              (spx_uint32_t *) &in_len, 
              (spx_int16_t *) ((char *) pAudioPipe->binaryWritePtr()),
              &out_len);

            if (out_len > 0) {
              // bytes written = num samples * 2 * num channels
              size_t bytes_written = 2 * out_len * tech_pvt->channels;
              pAudioPipe->binaryWritePtrAdd(bytes_written);
              available = pAudioPipe->binarySpaceAvailable();
              dirty = true;
            }
            if (available < pAudioPipe->binaryMinSpace()) {
              if (!tech_pvt->buffer_overrun_notified) {
                tech_pvt->buffer_overrun_notified = 1;
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets!\n", 
                  tech_pvt->id);
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
              }
              break;
            }
          }
        }
      }

      pAudioPipe->unlockAudioBuffer();
      switch_mutex_unlock(tech_pvt->mutex);
    }
    return SWITCH_TRUE;
  }
}
