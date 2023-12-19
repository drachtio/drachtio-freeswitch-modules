#ifndef __MOD_AWS_TRANSCRIBE_H__
#define __MOD_AWS_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "deepgram_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "deepgram_transcribe::transcription"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "deepgram_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_VAD_DETECTED "deepgram_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_CONNECT_SUCCESS "deepgram_transcribe::connect"
#define TRANSCRIBE_EVENT_CONNECT_FAIL    "deepgram_transcribe::connect_failed"
#define TRANSCRIBE_EVENT_BUFFER_OVERRUN  "deepgram_transcribe::buffer_overrun"
#define TRANSCRIBE_EVENT_DISCONNECT      "deepgram_transcribe::disconnect"

#define MAX_LANG (12)
#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (4096)
#define MAX_BUG_LEN (64)

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished);

struct private_data {
	switch_mutex_t *mutex;
	char sessionId[MAX_SESSION_ID];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  void *pAudioPipe;
  int ws_state;
  char host[MAX_WS_URL_LEN];
  unsigned int port;
  char path[MAX_PATH_LEN];
  char bugname[MAX_BUG_LEN+1];
  int sampling;
  int  channels;
  unsigned int id;
  int buffer_overrun_notified:1;
  int is_finished:1;
};

typedef struct private_data private_t;

#endif