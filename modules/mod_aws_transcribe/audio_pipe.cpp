#include "audio_pipe.hpp"
#include "transcribe_manager.hpp"
#include "crc.h"

#include <cassert>
#include <iostream>
#include <netinet/in.h>

#include <fstream>
#include <string>

/* discard incoming text messages over the socket that are longer than this */
#define MAX_RECV_BUF_SIZE (65 * 1024 * 10)
#define RECV_BUF_REALLOC_SIZE (8 * 1024)
#define AWS_PRELUDE_PLUS_HDRS_LEN (100)

using namespace aws;

namespace {
  static const char *requestedTcpKeepaliveSecs = std::getenv("MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS");
  static int nTcpKeepaliveSecs = requestedTcpKeepaliveSecs ? ::atoi(requestedTcpKeepaliveSecs) : 55;
  static uint8_t aws_prelude_and_headers[AWS_PRELUDE_PLUS_HDRS_LEN];

  void writeToFile(const char* buffer, size_t bufferSize) {
    static int writeCounter = 0; // Static variable to keep track of write count

    // Write only the first three times
    if (writeCounter >= 4) {
        return;
    }

    // Generate a unique file name using the writeCounter
    std::string filename = "/tmp/audio_data_" + std::to_string(writeCounter) + ".bin";

    // Open a file in binary mode
    std::ofstream outFile(filename, std::ios::binary);

    // Check if the file is open
    if (outFile.is_open()) {
        // Write the buffer to the file
        outFile.write(buffer, bufferSize);
        outFile.close();

        // Increment the write counter
        writeCounter++;
    } else {
        // Handle error in file opening
        std::cerr << "Unable to open file: " << filename << std::endl;
    }
  }
}


int AudioPipe::aws_lws_callback(struct lws *wsi, 
  enum lws_callback_reasons reason,
  void *user, void *in, size_t len) {

  struct AudioPipe::lws_per_vhost_data *vhd = 
    (struct AudioPipe::lws_per_vhost_data *) lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

  struct lws_vhost* vhost = lws_get_vhost(wsi);
  AudioPipe ** ppAp = (AudioPipe **) user;

  switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      vhd = (struct AudioPipe::lws_per_vhost_data *) lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct AudioPipe::lws_per_vhost_data));
      vhd->context = lws_get_context(wsi);
      vhd->protocol = lws_get_protocol(wsi);
      vhd->vhost = lws_get_vhost(wsi);
      break;

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      processPendingConnects(vhd);
      processPendingDisconnects(vhd);
      processPendingWrites();
      break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      {
        AudioPipe* ap = findAndRemovePendingConnect(wsi);
        int rc = lws_http_client_http_response(wsi);
        lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR: %s, response status %d\n", in ? (char *)in : "(null)", rc); 
        if (ap) {
          ap->m_state = LWS_CLIENT_FAILED;
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECT_FAIL, (char *) in, ap->isFinished());
        }
        else {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find wsi %p..\n", wsi); 
        }
      }      
      break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      {
        AudioPipe* ap = findAndRemovePendingConnect(wsi);
        if (ap) {
          *ppAp = ap;
          ap->m_vhd = vhd;
          ap->m_state = LWS_CLIENT_CONNECTED;
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECT_SUCCESS, NULL,  ap->isFinished());
        }
        else {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_ESTABLISHED %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
        }
      }      
      break;
    case LWS_CALLBACK_CLIENT_CLOSED:
      {
        AudioPipe* ap = *ppAp;
        if (!ap) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CLOSED %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
          return 0;
        }
        if (ap->m_state == LWS_CLIENT_DISCONNECTING) {
          // closed by us

          lwsl_debug("%s socket closed by us\n", ap->m_uuid.c_str());
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECTION_CLOSED_GRACEFULLY, NULL,  ap->isFinished());
        }
        else if (ap->m_state == LWS_CLIENT_CONNECTED) {
          // closed by far end
          lwsl_info("%s socket closed by far end\n", ap->m_uuid.c_str());
          ap->m_callback(ap->m_uuid.c_str(), AudioPipe::CONNECTION_DROPPED, NULL,  ap->isFinished());
        }
        ap->m_state = LWS_CLIENT_DISCONNECTED;
        ap->setClosed();
    
        //NB: after receiving any of the events above, any holder of a 
        //pointer or reference to this object must treat is as no longer valid

        //*ppAp = NULL;
        //delete ap;
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      {
        AudioPipe* ap = *ppAp;
        if (!ap) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
          return 0;
        }

        if (lws_is_first_fragment(wsi)) {
          // allocate a buffer for the entire chunk of memory needed
          assert(nullptr == ap->m_recv_buf);
          ap->m_recv_buf_len = len + lws_remaining_packet_payload(wsi);
          ap->m_recv_buf = (uint8_t*) malloc(ap->m_recv_buf_len);
          ap->m_recv_buf_ptr = ap->m_recv_buf;
        }

        size_t write_offset = ap->m_recv_buf_ptr - ap->m_recv_buf;
        size_t remaining_space = ap->m_recv_buf_len - write_offset;
        if (remaining_space < len) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE buffer realloc needed.\n");
          size_t newlen = ap->m_recv_buf_len + RECV_BUF_REALLOC_SIZE;
          if (newlen > MAX_RECV_BUF_SIZE) {
            free(ap->m_recv_buf);
            ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
            ap->m_recv_buf_len = 0;
            lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE max buffer exceeded, truncating message.\n");
          }
          else {
            ap->m_recv_buf = (uint8_t*) realloc(ap->m_recv_buf, newlen);
            if (nullptr != ap->m_recv_buf) {
              ap->m_recv_buf_len = newlen;
              ap->m_recv_buf_ptr = ap->m_recv_buf + write_offset;
            }
          }
        }

        if (nullptr != ap->m_recv_buf) {
          if (len > 0) {
            memcpy(ap->m_recv_buf_ptr, in, len);
            ap->m_recv_buf_ptr += len;
          }
          if (lws_is_final_fragment(wsi)) {
            //lwsl_err("AudioPipe::lws_service_thread - LWS_CALLBACK_CLIENT_RECEIVE received %d bytes.\n", len);
            if (nullptr != ap->m_recv_buf) {
              bool isError = false;
              std::string payload;
              std::string msg((char *)ap->m_recv_buf, ap->m_recv_buf_ptr - ap->m_recv_buf);

              TranscribeManager::parseResponse(msg, payload, isError, true);
              //lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE payload: %s.\n", payload.c_str());
              //lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE response %s.\n", msg.c_str());


              ap->m_callback(ap->m_uuid.c_str(), AudioPipe::MESSAGE, payload.c_str(),  ap->isFinished());
              if (nullptr != ap->m_recv_buf) free(ap->m_recv_buf);
            }
            ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
            ap->m_recv_buf_len = 0;
          }
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      {
        AudioPipe* ap = *ppAp;
        if (!ap) {
          lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s unable to find wsi %p..\n", ap->m_uuid.c_str(), wsi); 
          return 0;
        }

        if (ap->m_state == LWS_CLIENT_DISCONNECTING) {
          lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
          return -1;
        }

        // check for audio packets
        {
          std::lock_guard<std::mutex> lk(ap->m_audio_mutex);
          if (ap->m_audio_buffer_write_offset > LWS_PRE + AWS_PRELUDE_PLUS_HDRS_LEN || ap->isFinished()) {

            // send a zero length audio packet to indicate end of stream
            if (ap->isFinished()) {
              ap->m_audio_buffer_write_offset = LWS_PRE + AWS_PRELUDE_PLUS_HDRS_LEN;
            }
            /**
             * fill in
             * [0..3] = total byte length
             * [8..11] = prelude crc
             * following the audio data: 4 bytes of Message CRC
             * 
             */

            // copy in the prelude and headers
            memcpy(ap->m_audio_buffer + LWS_PRE, aws_prelude_and_headers, AWS_PRELUDE_PLUS_HDRS_LEN);

            // fill in the total byte length
            uint32_t totalLen = ap->m_audio_buffer_write_offset - LWS_PRE + 4; // for the trailing Message CRC which is 4 bytes
            //lwsl_err("AudioPipe - total length %u (decimal), 0x%X (hex)\n", totalLen, totalLen);
            totalLen = htonl(totalLen);
            //lwsl_err("AudioPipe - total length in network byte order %u (decimal), 0x%X (hex)\n", totalLen, totalLen);
            memcpy(ap->m_audio_buffer + LWS_PRE, &totalLen, sizeof(uint32_t));

            // fill in the prelude crc
            uint32_t preludeCRC = CRC::Calculate(ap->m_audio_buffer + LWS_PRE, 8, CRC::CRC_32());
            //lwsl_err("AudioPipe - prelude CRC %u (decimal), 0x%X (hex)\n", preludeCRC, preludeCRC);
            preludeCRC = htonl(preludeCRC);
            //lwsl_err("AudioPipe - prelude CRC in network order %u (decimal), 0x%X (hex)\n", preludeCRC, preludeCRC);
            memcpy(ap->m_audio_buffer + LWS_PRE + 8, &preludeCRC, sizeof(uint32_t));

            // fill in the message crc
            uint32_t messageCRC = CRC::Calculate(ap->m_audio_buffer + LWS_PRE, ap->m_audio_buffer_write_offset - LWS_PRE, CRC::CRC_32());
            messageCRC = htonl(messageCRC);
            memcpy(ap->m_audio_buffer + ap->m_audio_buffer_write_offset, &messageCRC, sizeof(uint32_t));
            ap->m_audio_buffer_write_offset + sizeof(uint32_t);

            size_t datalen = ap->m_audio_buffer_write_offset - LWS_PRE + 4;
            //lwsl_err("AudioPipe - datalen %u (decimal)\n", datalen);

            // TMP: write data to a file
            //writeToFile((const char *) ap->m_audio_buffer + LWS_PRE, datalen);

            int sent = lws_write(wsi, (unsigned char *) ap->m_audio_buffer + LWS_PRE, datalen, LWS_WRITE_BINARY);
            if (sent < datalen) {
              lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s attemped to send %lu only sent %d wsi %p..\n", 
                ap->m_uuid.c_str(), datalen, sent, wsi); 
            }
            ap->binaryWritePtrResetToZero();
          }
        }

        return 0;
      }
      break;

    default:
      break;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}


// static members
static const lws_retry_bo_t retry = {
    nullptr,   // retry_ms_table
    0,         // retry_ms_table_count
    0,         // conceal_count
    UINT16_MAX,         // secs_since_valid_ping
    UINT16_MAX,        // secs_since_valid_hangup
    0          // jitter_percent
};

struct lws_context *AudioPipe::contexts[] = {
  nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr
};
unsigned int AudioPipe::numContexts = 0;
unsigned int AudioPipe::nchild = 0;
std::string AudioPipe::protocolName;
std::mutex AudioPipe::mutex_connects;
std::mutex AudioPipe::mutex_disconnects;
std::mutex AudioPipe::mutex_writes;
std::list<AudioPipe*> AudioPipe::pendingConnects;
std::list<AudioPipe*> AudioPipe::pendingDisconnects;
std::list<AudioPipe*> AudioPipe::pendingWrites;
AudioPipe::log_emit_function AudioPipe::logger;
std::mutex AudioPipe::mapMutex;
std::unordered_map<std::thread::id, bool> AudioPipe::stopFlags;
std::queue<std::thread::id> AudioPipe::threadIds;


void AudioPipe::processPendingConnects(lws_per_vhost_data *vhd) {
  std::list<AudioPipe*> connects;
  {
    std::lock_guard<std::mutex> guard(mutex_connects);
    for (auto it = pendingConnects.begin(); it != pendingConnects.end(); ++it) {
      if ((*it)->m_state == LWS_CLIENT_IDLE) {
        connects.push_back(*it);
        (*it)->m_state = LWS_CLIENT_CONNECTING;
      }
    }
  }
  for (auto it = connects.begin(); it != connects.end(); ++it) {
    AudioPipe* ap = *it;
    ap->connect_client(vhd);   
  }
}

void AudioPipe::processPendingDisconnects(lws_per_vhost_data *vhd) {
  std::list<AudioPipe*> disconnects;
  {
    std::lock_guard<std::mutex> guard(mutex_disconnects);
    for (auto it = pendingDisconnects.begin(); it != pendingDisconnects.end(); ++it) {
      if ((*it)->m_state == LWS_CLIENT_DISCONNECTING) disconnects.push_back(*it);
    }
    pendingDisconnects.clear();
  }
  for (auto it = disconnects.begin(); it != disconnects.end(); ++it) {
    AudioPipe* ap = *it;
    lws_callback_on_writable(ap->m_wsi); 
  }
}

void AudioPipe::processPendingWrites() {
  std::list<AudioPipe*> writes;
  {
    std::lock_guard<std::mutex> guard(mutex_writes);
    for (auto it = pendingWrites.begin(); it != pendingWrites.end(); ++it) {
       if ((*it)->m_state == LWS_CLIENT_CONNECTED) writes.push_back(*it);
    }  
    pendingWrites.clear();
  }
  for (auto it = writes.begin(); it != writes.end(); ++it) {
    AudioPipe* ap = *it;
    lws_callback_on_writable(ap->m_wsi);
  }
}

AudioPipe* AudioPipe::findAndRemovePendingConnect(struct lws *wsi) {
  AudioPipe* ap = NULL;
  std::lock_guard<std::mutex> guard(mutex_connects);
  std::list<AudioPipe* > toRemove;

  for (auto it = pendingConnects.begin(); it != pendingConnects.end() && !ap; ++it) {
    int state = (*it)->m_state;

    if ((*it)->m_wsi == nullptr)
      toRemove.push_back(*it);

    if ((state == LWS_CLIENT_CONNECTING) &&
      (*it)->m_wsi == wsi) ap = *it;
  }

  for (auto it = toRemove.begin(); it != toRemove.end(); ++it)
    pendingConnects.remove(*it);

  if (ap) {
    pendingConnects.remove(ap);
  }

  return ap;
}

AudioPipe* AudioPipe::findPendingConnect(struct lws *wsi) {
  AudioPipe* ap = NULL;
  std::lock_guard<std::mutex> guard(mutex_connects);

  for (auto it = pendingConnects.begin(); it != pendingConnects.end() && !ap; ++it) {
    int state = (*it)->m_state;
    if ((state == LWS_CLIENT_CONNECTING) &&
      (*it)->m_wsi == wsi) ap = *it;
  }
  return ap;
}

void AudioPipe::addPendingConnect(AudioPipe* ap) {
  {
    std::lock_guard<std::mutex> guard(mutex_connects);
    pendingConnects.push_back(ap);
    lwsl_debug("%s after adding connect there are %lu pending connects\n", 
      ap->m_uuid.c_str(), pendingConnects.size());
  }
  lws_cancel_service(contexts[nchild++ % numContexts]);
}
void AudioPipe::addPendingDisconnect(AudioPipe* ap) {
  ap->m_state = LWS_CLIENT_DISCONNECTING;
  {
    std::lock_guard<std::mutex> guard(mutex_disconnects);
    pendingDisconnects.push_back(ap);
    lwsl_debug("%s after adding disconnect there are %lu pending disconnects\n", 
      ap->m_uuid.c_str(), pendingDisconnects.size());
  }
  lws_cancel_service(ap->m_vhd->context);
}
void AudioPipe::addPendingWrite(AudioPipe* ap) {
  {
    std::lock_guard<std::mutex> guard(mutex_writes);
    pendingWrites.push_back(ap);
  }
  lws_cancel_service(ap->m_vhd->context);
}

void AudioPipe::binaryWritePtrResetToZero(void) {
    m_audio_buffer_write_offset = LWS_PRE + AWS_PRELUDE_PLUS_HDRS_LEN;
}

bool AudioPipe::lws_service_thread(unsigned int nServiceThread) {
  struct lws_context_creation_info info;
  std::thread::id this_id = std::this_thread::get_id();

  const struct lws_protocols protocols[] = {
    {
      "",
      AudioPipe::aws_lws_callback,
      sizeof(void *),
      1024,
    },
    { NULL, NULL, 0, 0 }
  };

  memset(&info, 0, sizeof info); 
  info.port = CONTEXT_PORT_NO_LISTEN; 
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.protocols = protocols;
  info.ka_time = nTcpKeepaliveSecs;                    // tcp keep-alive timer
  info.ka_probes = 4;                   // number of times to try ka before closing connection
  info.ka_interval = 5;                 // time between ka's
  info.timeout_secs = 10;                // doc says timeout for "various processes involving network roundtrips"
  info.keepalive_timeout = 5;           // seconds to allow remote client to hold on to an idle HTTP/1.1 connection 
  info.timeout_secs_ah_idle = 10;       // secs to allow a client to hold an ah without using it
  info.retry_and_idle_policy = &retry;

  lwsl_notice("AudioPipe::lws_service_thread creating context in service thread %d.\n", nServiceThread);

  contexts[nServiceThread] = lws_create_context(&info);
  if (!contexts[nServiceThread]) {
    lwsl_err("AudioPipe::lws_service_thread failed creating context in service thread %d..\n", nServiceThread); 
    return false;
  }

  int n;
  do {
    n = lws_service(contexts[nServiceThread], 0);
  } while (n >= 0 && !stopFlags[this_id]);

  // Cleanup once work is done or stopped
  {
      std::lock_guard<std::mutex> lock(mapMutex);
      stopFlags.erase(this_id);
  }

  lwsl_notice("AudioPipe::lws_service_thread ending in service thread %d\n", nServiceThread); 
  return true;
}

void AudioPipe::initialize(unsigned int nThreads, int loglevel, log_emit_function logger) {
  assert(nThreads > 0 && nThreads <= 10);

  numContexts = nThreads;
  lws_set_log_level(loglevel, logger);

  lwsl_notice("AudioPipe::initialize starting %d threads\n", nThreads); 
  for (unsigned int i = 0; i < numContexts; i++) {
    std::lock_guard<std::mutex> lock(mapMutex);
    std::thread t(&AudioPipe::lws_service_thread, i);
    stopFlags[t.get_id()] = false;
    threadIds.push(t.get_id());
    t.detach();
  }
}

bool AudioPipe::deinitialize() {
  lwsl_notice("AudioPipe::deinitialize\n"); 
  std::lock_guard<std::mutex> lock(mapMutex);
  if (!threadIds.empty()) {
      std::thread::id id = threadIds.front();
      threadIds.pop();
      stopFlags[id] = true;
  }
  for (unsigned int i = 0; i < numContexts; i++)
  {
    lwsl_notice("AudioPipe::deinitialize destroying context %d of %d\n", i + 1, numContexts);
    lws_context_destroy(contexts[i]);
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  return true;
}

// instance members
AudioPipe::AudioPipe(const char* uuid, const char* host, unsigned int port, const char* path,
  size_t bufLen, size_t minFreespace, notifyHandler_t callback) :
  m_uuid(uuid), m_host(host), m_port(port), m_path(path), m_finished(false),
  m_audio_buffer_min_freespace(minFreespace), m_audio_buffer_max_len(bufLen), m_gracefulShutdown(false),
  m_recv_buf(nullptr), m_recv_buf_ptr(nullptr),
  m_state(LWS_CLIENT_IDLE), m_wsi(nullptr), m_vhd(nullptr), m_callback(callback) {

  char headerBuffer[88];
  char* buffer = headerBuffer;
  m_audio_buffer = new uint8_t[m_audio_buffer_max_len];

  // stamp out the template for the prelude and headers 
  memset(aws_prelude_and_headers, 0, AWS_PRELUDE_PLUS_HDRS_LEN);

  // aws_prelude_and_headers[0..3] = total byte length (not known till message send time)

  // aws_prelude_and_headers[4..7] = headers byte length
  uint32_t headerLen = sizeof(headerBuffer);
  headerLen = htonl(headerLen);
  memcpy(&aws_prelude_and_headers[4], &headerLen, sizeof(uint32_t));

  // aws_prelude_and_headers[8..11] = prelude crc (not known till message send time)

  // aws_prelude_and_headers[12..99] = headers
  TranscribeManager::writeHeader(&buffer, ":content-type", "application/octet-stream");
  TranscribeManager::writeHeader(&buffer, ":event-type", "AudioEvent");
  TranscribeManager::writeHeader(&buffer, ":message-type", "event");

  memcpy(&aws_prelude_and_headers[12], headerBuffer, sizeof(headerBuffer));

  // following this will be the audio data and a final message CRC (not known till message send time)

  memcpy(m_audio_buffer + LWS_PRE, aws_prelude_and_headers, AWS_PRELUDE_PLUS_HDRS_LEN);
  m_audio_buffer_write_offset = LWS_PRE + AWS_PRELUDE_PLUS_HDRS_LEN;

  //writeToFile((const char *) m_audio_buffer + LWS_PRE, AWS_PRELUDE_PLUS_HDRS_LEN);

}

AudioPipe::~AudioPipe() {
  if (m_audio_buffer) delete [] m_audio_buffer;
  if (m_recv_buf) delete [] m_recv_buf;
}

void AudioPipe::connect(void) {
  addPendingConnect(this);
}

bool AudioPipe::connect_client(struct lws_per_vhost_data *vhd) {
  assert(m_audio_buffer != nullptr);
  assert(m_vhd == nullptr);
  struct lws_client_connect_info i;

  memset(&i, 0, sizeof(i));
  i.context = vhd->context;
  i.port = m_port;
  i.address = m_host.c_str();
  i.path = m_path.c_str();
  i.host = i.address;
  i.origin = i.address;
  i.ssl_connection = LCCSCF_USE_SSL;
  //i.protocol = protocolName.c_str();
  i.pwsi = &(m_wsi);

  m_state = LWS_CLIENT_CONNECTING;
  m_vhd = vhd;

  m_wsi = lws_client_connect_via_info(&i);
  lwsl_debug("%s attempting connection, wsi is %p\n", m_uuid.c_str(), m_wsi);

  return nullptr != m_wsi;
}

void AudioPipe::bufferForSending(const char* text) {
  if (m_state != LWS_CLIENT_CONNECTED) return;
  {
    std::lock_guard<std::mutex> lk(m_text_mutex);
    m_metadata.append(text);
  }
  addPendingWrite(this);
}

void AudioPipe::unlockAudioBuffer() {
  if (m_audio_buffer_write_offset > LWS_PRE) addPendingWrite(this);
  m_audio_mutex.unlock();
}

void AudioPipe::close() {
  if (m_state != LWS_CLIENT_CONNECTED) return;
  addPendingDisconnect(this);
}

void AudioPipe::finish() {
  if (m_finished || m_state != LWS_CLIENT_CONNECTED) return;
  m_finished = true;
  addPendingWrite(this);
}

void AudioPipe::waitForClose() {
  std::shared_future<void> sf(m_promise.get_future());
  sf.wait();
  return;
}
