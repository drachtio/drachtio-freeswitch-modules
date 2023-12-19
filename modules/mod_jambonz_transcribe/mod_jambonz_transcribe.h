#ifndef __MOD_JAMBONZ_TRANSCRIBE_H__
#define __MOD_JAMBONZ_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "jambonz_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "jambonz_transcribe::transcription"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "jambonz_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_VAD_DETECTED "jambonz_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_CONNECT_SUCCESS "jambonz_transcribe::connect"
#define TRANSCRIBE_EVENT_CONNECT_FAIL    "jambonz_transcribe::connect_failed"
#define TRANSCRIBE_EVENT_BUFFER_OVERRUN  "jambonz_transcribe::buffer_overrun"
#define TRANSCRIBE_EVENT_DISCONNECT      "jambonz_transcribe::disconnect"
#define TRANSCRIBE_EVENT_ERROR      "jambonz_transcribe::error"

#define MAX_LANG_LEN (12)
#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (4096)
#define MAX_BUG_LEN (64)

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished);

struct private_data {
	switch_mutex_t *mutex;
	char sessionId[MAX_SESSION_ID+1];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  void *pAudioPipe;
  int ws_state;
  char host[MAX_WS_URL_LEN+1];
  unsigned int port;
  char path[MAX_PATH_LEN+1];
  int sslFlags;
  char bugname[MAX_BUG_LEN+1];
  char language[MAX_LANG_LEN+1];
  int sampling;
  int  channels;
  int interim;
  unsigned int id;
  int buffer_overrun_notified:1;
  int is_finished:1;
};

typedef struct private_data private_t;

#endif