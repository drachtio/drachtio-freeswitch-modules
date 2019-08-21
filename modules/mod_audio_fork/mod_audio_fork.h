#ifndef __MOD_FORK_H__
#define __MOD_FORK_H__

#include <switch.h>
#include <libwebsockets.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "audio_fork"
#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (128)

#define EVENT_TRANSCRIPTION   "mod_audio_fork::transcription"
#define EVENT_TRANSFER        "mod_audio_fork::transfer"
#define EVENT_PLAY_AUDIO      "mod_audio_fork::play_audio"
#define EVENT_KILL_AUDIO      "mod_audio_fork::kill_audio"
#define EVENT_DISCONNECT      "mod_audio_fork::disconnect"
#define EVENT_ERROR           "mod_audio_fork::error"
#define EVENT_MAINTENANCE     "mod_audio_fork::maintenance"

enum {
	LWS_CLIENT_IDLE,
	LWS_CLIENT_CONNECTING,
	LWS_CLIENT_CONNECTED,
	LWS_CLIENT_FAILED,
	LWS_CLIENT_DISCONNECTING,
	LWS_CLIENT_DISCONNECTED
};

struct lws_per_vhost_data {
  struct lws_context *context;
  struct lws_vhost *vhost;
  const struct lws_protocols *protocol;
};

struct playout {
  char *file;
  struct playout* next;
};

typedef void (*responseHandler_t)(const char* sessionId, const char* eventName, char* json);

struct private_data {
	switch_mutex_t *mutex;
	switch_mutex_t *ws_send_mutex;
	switch_mutex_t *ws_recv_mutex;
  switch_thread_cond_t *cond;
	char sessionId[MAX_SESSION_ID];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  int ws_state;
  char host[MAX_WS_URL_LEN];
  unsigned int port;
  char path[MAX_PATH_LEN];
  uint8_t *metadata;
  size_t metadata_length;
  int sslFlags;
  int sampling;
  struct lws *wsi;
  uint8_t *ws_audio_buffer;
  size_t ws_audio_buffer_max_len;
  size_t ws_audio_buffer_write_offset;
  size_t ws_audio_buffer_min_freespace;
  uint8_t* recv_buf;
  uint8_t* recv_buf_ptr;
  struct playout* playout;
  struct lws_per_vhost_data* vhd;
  int  channels;
  unsigned int id;
  int buffer_overrun_notified:1;
};

typedef struct private_data private_t;

#endif
