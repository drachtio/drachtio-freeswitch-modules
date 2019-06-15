#include <unistd.h>
#include <string>
#include <mutex>
#include <thread>
#include <algorithm>
#include <list>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string.h>
#include <libwebsockets.h>

#include <switch.h>
#include <switch_json.h>

#include "base64.hpp"
#include "parser.hpp"
#include "lws_glue.h"
#include "mod_audio_fork.h"
#include "session_handler.hpp"

struct lws_per_vhost_data {
  struct lws_context *context;
  struct lws_vhost *vhost;
  const struct lws_protocols *protocol;
};

namespace {

  static bool running = true;

  // environment variables to override defaults
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 1, 10));
  static const char* mySubProtocolName = std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") ? std::getenv("MOD_AUDIO_FORK_SUBPROTOCOL_NAME") : "audiostream.drachtio.org";
  static int loglevel = std::getenv("MOD_AUDIO_FORK_LWS_LOGLEVEL") ? atoi(std::getenv("MOD_AUDIO_FORK_LWS_LOGLEVEL")) : 7777 /*(LLL_ERR | LLL_WARN | LLL_NOTICE)*/ ;

  static int interrupted = 0;
  static struct lws_context *context[10] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
  static std::deque<std::thread> lwsContextThreads;

  static unsigned int idxCallCount = 0;
  static std::list< std::weak_ptr<SessionHandler> > pendingConnects;
  static std::list< std::weak_ptr<SessionHandler> > pendingDisconnects;
  static std::list< std::weak_ptr<SessionHandler> > pendingWrites;
  static std::mutex g_mutex_connects;
  static std::mutex g_mutex_disconnects;
  static std::mutex g_mutex_writes;
  static uint32_t playCount = 0;
  static responseHandler_t responseHandler = nullptr;

	uint32_t bumpPlayCount(void) { return ++playCount; }

  void addPendingConnect(std::shared_ptr<SessionHandler> p) {
    std::lock_guard<std::mutex> guard(g_mutex_connects);
    pendingConnects.push_back(p);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "addPendingConnect - after adding there are now %lu pending\n", pendingConnects.size());
  }

  void addPendingDisconnect(std::shared_ptr<SessionHandler> p) {
    std::lock_guard<std::mutex> guard(g_mutex_disconnects);
    p->setState(LWS_CLIENT_DISCONNECTING);
    pendingDisconnects.push_back(p);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "addPendingDisconnect - after adding there are now %lu pending\n", pendingDisconnects.size());
  }

  void addPendingWrite(std::shared_ptr<SessionHandler> p) {
    std::lock_guard<std::mutex> guard(g_mutex_writes);
    pendingWrites.push_back(p);
  }

  std::shared_ptr<SessionHandler> findAndRemovePendingConnect(struct lws *wsi) {
    std::shared_ptr<SessionHandler> ret;
    std::lock_guard<std::mutex> guard(g_mutex_connects);

    std::list< std::weak_ptr<SessionHandler> >::iterator i = pendingConnects.begin();
    while (i != pendingConnects.end()) {
      std::shared_ptr<SessionHandler> p = (*i).lock();
      if (!p) pendingConnects.erase(i++);
      else if (p->inState(LWS_CLIENT_CONNECTING) && p->hasWsi(wsi)) {
        ret = p;
        pendingConnects.erase(i++);
        break;
      }
      else {
        i++;
      }
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "findAndRemovePendingConnect - after removing there are now %lu pending\n", pendingConnects.size());

    return ret;
  }

  void processIncomingMessage(std::shared_ptr<SessionHandler> p, std::string& msg) {
    std::string type;
    std::string sessionId = p->getSessionId();
    cJSON* json = parse_json(sessionId.c_str(), msg, type) ;
    if (json) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "processIncomingMessage - received %s message\n", type.c_str());
      cJSON* jsonData = cJSON_GetObjectItem(json, "data");
      if (0 == type.compare("playAudio")) {
        if (jsonData) {
          // we don't send the actual audio bytes in event message
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
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "processIncomingMessage - unsupported audioContentType: %s\n", szAudioContentType);
          }

          if (validAudio) {
            char szFilePath[256];

            std::string rawAudio = drachtio::base64_decode(jsonAudio->valuestring);
            switch_snprintf(szFilePath, 256, "%s%s%s_%d.tmp%s", SWITCH_GLOBAL_dirs.temp_dir, 
              SWITCH_PATH_SEPARATOR, sessionId.c_str(), bumpPlayCount(), fileType);
            std::ofstream f(szFilePath, std::ofstream::binary);
            f << rawAudio;
            f.close();
            p->addFile(szFilePath);
            jsonFile = cJSON_CreateString(szFilePath);
            cJSON_AddItemToObject(jsonData, "file", jsonFile);
          }

          char* jsonString = cJSON_PrintUnformatted(jsonData);
          responseHandler(sessionId.c_str(), EVENT_PLAY_AUDIO, jsonString);
          free(jsonString);
          if (jsonAudio) cJSON_Delete(jsonAudio);
        }
        else {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "processIncomingMessage - missing data payload in playAudio request\n"); 
        }
      }
      else if (0 == type.compare("killAudio")) {
        responseHandler(sessionId.c_str(), EVENT_KILL_AUDIO, NULL);

        // kill any current playback on the channel
        switch_core_session_t* session = switch_core_session_locate(sessionId.c_str());
        if (session) {
          switch_channel_t *channel = switch_core_session_get_channel(session);
          switch_channel_set_flag_value(channel, CF_BREAK, 2);
          switch_core_session_rwunlock(session);
        }

      }
      else if (0 == type.compare("transcription")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        responseHandler(sessionId.c_str(), EVENT_TRANSCRIPTION, jsonString);
        free(jsonString);        
      }
      else if (0 == type.compare("transfer")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        responseHandler(sessionId.c_str(), EVENT_TRANSFER, jsonString);
        free(jsonString);                
      }
      else if (0 == type.compare("disconnect")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        responseHandler(sessionId.c_str(), EVENT_DISCONNECT, jsonString);
        free(jsonString);        
      }
      else if (0 == type.compare("error")) {
        char* jsonString = cJSON_PrintUnformatted(jsonData);
        responseHandler(sessionId.c_str(), EVENT_ERROR, jsonString);
        free(jsonString);        
      }
      else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "processIncomingMessage - unsupported msg type %s\n", type.c_str());  
      }
      cJSON_Delete(json);
    }
  }

  int connect_client(std::shared_ptr<SessionHandler> p, struct lws_per_vhost_data *vhd) {
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = vhd->context;
    i.port = p->getPort();
    i.address = p->getHost();
    i.path = p->getPath();
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = p->getSslFlags();
    i.protocol = mySubProtocolName;
    i.pwsi = p->getPointerToWsi();

    p->setState(LWS_CLIENT_CONNECTING);
    p->setVhd(vhd);

    if (!lws_client_connect_via_info(&i)) {
      return 0;
    }

    return 1;
  }

  static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    struct lws_per_vhost_data *vhd = (struct lws_per_vhost_data *) lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    struct lws_vhost* vhost = lws_get_vhost(wsi);

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
          std::list< std::weak_ptr<SessionHandler> >::iterator i = pendingConnects.begin();
          while (i != pendingConnects.end()) {
            std::shared_ptr<SessionHandler> p = (*i).lock();
            if (!p) pendingConnects.erase(i++);
            else {
              if (p->inState(LWS_CLIENT_IDLE)) connect_client(p, vhd);
              i++;
            }
          }
        }

        // process writes
        {
          std::lock_guard<std::mutex> guard(g_mutex_writes);
          for (auto it = pendingWrites.begin(); it != pendingWrites.end(); ++it) {
            std::shared_ptr<SessionHandler> p = (*it).lock();
            if (p && p->getWsi()) lws_callback_on_writable(p->getWsi());
          }
          pendingWrites.clear();
        }

        // process disconnects
        {
          std::lock_guard<std::mutex> guard(g_mutex_disconnects);
          for (auto it = pendingDisconnects.begin(); it != pendingDisconnects.end(); ++it) {
            std::shared_ptr<SessionHandler> p = (*it).lock();
            if (p && p->getWsi()) lws_callback_on_writable(p->getWsi());
          }
          pendingDisconnects.clear();
        }
      }
      break;

    /* --- client callbacks --- */
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "lws_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR wsi: %p\n", wsi);
      {
        std::shared_ptr<SessionHandler> p = findAndRemovePendingConnect(wsi);
        if (!p) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "lws_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find pending connection for wsi: %p\n", wsi);
        }
        else {
          p->lock();
          p->setState(LWS_CLIENT_FAILED);
          p->cond_signal();
          p->unlock();
        }
      }      
      break;


    case LWS_CALLBACK_CLIENT_ESTABLISHED:

      {
        std::shared_ptr<SessionHandler> p = findAndRemovePendingConnect(wsi);
        if (!p) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "lws_callback LWS_CALLBACK_CLIENT_ESTABLISHED unable to find pending connection for wsi: %p\n", wsi);
        }
        else {
          p->lock();
          p->setVhd(vhd);
          p->setState(LWS_CLIENT_CONNECTED);
          p->cond_signal();
          p->unlock();

          // use placement new to allocate a new shared_ptr on the heap that points to the already-created session handler object
          std::shared_ptr<SessionHandler> *pp = (std::shared_ptr<SessionHandler> *) user;
          new (pp) std::shared_ptr<SessionHandler>(p);
        }
      }      
      break;

    case LWS_CALLBACK_CLIENT_CLOSED:
      {
        std::shared_ptr<SessionHandler> *pp = (std::shared_ptr<SessionHandler> *) user;
        {
          std::shared_ptr<SessionHandler> p = *pp;

          if (p && p->inState(LWS_CLIENT_DISCONNECTING)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lws_callback LWS_CALLBACK_CLIENT_CLOSED by us wsi: %p\n", wsi);
          }
          else if (p && p->inState(LWS_CLIENT_CONNECTED)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "lws_callback LWS_CALLBACK_CLIENT_CLOSED by far end wsi: %p\n", wsi);
          }
          p->lock();
          p->setState(LWS_CLIENT_DISCONNECTED);
          p->setWsi(nullptr);
          p->unlock();
        }

        // need to explicitly call destructor of items allocated with placement new
        pp->~shared_ptr<SessionHandler>();
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      {
        std::shared_ptr<SessionHandler> p = *((std::shared_ptr<SessionHandler> *) user);
        if (p && !lws_frame_is_binary(wsi)) {
          std::string msg ;
          if (p->recvFrame(wsi, in, len, msg)) {
            processIncomingMessage(p, msg);
          }
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      {
        std::shared_ptr<SessionHandler> p = *((std::shared_ptr<SessionHandler> *) user);
        if (p) {
          if (p->actuallySendText()) {
            if (p->inState(LWS_CLIENT_DISCONNECTING) || p->hasBufferedAudio()) lws_callback_on_writable(wsi);
            return 0;
          }

          if (p->inState(LWS_CLIENT_DISCONNECTING)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "lws_callback LWS_CALLBACK_WRITEABLE closing connection wsi: %p\n", wsi);
            lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
            return -1;
          }

          // check for audio packets
          p->actuallySendAudio();
        }

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
      sizeof(std::shared_ptr<SessionHandler>),
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

  switch_status_t fork_init(responseHandler_t fn) {
    responseHandler = fn;
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_cleanup() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: stopping lws threads\n");
    
    running = false;
    for (std::deque<std::thread>::iterator it = lwsContextThreads.begin(); it != lwsContextThreads.end(); it++) {
      (*it).join();
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: lws threads stopped\n");
    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_init(switch_core_session_t *session, 
              uint32_t samples_per_second, 
              char *host,
              unsigned int port,
              char *path,
              int desiredSampling,
              int sslFlags,
              int channels,
              char* metadata, 
              void **ppUserData)
  {    	
   switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, 
    "fork_session_init read sampling %u desired sampling %d %s\n", samples_per_second, desiredSampling, host);
   std::shared_ptr<SessionHandler> p = std::make_shared<SessionHandler>(session, host, port, path, sslFlags, 
    samples_per_second, desiredSampling, channels, metadata);

    if (!p || !p->isValid()) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating SessionHandler memory\n");  
      return SWITCH_STATUS_FALSE;
    }

   switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "fork_session_init - calling connect %s\n", host);
    if (!p->connect(context[idxCallCount++ % nServiceThreads])) {
      return SWITCH_STATUS_FALSE;
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "successfully connected to host %s\n", host);

    // allocate a shared_ptr on the heap that will be the owner of the session handler object
    // the lws thread will accesss this through weak pointers only, so when Freeswitch closes
    // the channel we will delete the shared ptr and subsequent accesses from the lws thread
    // will detect this when the lock the weak ptr.

    std::shared_ptr<SessionHandler>* ppHeap = new std::shared_ptr<SessionHandler>(p);
    *ppUserData = ppHeap;

    return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t fork_session_cleanup(switch_core_session_t *session, char* text) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);

    if (bug) {
      std::shared_ptr<SessionHandler>* ppHeap = (std::shared_ptr<SessionHandler>*) switch_core_media_bug_get_user_data(bug);
      std::shared_ptr<SessionHandler> p = *ppHeap;
      switch_channel_set_private(channel, MY_BUG_NAME, NULL);

      p->disconnect(text);

      delete ppHeap;
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
      std::shared_ptr<SessionHandler> p = *((std::shared_ptr<SessionHandler>*) switch_core_media_bug_get_user_data(bug));
      p->sendText(text);
      return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }

  switch_bool_t fork_frame(switch_media_bug_t *bug, void* user_data) {
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    std::shared_ptr<SessionHandler> p = *((std::shared_ptr<SessionHandler>*) switch_core_media_bug_get_user_data(bug));

    p->sendAudio(bug, user_data);

    return SWITCH_TRUE;
  }

  void service_thread(unsigned int nServiceThread) {
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
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: successfully created lws context\n");

    int n;
    do {
      n = lws_service(context[nServiceThread], 500);
    } while (n >= 0 && running);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: thread exiting\n");

    lws_context_destroy(context[nServiceThread]);

  }

  switch_status_t fork_service_threads(int *pRunning) {
    lws_set_log_level(loglevel, lws_logger);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_audio_fork: starting %u lws service threads\n", nServiceThreads);
    for (unsigned int i = 0; i < nServiceThreads; i++) {
      lwsContextThreads.push_back(std::thread(service_thread, i));
    }


    return SWITCH_STATUS_FALSE;
  }

}

// Session Handler implementation

// buffer at most 2 secs of audio @8khz sampling
#define MAX_BUFFERED_SAMPLES (LWS_PRE + 16234)

SessionHandler::SessionHandler(switch_core_session_t *session, const char *host, unsigned int port, const char *path, 
  int sslFlags, uint32_t readSampling, uint32_t desiredSampling, int channels, char* metadata) : 
  m_wsi(nullptr), m_valid(false), m_buffer(nullptr), m_mutex(nullptr), m_cond(nullptr), m_state(LWS_CLIENT_IDLE),
  m_resampler(nullptr), m_sessionId(switch_core_session_get_uuid(session)), m_host(host), m_path(path), m_port(port), m_sslFlags(sslFlags),
  m_sampling(desiredSampling), m_context(nullptr), m_hasAudio(false), m_bufWriteOffset(LWS_PRE) {

  uint8_t pad[LWS_PRE];
  switch_memory_pool_t *pool = switch_core_session_get_pool(session);
  if (SWITCH_STATUS_SUCCESS != switch_buffer_create(pool, &m_buffer, MAX_BUFFERED_SAMPLES)) {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_sessionId.c_str()), SWITCH_LOG_ERROR, "error allocating audio buffer");
    return;
  }

  // libwebsockets requires this padding at front of buffer
  switch_buffer_write(m_buffer, &pad, LWS_PRE);

  if (SWITCH_STATUS_SUCCESS != switch_mutex_init(&m_mutex, SWITCH_MUTEX_DEFAULT, pool)) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error initializing mutex");
    switch_buffer_destroy(&m_buffer);
    m_buffer = nullptr;
    return;
  }
  if (SWITCH_STATUS_SUCCESS != switch_thread_cond_create(&m_cond, pool)) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error initializing condition variable");
    switch_mutex_destroy(m_mutex);
    switch_buffer_destroy(&m_buffer);
    m_buffer = nullptr;
    m_mutex = nullptr;
    return;
  }
  switch_buffer_add_mutex(m_buffer, m_mutex);

  if (readSampling != desiredSampling) {
    int err;
    m_resampler = speex_resampler_init(channels, readSampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
    if (!m_resampler) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error initializing resampler");
    }
  }
  if (metadata) m_deqTextOut.push_back(metadata);

  m_valid = true;
}

SessionHandler::~SessionHandler() {
  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "SessionHandler destroyed\n");
  if (m_resampler) speex_resampler_destroy(m_resampler);
  if (m_mutex) switch_mutex_destroy(m_mutex);
  if (m_cond) switch_thread_cond_destroy(m_cond);
  if (m_buffer) switch_buffer_destroy(&m_buffer);

  // erase all files
  std::for_each(m_filesCreated.begin(), m_filesCreated.end(), [](std::string &path) {
    std::remove(path.c_str());
  });
}

bool SessionHandler::connect(struct lws_context *context) {
  assert(m_valid);

  lock();
  addPendingConnect(shared_from_this());
  lws_cancel_service(context);
  cond_wait();

  if (m_state == LWS_CLIENT_FAILED) {
    unlock();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "failed connecting to host %s\n", m_host.c_str());
    return false;
  }
  m_context = context;
  unlock();

  // write initial metadata
  if (!m_deqTextOut.empty()) {
    std::string metadata = m_deqTextOut.front();
    m_deqTextOut.pop_front();
    sendText(metadata.c_str());
  }

  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "successfully connected to host %s\n", m_host.c_str());
  return true;
}

bool SessionHandler::sendAudio(switch_media_bug_t *bug, void* data) {
  bool written = false;
  if (!m_valid || !m_wsi) return false;

  if (trylock()) {
    const void *pBufHead ;
    switch_frame_t frame = { 0 };
    switch_buffer_peek_zerocopy(m_buffer, &pBufHead);

    // write directly to the buffer if we don't have to resample
    if (!m_resampler) {
      frame.data = (char *) pBufHead + getBufWriteOffset();
      frame.buflen = getBufFreespace();

      while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
        if (frame.datalen && !switch_test_flag((&frame), SFF_CNG) && frame.buflen > 0) {
          bumpBufWriteOffset(frame.datalen);
          frame.data = (char *) pBufHead + getBufWriteOffset();
          frame.buflen = getBufFreespace();
          written = m_hasAudio = true;
        }
        else if (frame.buflen == 0) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Buffer overrun.\n");
          break;
        }
      }
    }
    else {
      uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
      frame.data = data;
      frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

      while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
        if (frame.datalen) {
          spx_uint32_t out_len = getBufFreespace() >> 1; //divide by 2 to get number of uint16_t samples
          spx_uint32_t in_len = frame.samples;

          speex_resampler_process_interleaved_int(m_resampler, 
            (const spx_int16_t *) frame.data, 
            (spx_uint32_t *) &in_len, 
            (spx_int16_t *) ((char *) pBufHead + getBufWriteOffset()),
            &out_len);

          if (out_len > 0) {
            bumpBufWriteOffset(out_len << frame.channels);  //num samples x 2 x num channels
            written = m_hasAudio = true;
          }
          else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Buffer overrun.\n");
            break;
          }
        }
      }
    }

    if (written) {
      addPendingWrite(shared_from_this());
      lws_cancel_service(m_context);
    }
    unlock();
  }
  return written;
}

void SessionHandler::disconnect(const char *finalText) {
  if (!m_wsi) return;

  addPendingDisconnect(shared_from_this());
  if (finalText) sendText(finalText);
  else lws_cancel_service(m_context);
}

void SessionHandler::sendText(const char* szText) {
  if (!m_wsi) return;

  std::lock_guard<std::mutex> l(m_mutexLws);
  std::string metadata(LWS_PRE, '\0');
  metadata.append(szText);
  m_deqTextOut.push_back(metadata);
  addPendingWrite(shared_from_this());
  lws_cancel_service(m_context);
}

// called from lws thread
bool SessionHandler::actuallySendAudio() {
  bool ret = false;

  if (m_wsi && m_hasAudio) {
    uint8_t pad[LWS_PRE];
    const void *pHead ;
    switch_buffer_peek_zerocopy(m_buffer, &pHead);
 
    lock();
    size_t reading = m_bufWriteOffset - LWS_PRE;
    int n = lws_write(m_wsi, (unsigned char *) pHead + LWS_PRE, reading, LWS_WRITE_BINARY);
    if (n < reading) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error writing audio %lu requested, %d written\n", reading, n);
    }

    // reinitialize padding at front of buffer now that it is empty
    switch_buffer_zero(m_buffer);
    switch_buffer_write(m_buffer, &pad, LWS_PRE);
    m_bufWriteOffset = LWS_PRE;
    m_hasAudio = false;
    ret = true;
    unlock();
  }

  return ret;
}

// called from lws thread
bool SessionHandler::actuallySendText() {
  if (!m_wsi) return false;

  std::lock_guard<std::mutex> l(m_mutexLws);
  if (!m_deqTextOut.empty()) {
    std::string msg = m_deqTextOut.front();
    m_deqTextOut.pop_front();
    size_t m = msg.length() - LWS_PRE;
    int n = lws_write(m_wsi, (unsigned char *) msg.data() + LWS_PRE, m, LWS_WRITE_TEXT);
    if (n < m) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "error writing metadata %lu requested, %d written\n", msg.length(), n);
      return false;
    }
    return true;
  }  
  return false;
}

bool SessionHandler::recvFrame(struct lws* wsi, void* in, size_t len, std::string& str) {
  bool ret = false;
  std::lock_guard<std::mutex> l(m_mutexLws);

  if (lws_is_first_fragment(wsi)) {
    std::string metadata(LWS_PRE, '\0');
    m_deqTextIn.push_back(metadata);
  }

  std::string& md = m_deqTextIn.back();
  if (len > 0) {
    md.append((char *)in, len);
  }

  if (lws_is_final_fragment(wsi)) {
    str = md;
    ret = true;
  }

  return ret;
}
