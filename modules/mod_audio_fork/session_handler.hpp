#ifndef __REQUEST_HANDLER_HPP__
#define __REQUEST_HANDLER_HPP__

#include <memory>
#include <functional>
#include <deque>
#include<algorithm>
#include <mutex>

#include <switch.h>
#include <speex/speex_resampler.h>

enum State_t {
	LWS_CLIENT_IDLE,
	LWS_CLIENT_CONNECTING,
	LWS_CLIENT_CONNECTED,
	LWS_CLIENT_FAILED,
	LWS_CLIENT_DISCONNECTING,
	LWS_CLIENT_DISCONNECTED
};

class SessionHandler;

typedef std::function< void(std::shared_ptr<SessionHandler>) > NotifyFn_t;

class SessionHandler : public std::enable_shared_from_this<SessionHandler> {
public:

  SessionHandler(switch_core_session_t *session, const char *host, unsigned int port, const char *path, 
    int sslFlags, uint32_t readSampling, uint32_t desiredSampling, int channels, char* metadata) ;

  ~SessionHandler() ;

  // non-copyable, no default constructor
  SessionHandler() = delete;
  void operator=(const SessionHandler&) = delete;
  SessionHandler(const SessionHandler&) = delete;

  bool isValid(void) { return m_valid; }

  const std::string& getSessionId(void) { return m_sessionId; }

  // connect to the remote websocket server
  bool connect(struct lws_context *context);
  void disconnect(const char *finalText);
  bool hasBufferedAudio(void) { return m_hasAudio; }

  // data from VoIP network side
  bool sendAudio(switch_media_bug_t *bug, void* user_data) ;
  bool actuallySendAudio(void);
  void sendText(const char* szText);
  bool actuallySendText(void);

  // data from websocket side
  bool recvFrame(struct lws* wsi, void* in, size_t len, std::string& str);

  void addFile(const char* szFilename) { m_filesCreated.push_back(szFilename); }

  bool inState(State_t state) { return m_state == state; }
  void setState(State_t state) { m_state = state; }
  bool hasWsi(struct lws *wsi) { return m_wsi == wsi; }
  void setWsi(struct lws *wsi) { m_wsi = wsi; }
  struct lws* getWsi(void) { return m_wsi; }
  void setVhd(struct lws_per_vhost_data *vhd) { m_vhd = vhd; }

  const char* getHost(void) { return m_host.c_str(); }
  unsigned int getPort(void) { return m_port; }
  const char* getPath(void) { return m_path.c_str(); }

  int getSslFlags(void) { return m_sslFlags; }

  void lock(void) { switch_buffer_lock(m_buffer); }
  bool trylock(void) {return switch_buffer_trylock(m_buffer) == SWITCH_STATUS_SUCCESS; }
  void unlock(void) { switch_buffer_unlock(m_buffer); }

  void cond_signal(void) { switch_thread_cond_signal(m_cond); }
  void cond_wait(void) { switch_thread_cond_wait(m_cond, m_mutex); }

  struct lws** getPointerToWsi(void) { return &m_wsi; }

  size_t getBufWriteOffset(void) { return m_bufWriteOffset;}
  size_t bumpBufWriteOffset(int n) { return m_bufWriteOffset += n;}
  size_t getBufFreespace(void) { return switch_buffer_freespace(m_buffer) - LWS_PRE; }

private:

  bool                       m_valid;
  bool                       m_hasAudio;
  struct lws_per_vhost_data  *m_vhd;
  struct lws_context         *m_context;
	std::string                m_sessionId;
  switch_buffer_t            *m_buffer;
	switch_mutex_t             *m_mutex;
  switch_thread_cond_t       *m_cond;
  SpeexResamplerState        *m_resampler;
  struct lws                 *m_wsi;
  std::mutex                 m_mutexLws;
  std::deque<std::string>    m_filesCreated;
  std::deque<std::string>    m_deqTextIn;
  std::deque<std::string>    m_deqTextOut;
  int                        m_state;
  std::string                m_host;
  unsigned int               m_port;
  std::string                m_path;
  int                        m_sslFlags;
  uint32_t                   m_sampling;
  size_t                     m_bufWriteOffset; 
};

#endif
