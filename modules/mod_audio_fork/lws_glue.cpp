#include <switch.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <list>
#include <algorithm>
#include <condition_variable>
#include <cassert>
#include <cstdlib>
#include <fstream>

#include "base64.hpp"
#include "parser.hpp"
#include "mod_audio_fork.h"

// buffer at most 2 secs of audio (at 20 ms packetization)
#define MAX_BUFFERED_MSGS (100)

namespace {
  static const char* mySubProtocolName = std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") ?
    std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") : "audiostream.drachtio.org";
  static int interrupted = 0;
  static struct lws_context *context = NULL;
  static std::list<struct cap_cb *> pendingConnects;
  static std::list<struct cap_cb *> pendingDisconnects;
  static std::list<struct cap_cb *> pendingWrites;
  static std::mutex g_mutex_connects;
  static std::mutex g_mutex_disconnects;
  static std::mutex g_mutex_writes;
  static uint32_t playCount = 0;

	uint32_t bumpPlayCount(void) { return ++playCount; }

  void bufInit(struct cap_cb* cb) {
    cb->buf_head = &cb->audio_buffer[0] + LWS_PRE;
  }
  uint8_t* bufGetWriteHead(struct cap_cb* cb) {
    return cb->buf_head;
  }
  uint8_t* bufGetReadHead(struct cap_cb* cb) {
    return &cb->audio_buffer[0] + LWS_PRE;
  }
  size_t bufGetAvailable(struct cap_cb* cb) {
    uint8_t* pEnd = &cb->audio_buffer[0] + sizeof(cb->audio_buffer);
    assert(cb->buf_head <= pEnd);
    return pEnd - cb->buf_head;
  }
  size_t bufGetUsed(struct cap_cb* cb) {
    return cb->buf_head - &cb->audio_buffer[0] - LWS_PRE;
  }
  uint8_t* bufBumpWriteHead(struct cap_cb* cb, spx_uint32_t len) {
    cb->buf_head += len;
    assert(cb->buf_head <= &cb->audio_buffer[0] + sizeof(cb->audio_buffer));
  }

  void addPendingConnect(struct cap_cb* cb) {
    std::lock_guard<std::mutex> guard(g_mutex_connects);
    cb->state = LWS_CLIENT_IDLE;
    pendingConnects.push_back(cb);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "addPendingConnect - after adding there are now %d pending\n", pendingConnects.size());
  }

  void addPendingDisconnect(struct cap_cb* cb) {
    std::lock_guard<std::mutex> guard(g_mutex_disconnects);
    cb->state = LWS_CLIENT_DISCONNECTING;
    pendingDisconnects.push_back(cb);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "addPendingDisconnect - after adding there are now %d pending\n", pendingDisconnects.size());
  }

  void addPendingWrite(struct cap_cb* cb) {
    std::lock_guard<std::mutex> guard(g_mutex_writes);
    pendingWrites.push_back(cb);
  }

  struct cap_cb* findAndRemovePendingConnect(struct lws *wsi) {
    struct cap_cb* cb = NULL;
    std::lock_guard<std::mutex> guard(g_mutex_connects);

    for (auto it = pendingConnects.begin(); it != pendingConnects.end() && !cb; ++it) {
      if ((*it)->state == LWS_CLIENT_CONNECTING && (*it)->wsi == wsi) cb = *it;
    }

    if (cb) pendingConnects.remove(cb);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "findAndRemovePendingConnect - after removing there are now %d pending\n", pendingConnects.size());

    return cb;
  }

  void destroy_cb(struct cap_cb *cb) {
    speex_resampler_destroy(cb->resampler);
    switch_mutex_destroy(cb->mutex);
    switch_thread_cond_destroy(cb->cond);
    cb->wsi = NULL;
    cb->state = LWS_CLIENT_DISCONNECTED;
  }

  void processIncomingMessage(struct cap_cb* cb, int isBinary) {
    assert(cb->recv_buf);
    std::string type;

    if (isBinary) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "processIncomingMessage - unexpected binary message, discarding..\n");
      return;
    }
    std::string msg((char *)cb->recv_buf, cb->recv_buf_ptr - cb->recv_buf);
    cJSON* json = parse_json(cb->sessionId, msg, type) ;
    if (json) {
      cJSON* jsonData = cJSON_GetObjectItem(json, "data");
      if (jsonData) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "processIncomingMessage - received %s message\n", type.c_str());
        if (0 == type.compare("ivrAudio")) {
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
              switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "processIncomingMessage - unsupported audioContentType: %s\n", 
                szAudioContentType);
            }

            if (validAudio) {
              char szFilePath[256];

              std::string rawAudio = drachtio::base64_decode(jsonAudio->valuestring);
              switch_snprintf(szFilePath, 256, "%s%s%s_%d.tmp%s", SWITCH_GLOBAL_dirs.temp_dir, 
                SWITCH_PATH_SEPARATOR, cb->sessionId, bumpPlayCount(), fileType);
              std::ofstream f(szFilePath, std::ofstream::binary);
              f << rawAudio;
              f.close();

              // add the file to the list of files played for this session, we'll delete when session closes
              struct playout* playout = (struct playout *) malloc(sizeof(struct playout));
              playout->file = (char *) malloc(strlen(szFilePath) + 1);
              strcpy(playout->file, szFilePath);
              playout->next = cb->playout;
              cb->playout = playout;

              jsonFile = cJSON_CreateString(szFilePath);
              cJSON_AddItemToObject(jsonData, "file", jsonFile);
            }

            char* jsonString = cJSON_PrintUnformatted(jsonData);
            cb->responseHandler(cb->sessionId, EVENT_AUDIO, jsonString);
            free(jsonString);
            if (jsonAudio) cJSON_Delete(jsonAudio);
          }
        }
        else if (0 == type.compare("transcription")) {
          char* jsonString = cJSON_PrintUnformatted(jsonData);
          cb->responseHandler(cb->sessionId, EVENT_TRANSCRIPTION, jsonString);
          free(jsonString);        
        }
        else if (0 == type.compare("transfer")) {
          char* jsonString = cJSON_PrintUnformatted(jsonData);
          cb->responseHandler(cb->sessionId, EVENT_TRANSFER, jsonString);
          free(jsonString);                
        }
        else if (0 == type.compare("disconnect")) {
          char* jsonString = cJSON_PrintUnformatted(jsonData);
          cb->responseHandler(cb->sessionId, EVENT_DISCONNECT, jsonString);
          free(jsonString);        
        }
        else if (0 == type.compare("error")) {
          char* jsonString = cJSON_PrintUnformatted(jsonData);
          cb->responseHandler(cb->sessionId, EVENT_ERROR, jsonString);
          free(jsonString);        
        }
        else {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "processIncomingMessage - unsupported msg type %s\n", type.c_str());  
        }
      }
      else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "processIncomingMessage - missing 'data' property\n", type.c_str());  
      }
      cJSON_Delete(json);
    }

    delete [] cb->recv_buf;
    cb->recv_buf = NULL;
    cb->recv_buf_ptr = NULL;
  }

  int connect_client(struct cap_cb* cb, struct lws_per_vhost_data *vhd) {
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = vhd->context;
    i.port = cb->port;
    i.address = cb->host;
    i.path = cb->path;
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = cb->sslFlags;
    i.protocol = mySubProtocolName;
    i.pwsi = &(cb->wsi);

    cb->state = LWS_CLIENT_CONNECTING;
    cb->vhd = vhd;

    if (!lws_client_connect_via_info(&i)) {
      //cb->state = LWS_CLIENT_IDLE;
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
  	struct cap_cb ** pCb = (struct cap_cb **) user;

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
            struct cap_cb* cb = *it;
            if (cb->state == LWS_CLIENT_IDLE) {
              connect_client(cb, vhd);
            }
          }
        }

        // process writes
        {
          std::lock_guard<std::mutex> guard(g_mutex_writes);
          for (auto it = pendingWrites.begin(); it != pendingWrites.end(); ++it) {
            struct cap_cb* cb = *it;
            lws_callback_on_writable(cb->wsi);
          }
          pendingWrites.clear();
        }

        // process disconnects
        {
          std::lock_guard<std::mutex> guard(g_mutex_disconnects);
          for (auto it = pendingDisconnects.begin(); it != pendingDisconnects.end(); ++it) {
            struct cap_cb* cb = *it;
            lws_callback_on_writable(cb->wsi);
          }
          pendingDisconnects.clear();
        }
      }
      break;

    /* --- client callbacks --- */
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "lws_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR wsi: %p\n", wsi);
      {
        struct cap_cb* my_cb = findAndRemovePendingConnect(wsi);
        if (!my_cb) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "lws_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find pending connection for wsi: %p\n", wsi);
        }
        else {
          struct cap_cb *cb = *pCb = my_cb;
          switch_mutex_lock(cb->mutex);
          cb->state = LWS_CLIENT_FAILED;
          switch_thread_cond_signal(cb->cond);
          switch_mutex_unlock(cb->mutex);
        }
      }      
      break;


    case LWS_CALLBACK_CLIENT_ESTABLISHED:

      // remove the associated cb from the pending list and allocate audio ring buffer
      {
        struct cap_cb* my_cb = findAndRemovePendingConnect(wsi);
        if (!my_cb) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "lws_callback LWS_CALLBACK_CLIENT_ESTABLISHED unable to find pending connection for wsi: %p\n", wsi);
        }
        else {
          struct cap_cb *cb = *pCb = my_cb;
          switch_mutex_lock(cb->mutex);
          cb->vhd = vhd;
          cb->state = LWS_CLIENT_CONNECTED;
          switch_thread_cond_signal(cb->cond);
          switch_mutex_unlock(cb->mutex);
        }
      }      
      break;

    case LWS_CALLBACK_CLIENT_CLOSED:
      {
        struct cap_cb *cb = *pCb;
        if (cb && cb->state == LWS_CLIENT_DISCONNECTING) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lws_callback LWS_CALLBACK_CLIENT_CLOSED by us wsi: %p\n", wsi);
          destroy_cb(cb);
        }
        else if (cb && cb->state == LWS_CLIENT_CONNECTED) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lws_callback LWS_CALLBACK_CLIENT_CLOSED from far end wsi: %p\n", wsi);
          destroy_cb(cb);
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      {
        struct cap_cb *cb = *pCb;
        switch_mutex_lock(cb->mutex);

        if (lws_is_first_fragment(wsi)) {
          // allocate a buffer for the entire chunk of memory needed
          assert(NULL == cb->recv_buf);
          size_t bufLen = len + lws_remaining_packet_payload(wsi);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "LWS_CALLBACK_CLIENT_RECEIVE allocating %d bytes\n", bufLen);
          cb->recv_buf = new uint8_t[bufLen];
          cb->recv_buf_ptr = cb->recv_buf;
        }

        assert(NULL != cb->recv_buf);
        if (len > 0) {
          // if we got any data, append it to the buffer
          memcpy(cb->recv_buf_ptr, in, len);
          cb->recv_buf_ptr += len;
        }

        if (lws_is_final_fragment(wsi)) {
          processIncomingMessage(cb, lws_frame_is_binary(wsi));
        }
        switch_mutex_unlock(cb->mutex);
      }
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      {
        struct cap_cb *cb = *pCb;

        switch_mutex_lock(cb->mutex);

        // check for text frames to send
        if (cb->metadata) {          
          int n = cb->metadata_length - LWS_PRE - 1;
          int m = lws_write(wsi, cb->metadata + LWS_PRE, n, LWS_WRITE_TEXT);
          delete[] cb->metadata;
          cb->metadata = NULL;
          cb->metadata_length = 0;
          if (m < n) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error writing metadata %d requested, %d written\n", n, m);
            switch_mutex_unlock(cb->mutex);
            return -1;
          }
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "wrote %d bytes of text\n", n);

          // there may be audio data, but only one write per writeable event
          // get it next time
          switch_mutex_unlock(cb->mutex);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "lws_callback LWS_CALLBACK_WRITEABLE sent text frame (%d bytes) wsi: %p\n", wsi, n);
          lws_callback_on_writable(cb->wsi);

          return 0;
        }

        if (cb->state == LWS_CLIENT_DISCONNECTING) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "lws_callback LWS_CALLBACK_WRITEABLE closing connection wsi: %p\n", wsi);
          lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
          switch_mutex_unlock(cb->mutex);
          return -1;
        }

        // check for audio packets
        int n = bufGetUsed(cb);
        if (n > 0) {
          int m = lws_write(wsi, bufGetReadHead(cb), n, LWS_WRITE_BINARY);
          if (m < n) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error writing audio data %d requested, %d written\n", n, m);
            switch_mutex_unlock(cb->mutex);
            return -1;
          }
          bufInit(cb);
        }
        switch_mutex_unlock(cb->mutex);

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
      0,
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
    switch_channel_t *channel = switch_core_session_get_channel(session);
    struct cap_cb *cb;
    int err;

    // allocate per-session data structure
    cb = (struct cap_cb *) switch_core_session_alloc(session, sizeof(struct cap_cb));
    memset(cb, 0, sizeof(struct cap_cb));
    cb->base = switch_core_session_strdup(session, "mod_audio_fork");
    strncpy(cb->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    cb->state = LWS_CLIENT_IDLE;
    strncpy(cb->host, host, MAX_WS_URL_LEN);
    cb->port = port;
    strncpy(cb->path, path, MAX_PATH_LEN);
    cb->sslFlags = sslFlags;
    cb->wsi = NULL;
    cb->vhd = NULL;
    cb->metadata = NULL;
    cb->sampling = sampling;
    cb->responseHandler = responseHandler;
    cb->playout = NULL;
    bufInit(cb);

    switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
    switch_thread_cond_create(&cb->cond, switch_core_session_get_pool(session));

    cb->resampler = speex_resampler_init(channels, 8000, sampling, SWITCH_RESAMPLE_QUALITY, &err);

    if (0 != err) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n", 
        switch_channel_get_name(channel), speex_resampler_strerror(err));
      return SWITCH_STATUS_FALSE;
    }

    // now try to connect
    switch_mutex_lock(cb->mutex);
    addPendingConnect(cb);
    lws_cancel_service(context);
    switch_thread_cond_wait(cb->cond, cb->mutex);

    if (cb->state == LWS_CLIENT_FAILED) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: failed connecting to host %s\n", 
        switch_channel_get_name(channel), host);
      switch_mutex_unlock(cb->mutex);
      destroy_cb(cb);
      return SWITCH_STATUS_FALSE;
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s: successfully connected to host %s\n", 
        switch_channel_get_name(channel), host);

    // write initial metadata
    if (metadata) {
      cb->metadata_length = strlen(metadata) + 1 + LWS_PRE;
      cb->metadata = new uint8_t[cb->metadata_length];
      memset(cb->metadata, 0, cb->metadata_length);
      memcpy(cb->metadata + LWS_PRE, metadata, strlen(metadata));
      addPendingWrite(cb);
      lws_cancel_service(cb->vhd->context);
    }
    switch_mutex_unlock(cb->mutex);


    *ppUserData = cb;
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_cleanup(switch_core_session_t *session, char* text) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

    if (bug) {
      struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
      switch_channel_set_private(channel, MY_BUG_NAME, NULL);
      if (cb->wsi) {
        switch_mutex_lock(cb->mutex);
        addPendingDisconnect(cb);

        if (text) {
          cb->metadata_length = strlen(text) + 1 + LWS_PRE;
          cb->metadata = new uint8_t[cb->metadata_length];
          memset(cb->metadata, 0, cb->metadata_length);
          memcpy(cb->metadata + LWS_PRE, text, strlen(text));
          addPendingWrite(cb);
        }

        struct playout* playout = cb->playout;
        while (playout) {
          std::remove(playout->file);
          free(playout->file);
          struct playout *tmp = playout;
          playout = playout->next;
          free(tmp);
        }

        lws_cancel_service(cb->vhd->context);
        switch_mutex_unlock(cb->mutex);
      }

      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "fork_session_cleanup: Closed stream\n");
      return SWITCH_STATUS_SUCCESS; 
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
    return SWITCH_STATUS_FALSE;
  }

  switch_status_t fork_session_send_text(switch_core_session_t *session, char* text) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

    if (bug) {
      struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
      if (cb->wsi) {
        switch_mutex_lock(cb->mutex);

        cb->metadata_length = strlen(text) + 1 + LWS_PRE;
        cb->metadata = new uint8_t[cb->metadata_length];
        memset(cb->metadata, 0, cb->metadata_length);
        memcpy(cb->metadata + LWS_PRE, text, strlen(text));

        switch_mutex_unlock(cb->mutex);

        addPendingWrite(cb);
        lws_cancel_service(cb->vhd->context);
      }

      return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }

  switch_bool_t fork_frame(switch_media_bug_t *bug, void* user_data) {
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    struct cap_cb *cb = (struct cap_cb *) user_data;
    bool written = false;
    int channels = switch_core_media_bug_test_flag(bug, SMBF_STEREO) ? 2 : 1;

    if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
      uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
      switch_frame_t frame = { 0 };
      frame.data = data;
      frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

      while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
        if (frame.datalen) {
          size_t n = bufGetAvailable(cb) >> 1;  // divide by 2 to num of uint16_t spaces available
          if (n  > frame.samples) {
            spx_uint32_t out_len = n;
            spx_uint32_t in_len = frame.samples;
            
            speex_resampler_process_interleaved_int(cb->resampler, 
              (const spx_int16_t *) frame.data, 
              (spx_uint32_t *) &in_len, 
              (spx_int16_t *) bufGetWriteHead(cb),
              &out_len);

             // i.e., if we wrote 320 16bit items then we need to increment 320*2 bytes in single-channel mode, twice that in dual channel  
            bufBumpWriteHead(cb, out_len << channels);        
            written = true;
          }
          else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Dropping packet.\n");
          }
        }
      }
      switch_mutex_unlock(cb->mutex);
    }

    if (written) {
      addPendingWrite(cb);
      lws_cancel_service(cb->vhd->context);
    }

    return SWITCH_TRUE;
  }

  switch_status_t fork_service_thread(int *pRunning) {
    struct lws_context_creation_info info;
    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE ;
      //LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;

    lws_set_log_level(logs, lws_logger);

    memset(&info, 0, sizeof info); 
    info.port = CONTEXT_PORT_NO_LISTEN; 
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    context = lws_create_context(&info);
    if (!context) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "mod_audio_fork: lws_create_context failed\n");
      return SWITCH_STATUS_FALSE;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: successfully created lws context\n");

    int n;
    do {
      n = lws_service(context, 500);
    } while (n >= 0 && *pRunning);

    lws_context_destroy(context);

    return SWITCH_STATUS_FALSE;
  }

}

