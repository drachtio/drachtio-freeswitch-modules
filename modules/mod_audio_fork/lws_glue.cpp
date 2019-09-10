#include <switch.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <algorithm>
#include <condition_variable>
#include <cassert>
#include <cstdlib>
#include <fstream>

#include "base64.hpp"
#include "parser.hpp"
#include "mod_audio_fork.h"

#define WS_TIMEOUT_MS    50
#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000  320 /*which means each 20ms frame as 320 bytes at 8 khz (1 channel only)*/

#define WS_AUDIO_BUFFER_SIZE (FRAME_SIZE_16000 * 2 * 50 + LWS_PRE)  /* 50 frames at 20 ms packetization = 1 sec of audio, allow for 2 channels */

namespace {
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  static const char* mySubProtocolName = std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") ?
    std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") : "audiostream.drachtio.org";
  static int interrupted = 0;
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 1, 5));
  static struct lws_context *context[5] = {NULL, NULL, NULL, NULL, NULL};

  static unsigned int idxCallCount = 0;
  static std::list<private_t*> pendingConnects;
  static std::list<private_t*> pendingDisconnects;
  static std::list<private_t*> pendingWrites;
  static std::mutex g_mutex_connects;
  static std::mutex g_mutex_disconnects;
  static std::mutex g_mutex_writes;
  static uint32_t playCount = 0;

  void initAudioBuffer(private_t *tech_pvt) {
    tech_pvt->ws_audio_buffer_write_offset = LWS_PRE;
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) reset write offset to start: %lu\n", 
      tech_pvt->id, tech_pvt->ws_audio_buffer_write_offset);
  }

  switch_status_t fork_data_init(private_t *tech_pvt, switch_core_session_t *session, char * host, 
    unsigned int port, char* path, int sslFlags, int sampling, int desiredSampling, int channels, char* metadata, responseHandler_t responseHandler) {

    int err;
    switch_codec_implementation_t read_impl;
    switch_core_session_get_read_impl(session, &read_impl);
  
    memset(tech_pvt, 0, sizeof(private_t));
  
    strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    tech_pvt->ws_state = LWS_CLIENT_IDLE;
    strncpy(tech_pvt->host, host, MAX_WS_URL_LEN);
    tech_pvt->port = port;
    strncpy(tech_pvt->path, path, MAX_PATH_LEN);
    tech_pvt->sslFlags = sslFlags;
    tech_pvt->wsi = NULL;
    tech_pvt->vhd = NULL;
    tech_pvt->metadata = NULL;
    tech_pvt->sampling = desiredSampling;
    tech_pvt->responseHandler = responseHandler;
    tech_pvt->playout = NULL;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->buffer_overrun_notified = 0;

    tech_pvt->ws_audio_buffer_max_len = LWS_PRE +
      (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD * nAudioBufferSecs);
    tech_pvt->ws_audio_buffer = (uint8_t *) malloc(tech_pvt->ws_audio_buffer_max_len);
    if (nullptr == tech_pvt->ws_audio_buffer) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating audio buffer\n");
      return SWITCH_STATUS_FALSE;
    }
    tech_pvt->ws_audio_buffer_min_freespace = read_impl.decoded_bytes_per_packet;
    initAudioBuffer(tech_pvt);

    switch_mutex_init(&tech_pvt->ws_send_mutex, SWITCH_MUTEX_DEFAULT, switch_core_session_get_pool(session));
    switch_mutex_init(&tech_pvt->ws_recv_mutex, SWITCH_MUTEX_DEFAULT, switch_core_session_get_pool(session));
    switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
    switch_thread_cond_create(&tech_pvt->cond, switch_core_session_get_pool(session));

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

  void destroy_tech_pvt(private_t* tech_pvt) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->sessionId, tech_pvt->id);
    tech_pvt->ws_state = LWS_CLIENT_DISCONNECTED;
    if (tech_pvt->resampler) {
      speex_resampler_destroy(tech_pvt->resampler);
      tech_pvt->resampler = nullptr;
    }
    if (tech_pvt->mutex) {
      switch_mutex_destroy(tech_pvt->mutex);
      tech_pvt->mutex = nullptr;
    }
    if (tech_pvt->cond) {
      switch_thread_cond_destroy(tech_pvt->cond);
      tech_pvt->cond = nullptr;
    }
    tech_pvt->wsi = nullptr;
    if (tech_pvt->ws_audio_buffer) {
      free(tech_pvt->ws_audio_buffer);
      tech_pvt->ws_audio_buffer = nullptr;
      tech_pvt->ws_audio_buffer_max_len = tech_pvt->ws_audio_buffer_write_offset = 0;
    }
  }

	uint32_t bumpPlayCount(void) { return ++playCount; }

  void addPendingConnect(private_t* tech_pvt) {
    std::lock_guard<std::mutex> guard(g_mutex_connects);
    tech_pvt->ws_state = LWS_CLIENT_IDLE;
    pendingConnects.push_back(tech_pvt);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) there are now %lu pending connections\n", tech_pvt->id, pendingConnects.size());
  }

  void addPendingDisconnect(private_t* tech_pvt) {
    std::lock_guard<std::mutex> guard(g_mutex_disconnects);
    tech_pvt->ws_state = LWS_CLIENT_DISCONNECTING;
    pendingDisconnects.push_back(tech_pvt);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) there are now %lu pending disconnects\n", tech_pvt->id, pendingDisconnects.size());
  }

  void addPendingWrite(private_t* tech_pvt) {
    std::lock_guard<std::mutex> guard(g_mutex_writes);
    pendingWrites.push_back(tech_pvt);
  }

  private_t* findAndRemovePendingConnect(struct lws *wsi) {
    private_t* tech_pvt = NULL;
    std::lock_guard<std::mutex> guard(g_mutex_connects);

    for (auto it = pendingConnects.begin(); it != pendingConnects.end() && !tech_pvt; ++it) {
      if ((*it)->ws_state == LWS_CLIENT_CONNECTING && (*it)->wsi == wsi) tech_pvt = *it;
    }

    if (tech_pvt) {
      pendingConnects.remove(tech_pvt);
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) after removing connection there are now %lu pending connections\n", tech_pvt->id, pendingConnects.size());
    }

    return tech_pvt;
  }

  void processIncomingMessage(private_t* tech_pvt, int isBinary) {
    assert(tech_pvt->recv_buf);
    std::string type;

    if (isBinary) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) processIncomingMessage - unexpected binary message, discarding..\n", tech_pvt->id);
      return;
    }
    std::string msg((char *)tech_pvt->recv_buf, tech_pvt->recv_buf_ptr - tech_pvt->recv_buf);
    cJSON* json = parse_json(tech_pvt->sessionId, msg, type) ;
    if (json) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) processIncomingMessage - received %s message\n", tech_pvt->id, type.c_str());
      cJSON* jsonData = cJSON_GetObjectItem(json, "data");
      if (0 == type.compare("playAudio")) {
        if (jsonData) {
          // dont send actual audio bytes in event message
          cJSON* jsonFile = NULL;
          cJSON* jsonAudio = cJSON_DetachItemFromObject(jsonData, "audioContent");
          int validAudio = (jsonAudio && NULL != jsonAudio->valuestring);

          const char* szAudioContentType = cJSON_GetObjectCstr(jsonData, "audioContentType");
          char fileType[6];
          int sampleRate = 16000;
          if (0 == strcmp(szAudioContentType, "raw")) {
            cJSON* jsonSR = cJSON_GetObjectItem(jsonData, "sampleRate");
            sampleRate = jsonSR && jsonSR->valueint ? jsonSR->valueint : 0;

            switch(sampleRate) {
              case 8000:
                strcpy(fileType, ".r8");
                break;
              case 16000:
                strcpy(fileType, ".r16");
                break;
              case 24000:
                strcpy(fileType, ".r24");
                break;
              case 32000:
                strcpy(fileType, ".r32");
                break;
              case 48000:
                strcpy(fileType, ".r48");
                break;
              case 64000:
                strcpy(fileType, ".r64");
                break;
              default:
                strcpy(fileType, ".r16");
                break;
            }
          }
          else if (0 == strcmp(szAudioContentType, ".wave")) {
            strcpy(fileType, "wave");
          }
          else {
            validAudio = 0;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) processIncomingMessage - unsupported audioContentType: %s\n", tech_pvt->id, szAudioContentType);
          }

          if (validAudio) {
            char szFilePath[256];

            std::string rawAudio = drachtio::base64_decode(jsonAudio->valuestring);
            switch_snprintf(szFilePath, 256, "%s%s%s_%d.tmp%s", SWITCH_GLOBAL_dirs.temp_dir, 
              SWITCH_PATH_SEPARATOR, tech_pvt->sessionId, bumpPlayCount(), fileType);
            std::ofstream f(szFilePath, std::ofstream::binary);
            f << rawAudio;
            f.close();

            // add the file to the list of files played for this session, we'll delete when session closes
            struct playout* playout = (struct playout *) malloc(sizeof(struct playout));
            playout->file = (char *) malloc(strlen(szFilePath) + 1);
            strcpy(playout->file, szFilePath);
            playout->next = tech_pvt->playout;
            tech_pvt->playout = playout;

            jsonFile = cJSON_CreateString(szFilePath);
            cJSON_AddItemToObject(jsonData, "file", jsonFile);
          }

          char* jsonString = cJSON_PrintUnformatted(jsonData);
          tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_PLAY_AUDIO, jsonString);
          free(jsonString);
          if (jsonAudio) cJSON_Delete(jsonAudio);
        }
        else {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) processIncomingMessage - missing data payload in playAudio request\n", tech_pvt->id); 
        }
      }
      else if (0 == type.compare("killAudio")) {
        tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_KILL_AUDIO, NULL);

        // kill any current playback on the channel
        switch_core_session_t* session = switch_core_session_locate(tech_pvt->sessionId);
        if (session) {
          switch_channel_t *channel = switch_core_session_get_channel(session);
          switch_channel_set_flag_value(channel, CF_BREAK, 2);
          switch_core_session_rwunlock(session);
        }

      }
      else if (0 == type.compare("transcription")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_TRANSCRIPTION, jsonString);
        free(jsonString);        
      }
      else if (0 == type.compare("transfer")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_TRANSFER, jsonString);
        free(jsonString);                
      }
      else if (0 == type.compare("disconnect")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_DISCONNECT, jsonString);
        free(jsonString);        
      }
      else if (0 == type.compare("error")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_ERROR, jsonString);
        free(jsonString);        
      }
      else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) processIncomingMessage - unsupported msg type %s\n", tech_pvt->id, type.c_str());  
      }
      cJSON_Delete(json);
    }

    delete [] tech_pvt->recv_buf;
    tech_pvt->recv_buf = NULL;
    tech_pvt->recv_buf_ptr = NULL;
  }

  int connect_client(private_t* tech_pvt, struct lws_per_vhost_data *vhd) {
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = vhd->context;
    i.port = tech_pvt->port;
    i.address = tech_pvt->host;
    i.path = tech_pvt->path;
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = tech_pvt->sslFlags;
    i.protocol = mySubProtocolName;
    i.pwsi = &(tech_pvt->wsi);

    tech_pvt->ws_state = LWS_CLIENT_CONNECTING;
    tech_pvt->vhd = vhd;

    switch_core_session_t* session = switch_core_session_locate(tech_pvt->sessionId);
    if (session) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%u) calling lws_client_connect_via_info\n", tech_pvt->id);
      switch_core_session_rwunlock(session);
    }

    if (!lws_client_connect_via_info(&i)) {
      //tech_pvt->ws_state = LWS_CLIENT_IDLE;
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "(%u) lws_client_connect_via_info immediately returned failure\n", tech_pvt->id);
      return 0;
    }

    return 1;
  }

  static int lws_callback(struct lws *wsi, 
    enum lws_callback_reasons reason,
    void *user, void *in, size_t len) {

    struct lws_per_vhost_data *vhd = 
      (struct lws_per_vhost_data *) lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

    struct lws_vhost* vhost = lws_get_vhost(wsi);
  	private_t ** pCb = (private_t **) user;

    switch (reason) {

    case LWS_CALLBACK_PROTOCOL_INIT:
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lws_callback LWS_CALLBACK_PROTOCOL_INIT wsi: %p\n", wsi);
      vhd = (struct lws_per_vhost_data *) lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct lws_per_vhost_data));
      vhd->context = lws_get_context(wsi);
      vhd->protocol = lws_get_protocol(wsi);
      vhd->vhost = lws_get_vhost(wsi);
      break;

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      {        
        // check if we have any new connections requested
        {
          std::lock_guard<std::mutex> guard(g_mutex_connects);
          for (auto it = pendingConnects.begin(); it != pendingConnects.end(); ++it) {
            private_t* tech_pvt = *it;
            if (tech_pvt->ws_state == LWS_CLIENT_IDLE) {
              connect_client(tech_pvt, vhd);
            }
          }
        }

        // process writes
        {
          std::lock_guard<std::mutex> guard(g_mutex_writes);
          for (auto it = pendingWrites.begin(); it != pendingWrites.end(); ++it) {
            private_t* tech_pvt = *it;
            if (tech_pvt && tech_pvt->ws_state == LWS_CLIENT_CONNECTED) lws_callback_on_writable(tech_pvt->wsi);
          }
          pendingWrites.clear();
        }

        // process disconnects
        {
          std::lock_guard<std::mutex> guard(g_mutex_disconnects);
          for (auto it = pendingDisconnects.begin(); it != pendingDisconnects.end(); ++it) {
            private_t* tech_pvt = *it;
            if (tech_pvt && tech_pvt->ws_state == LWS_CLIENT_DISCONNECTING) lws_callback_on_writable(tech_pvt->wsi);
          }
          pendingDisconnects.clear();
        }
      }
      break;

    /* --- client callbacks --- */
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      {
        private_t* tech_pvt = findAndRemovePendingConnect(wsi);
        if (!tech_pvt) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find pending connection for wsi: %p\n", wsi);
        }
        else {
          switch_mutex_lock(tech_pvt->mutex);
          switch_core_session_t* session = switch_core_session_locate(tech_pvt->sessionId);
          if (session) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "lws_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR wsi: %p: %s\n",
              wsi, in ? (char *)in : "(null)");
            switch_core_session_rwunlock(session);
          }
          tech_pvt->ws_state = LWS_CLIENT_FAILED;
          switch_thread_cond_signal(tech_pvt->cond);
          switch_mutex_unlock(tech_pvt->mutex);
        }
      }      
      break;


    case LWS_CALLBACK_CLIENT_ESTABLISHED:

      // remove the associated cb from the pending list and allocate audio ring buffer
      {
        private_t* tech_pvt = findAndRemovePendingConnect(wsi);
        if (!tech_pvt) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "lws_callback LWS_CALLBACK_CLIENT_ESTABLISHED unable to find pending connection for wsi: %p\n", wsi);
        }
        else {
          *pCb = tech_pvt;
          switch_mutex_lock(tech_pvt->mutex);
          tech_pvt->vhd = vhd;
          tech_pvt->ws_state = LWS_CLIENT_CONNECTED;
          switch_thread_cond_signal(tech_pvt->cond);
          switch_mutex_unlock(tech_pvt->mutex);
        }
      }      
      break;

    case LWS_CALLBACK_CLIENT_CLOSED:
      {
        private_t* tech_pvt = *pCb;
        if (tech_pvt && tech_pvt->ws_state == LWS_CLIENT_DISCONNECTING) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) LWS_CALLBACK_CLIENT_CLOSED by us wsi: %p, context: %p, thread: %lu\n", 
            tech_pvt->id, wsi, vhd->context, switch_thread_self());

          switch_mutex_lock(tech_pvt->mutex);
          switch_thread_cond_signal(tech_pvt->cond);
          switch_mutex_unlock(tech_pvt->mutex);
        }
        else if (tech_pvt && tech_pvt->ws_state == LWS_CLIENT_CONNECTED) {
          {
            switch_mutex_lock(tech_pvt->mutex);
            tech_pvt->ws_state = LWS_CLIENT_DISCONNECTED;
            switch_mutex_unlock(tech_pvt->mutex);
          }

          char *p = (char *) "{\"msg\": \"connection closed from far end\"}";
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) LWS_CALLBACK_CLIENT_CLOSED from far end wsi: %p, context: %p, thread: %lu\n", 
            tech_pvt->sessionId, tech_pvt->id, wsi, vhd->context, switch_thread_self());
          tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_MAINTENANCE, p);
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      {
        private_t* tech_pvt = *pCb;
        switch_mutex_lock(tech_pvt->ws_recv_mutex);

        if (lws_is_first_fragment(wsi)) {
          // allocate a buffer for the entire chunk of memory needed
          assert(NULL == tech_pvt->recv_buf);
          size_t bufLen = len + lws_remaining_packet_payload(wsi);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) LWS_CALLBACK_CLIENT_RECEIVE allocating %lu bytes\n", tech_pvt->id, bufLen);
          tech_pvt->recv_buf = new uint8_t[bufLen];
          tech_pvt->recv_buf_ptr = tech_pvt->recv_buf;
        }

        assert(NULL != tech_pvt->recv_buf);
        if (len > 0) {
          // if we got any data, append it to the buffer
          memcpy(tech_pvt->recv_buf_ptr, in, len);
          tech_pvt->recv_buf_ptr += len;
        }

        if (lws_is_final_fragment(wsi)) {
          processIncomingMessage(tech_pvt, lws_frame_is_binary(wsi));
        }
        switch_mutex_unlock(tech_pvt->ws_recv_mutex);
      }
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      {
        private_t* tech_pvt = *pCb;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) LWS_CALLBACK_CLIENT_WRITEABLE\n", tech_pvt->id);

        switch_mutex_lock(tech_pvt->ws_send_mutex);

        // check for text frames to send
        if (tech_pvt->metadata) {          
          int n = tech_pvt->metadata_length - LWS_PRE - 1;
          int m = lws_write(wsi, tech_pvt->metadata + LWS_PRE, n, LWS_WRITE_TEXT);
          delete[] tech_pvt->metadata;
          tech_pvt->metadata = NULL;
          tech_pvt->metadata_length = 0;
          if (m < n) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "(%u) error writing metadata %d requested, %d written\n", tech_pvt->id, n, m);
            switch_mutex_unlock(tech_pvt->ws_send_mutex);
            return -1;
          }
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) wrote %d bytes of text\n", tech_pvt->id, n);

          // there may be audio data, but only one write per writeable event
          // get it next time
          switch_mutex_unlock(tech_pvt->ws_send_mutex);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) LWS_CALLBACK_WRITEABLE sent text frame (%d bytes) wsi: %p\n", tech_pvt->id, n, wsi);
          lws_callback_on_writable(tech_pvt->wsi);

          return 0;
        }
        switch_mutex_unlock(tech_pvt->ws_send_mutex);

        switch_mutex_lock(tech_pvt->mutex);
        if (tech_pvt->ws_state == LWS_CLIENT_DISCONNECTING) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "(%u) lws_callback LWS_CALLBACK_WRITEABLE closing connection wsi: %p\n", tech_pvt->id, wsi);
          lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
          switch_mutex_unlock(tech_pvt->mutex);
          return -1;
        }

        // check for audio packets
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) (lwsthread) offset %lu\n", tech_pvt->id, tech_pvt->ws_audio_buffer_write_offset);

        if (tech_pvt->ws_audio_buffer_write_offset > LWS_PRE) {
          size_t datalen = tech_pvt->ws_audio_buffer_write_offset - LWS_PRE;
          int sent = lws_write(wsi, (unsigned char *) tech_pvt->ws_audio_buffer + LWS_PRE, datalen, LWS_WRITE_BINARY);
          if (sent < datalen) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
            "(%u)  LWS_CALLBACK_WRITEABLE wrote only %u of %lu bytes wsi: %p\n", 
              tech_pvt->id, sent, datalen, wsi);
          }
          initAudioBuffer(tech_pvt);
        }

        switch_mutex_unlock(tech_pvt->mutex);

        return 0;
      }
      break;

    default:
      break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }

  static const struct lws_protocols protocols[] = {
    {
      mySubProtocolName,
      lws_callback,
      sizeof(void *),
      1024,
    },
    { NULL, NULL, 0, 0 }
  };

  void lws_logger(int level, const char *line) {
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;

    switch (level) {
      case LLL_ERR: llevel = SWITCH_LOG_ERROR; break;
      case LLL_WARN: llevel = SWITCH_LOG_WARNING; break;
      case LLL_NOTICE: llevel = SWITCH_LOG_NOTICE; break;
      case LLL_INFO: llevel = SWITCH_LOG_INFO; break;
      break;
    }
	  switch_log_printf(SWITCH_CHANNEL_LOG, llevel, "%s\n", line);

  }

}

extern "C" {
  int parse_ws_uri(const char* szServerUri, char* host, char *path, unsigned int* pPort, int* pSslFlags) {
    int i = 0, offset;
    char server[MAX_WS_URL_LEN];
    

    // get the scheme
    strncpy(server, szServerUri, MAX_WS_URL_LEN);
    if (0 == strncmp(server, "https://", 8) || 0 == strncmp(server, "HTTPS://", 8)) {
      *pSslFlags = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED;
      offset = 8;
      *pPort = 443;
    }
    else if (0 == strncmp(server, "wss://", 6) || 0 == strncmp(server, "WSS://", 6)) {
      *pSslFlags = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED;
      offset = 6;
      *pPort = 443;
    }
    else if (0 == strncmp(server, "http://", 7) || 0 == strncmp(server, "HTTP://", 7)) {
      offset = 7;
      *pSslFlags = 0;
      *pPort = 80;
    }
    else if (0 == strncmp(server, "ws://", 5) || 0 == strncmp(server, "WS://", 5)) {
      offset = 5;
      *pSslFlags = 0;
      *pPort = 80;
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "parse_ws_uri - error parsing uri %s: invalid scheme\n", szServerUri);;
      return 0;
    }

    // parse host, port and path
    strcpy(path, "/");
    char *p = server + offset;
    char *pch = strtok(p, ":/");
    while (pch) {
      if (0 == i++) strncpy(host, pch, MAX_WS_URL_LEN);
      else {
        bool isdigits = true;
        int idx = 0;
        while (*(pch+idx) && isdigits) isdigits = isdigit(pch[idx++]);
        if (isdigits) *pPort = atoi(pch);
        else strncpy(path + 1, pch, MAX_PATH_LEN);
      }
      pch = strtok(NULL, ";/");
    }

    return 1;
  }

  switch_status_t fork_init() {
  return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_cleanup() {
      return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_init(switch_core_session_t *session, 
              responseHandler_t responseHandler,
              uint32_t samples_per_second, 
              char *host,
              unsigned int port,
              char *path,
              int sampling,
              int sslFlags,
              int channels,
              char* metadata, 
              void **ppUserData)
  {    	
    int err;

    // allocate per-session data structure
    private_t* tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));
    if (!tech_pvt) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
      return SWITCH_STATUS_FALSE;
    }
    if (SWITCH_STATUS_SUCCESS != fork_data_init(tech_pvt, session, host, port, path, sslFlags, samples_per_second, sampling, channels, metadata, responseHandler)) {
      destroy_tech_pvt(tech_pvt);
      return SWITCH_STATUS_FALSE;
    }

    // now try to connect
    unsigned int nSelectedServiceThread = tech_pvt->id % nServiceThreads;
    switch_mutex_lock(tech_pvt->mutex);
    addPendingConnect(tech_pvt);
    lws_cancel_service(context[nSelectedServiceThread]);
    switch_thread_cond_wait(tech_pvt->cond, tech_pvt->mutex);

    if (tech_pvt->ws_state == LWS_CLIENT_FAILED) {
      switch_mutex_unlock(tech_pvt->mutex);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) failed connecting to host %s\n", tech_pvt->id, host);
      destroy_tech_pvt(tech_pvt);
      return SWITCH_STATUS_FALSE;
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%u) successfully connected to host %s\n", tech_pvt->id, host);

    // write initial metadata
    if (metadata) {
      tech_pvt->metadata_length = strlen(metadata) + 1 + LWS_PRE;
      tech_pvt->metadata = new uint8_t[tech_pvt->metadata_length];
      memset(tech_pvt->metadata, 0, tech_pvt->metadata_length);
      memcpy(tech_pvt->metadata + LWS_PRE, metadata, strlen(metadata));
      addPendingWrite(tech_pvt);
      lws_cancel_service(tech_pvt->vhd->context);
    }
    switch_mutex_unlock(tech_pvt->mutex);

    *ppUserData = tech_pvt;
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_cleanup(switch_core_session_t *session, char* text, int channelIsClosing) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "fork_session_cleanup: no bug - websocket conection already closed\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    uint32_t id = tech_pvt->id;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_session_cleanup\n", id);

    if (!tech_pvt || !tech_pvt->wsi) return SWITCH_STATUS_FALSE;
      
    switch_mutex_lock(tech_pvt->mutex);

    // get the bug again, now that we are under lock
    {
      switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
      if (bug) {
        switch_channel_set_private(channel, MY_BUG_NAME, NULL);
        if (!channelIsClosing) {
          switch_core_media_bug_remove(session, &bug);
        }
      }
    }

    // delete any temp files
    struct playout* playout = tech_pvt->playout;
    while (playout) {
      std::remove(playout->file);
      free(playout->file);
      struct playout *tmp = playout;
      playout = playout->next;
      free(tmp);
    }

    if (tech_pvt->ws_state != LWS_CLIENT_CONNECTED) {
      switch_mutex_unlock(tech_pvt->mutex);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%u) fork_session_cleanup no need to close socket because ws state is %d\n", tech_pvt->id, tech_pvt->ws_state);
      return SWITCH_STATUS_FALSE;
    }
    else {
      if (text) {
        tech_pvt->metadata_length = strlen(text) + 1 + LWS_PRE;
        tech_pvt->metadata = new uint8_t[tech_pvt->metadata_length];
        memset(tech_pvt->metadata, 0, tech_pvt->metadata_length);
        memcpy(tech_pvt->metadata + LWS_PRE, text, strlen(text));
        addPendingWrite(tech_pvt);
      }

      addPendingDisconnect(tech_pvt);
      lws_cancel_service(tech_pvt->vhd->context);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) waiting to complete ws teardown\n", id);

      // wait for disconnect to complete
      switch_thread_cond_wait(tech_pvt->cond, tech_pvt->mutex);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) teardown completed\n", id);

      switch_mutex_unlock(tech_pvt->mutex);
      destroy_tech_pvt(tech_pvt);

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%u) fork_session_cleanup: connection closed\n", id);
    }
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_send_text(switch_core_session_t *session, char* text) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "fork_session_send_text failed because no bug\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
  
    if (!tech_pvt || !tech_pvt->wsi) return SWITCH_STATUS_FALSE;
      
    switch_mutex_lock(tech_pvt->ws_send_mutex);
    if (tech_pvt->ws_state != LWS_CLIENT_CONNECTED) {
      switch_mutex_unlock(tech_pvt->ws_send_mutex);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "(%u) fork_session_send_text failed because ws state is %d\n", tech_pvt->id, tech_pvt->ws_state);
      return SWITCH_STATUS_FALSE;
    }
    else {
      tech_pvt->metadata_length = strlen(text) + 1 + LWS_PRE;
      tech_pvt->metadata = new uint8_t[tech_pvt->metadata_length];
      memset(tech_pvt->metadata, 0, tech_pvt->metadata_length);
      memcpy(tech_pvt->metadata + LWS_PRE, text, strlen(text));

      addPendingWrite(tech_pvt);
      lws_cancel_service(tech_pvt->vhd->context);
      switch_mutex_unlock(tech_pvt->ws_send_mutex);
    }
    return SWITCH_STATUS_SUCCESS;
  }

  switch_bool_t fork_frame(switch_core_session_t *session, switch_media_bug_t *bug) {
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    size_t inuse = 0;
    bool dirty = false;
    char *p = (char *) "{\"msg\": \"buffer overrun\"}";

    if (!tech_pvt || !tech_pvt->wsi) return SWITCH_FALSE;
    
    if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
      size_t available = tech_pvt->ws_audio_buffer_max_len - tech_pvt->ws_audio_buffer_write_offset;
      if (tech_pvt->ws_state != LWS_CLIENT_CONNECTED) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }
      else if (available < tech_pvt->ws_audio_buffer_min_freespace) {
        if (!tech_pvt->buffer_overrun_notified) {
          tech_pvt->buffer_overrun_notified = 1;
          switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets! write offset %lu available %lu\n", 
            tech_pvt->id, tech_pvt->ws_audio_buffer_write_offset, available);
          tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_MAINTENANCE, p);
        }
      }
      else if (NULL == tech_pvt->resampler) {
        switch_frame_t frame = { 0 };
        frame.data = (char *) tech_pvt->ws_audio_buffer + tech_pvt->ws_audio_buffer_write_offset;
        frame.buflen = available;
        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
          if (frame.datalen) {
            tech_pvt->ws_audio_buffer_write_offset += frame.datalen;
            available -= frame.datalen;
            frame.data = (char *) tech_pvt->ws_audio_buffer + tech_pvt->ws_audio_buffer_write_offset;
            frame.buflen = available;
            dirty = true;
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) (rtpthread) wrote %u bytes, write offset now %lu available %lu\n", 
              tech_pvt->id, frame.datalen, tech_pvt->ws_audio_buffer_write_offset, available);
          }
          else {
            if (!tech_pvt->buffer_overrun_notified) {
              tech_pvt->buffer_overrun_notified = 1;
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropped packet! write offset %lu available %lu\n", 
                tech_pvt->id, tech_pvt->ws_audio_buffer_write_offset, available);
              tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_MAINTENANCE, p);
            }
            break;
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
              (spx_int16_t *) ((char *) tech_pvt->ws_audio_buffer + tech_pvt->ws_audio_buffer_write_offset),
              &out_len);

            if (out_len > 0) {
              // bytes written = num samples * 2 * num channels
              size_t bytes_written = out_len << tech_pvt->channels;
              tech_pvt->ws_audio_buffer_write_offset += bytes_written ;
              available -= bytes_written;
              dirty = true;
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) (rtpthread) wrote %lu bytes, write offset now %lu available %lu\n", 
                tech_pvt->id, bytes_written, tech_pvt->ws_audio_buffer_write_offset, available);
            }
            if (available < tech_pvt->ws_audio_buffer_min_freespace) {
              if (!tech_pvt->buffer_overrun_notified) {
                tech_pvt->buffer_overrun_notified = 1;
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packet! write offset %lu available %lu\n", 
                  tech_pvt->id, tech_pvt->ws_audio_buffer_write_offset, available);
                tech_pvt->responseHandler(tech_pvt->sessionId, EVENT_MAINTENANCE, p);
              }
              break;
            }
          }
        }
      }

      if (dirty) {
        addPendingWrite(tech_pvt);
        lws_cancel_service(tech_pvt->vhd->context);
      }
      switch_mutex_unlock(tech_pvt->mutex);
    }
    return SWITCH_TRUE;
  }

  void service_thread(unsigned int nServiceThread, int *pRunning) {
    struct lws_context_creation_info info;

    memset(&info, 0, sizeof info); 
    info.port = CONTEXT_PORT_NO_LISTEN; 
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    context[nServiceThread] = lws_create_context(&info);
    if (!context[nServiceThread]) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mod_audio_fork: lws_create_context failed\n");
      return;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: successfully created lws context in thread %lu\n", 
      switch_thread_self());

    int n;
    do {
      n = lws_service(context[nServiceThread], WS_TIMEOUT_MS);
    } while (n >= 0 && *pRunning);

    lws_context_destroy(context[nServiceThread]);
  }

  switch_status_t fork_service_threads(int *pRunning) {
    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE ;
      //LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;
    lws_set_log_level(logs, lws_logger);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: starting %u service threads\n", nServiceThreads);
    for (unsigned int i = 0; i < nServiceThreads; i++) {
      std::thread t(service_thread, i, pRunning);
      t.detach();
    }


    return SWITCH_STATUS_FALSE;
  }

}

