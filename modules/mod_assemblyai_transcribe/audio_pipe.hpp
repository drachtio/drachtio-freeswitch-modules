#ifndef __AAI_AUDIO_PIPE_HPP__
#define __AAI_AUDIO_PIPE_HPP__

#include <string>
#include <list>
#include <mutex>
#include <future>
#include <queue>
#include <unordered_map>
#include <thread>

#include <libwebsockets.h>

namespace assemblyai {

class AudioPipe {
public:
  enum LwsState_t {
    LWS_CLIENT_IDLE,
    LWS_CLIENT_CONNECTING,
    LWS_CLIENT_CONNECTED,
    LWS_CLIENT_FAILED,
    LWS_CLIENT_DISCONNECTING,
    LWS_CLIENT_DISCONNECTED
  };
  enum NotifyEvent_t {
    CONNECT_SUCCESS,
    CONNECT_FAIL,
    CONNECTION_DROPPED,
    CONNECTION_CLOSED_GRACEFULLY,
    MESSAGE
  };
  typedef void (*log_emit_function)(int level, const char *line);
  typedef void (*notifyHandler_t)(const char *sessionId, NotifyEvent_t event, const char* message, bool finished);

  struct lws_per_vhost_data {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;
  };

  static void initialize(unsigned int nThreads, int loglevel, log_emit_function logger);
  static bool deinitialize();
  static bool lws_service_thread(unsigned int nServiceThread);

  // constructor
  AudioPipe(const char* uuid, const char* host, unsigned int port, const char* path, 
    size_t bufLen, size_t minFreespace, const char* apiKey, notifyHandler_t callback);
  ~AudioPipe();  

  LwsState_t getLwsState(void) { return m_state; }
  std::string& getApiKey(void) {
    return m_apiKey;
  }
  void connect(void);
  void bufferForSending(const char* text);
  size_t binarySpaceAvailable(void) {
    return m_audio_buffer_max_len - m_audio_buffer_write_offset;
  }
  size_t binaryMinSpace(void) {
    return m_audio_buffer_min_freespace;
  }
  char * binaryWritePtr(void) { 
    return (char *) m_audio_buffer + m_audio_buffer_write_offset;
  }
  void binaryWritePtrAdd(size_t len) {
    m_audio_buffer_write_offset += len;
  }
  void binaryWritePtrResetToZero(void) {
    m_audio_buffer_write_offset = 0;
  }
  void lockAudioBuffer(void) {
    m_audio_mutex.lock();
  }
  void unlockAudioBuffer(void) ;

  void close() ;
  void finish();
  void waitForClose();
  void setClosed() { m_promise.set_value(); }
  bool isFinished() { return m_finished;}

  // no default constructor or copying
  AudioPipe() = delete;
  AudioPipe(const AudioPipe&) = delete;
  void operator=(const AudioPipe&) = delete;

private:

  static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len); 
  static unsigned int nchild;
  static struct lws_context *contexts[];
  static unsigned int numContexts;
  static std::string protocolName;
  static std::mutex mutex_connects;
  static std::mutex mutex_disconnects;
  static std::mutex mutex_writes;
  static std::list<AudioPipe*> pendingConnects;
  static std::list<AudioPipe*> pendingDisconnects;
  static std::list<AudioPipe*> pendingWrites;
  static log_emit_function logger;

  static std::mutex mapMutex;
  static std::unordered_map<std::thread::id, bool> stopFlags;
  static std::queue<std::thread::id> threadIds;

  static AudioPipe* findAndRemovePendingConnect(struct lws *wsi);
  static AudioPipe* findPendingConnect(struct lws *wsi);
  static void addPendingConnect(AudioPipe* ap);
  static void addPendingDisconnect(AudioPipe* ap);
  static void addPendingWrite(AudioPipe* ap);
  static void processPendingConnects(lws_per_vhost_data *vhd);
  static void processPendingDisconnects(lws_per_vhost_data *vhd);
  static void processPendingWrites(void);
  
  bool connect_client(struct lws_per_vhost_data *vhd);

  LwsState_t m_state;
  std::string m_uuid;
  std::string m_host;
  unsigned int m_port;
  std::string m_path;
  std::string m_metadata;
  std::mutex m_text_mutex;
  std::mutex m_audio_mutex;
  int m_sslFlags;
  struct lws *m_wsi;
  uint8_t *m_audio_buffer;
  size_t m_audio_buffer_max_len;
  size_t m_audio_buffer_write_offset;
  size_t m_audio_buffer_min_freespace;
  uint8_t* m_recv_buf;
  uint8_t* m_recv_buf_ptr;
  size_t m_recv_buf_len;
  struct lws_per_vhost_data* m_vhd;
  notifyHandler_t m_callback;
  log_emit_function m_logger;
  std::string m_apiKey;
  bool m_gracefulShutdown;
  bool m_finished;
  std::string m_bugname;
  std::promise<void> m_promise;
};

} // namespace assemblyai
#endif
