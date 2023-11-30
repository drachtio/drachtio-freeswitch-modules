#ifndef __MOD_AWS_TRANSCRIBE_H__
#define __MOD_AWS_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "aws_transcribe"
#define MAX_BUG_LEN (64)
#define MAX_SESSION_ID (256)
#define TRANSCRIBE_EVENT_RESULTS "aws_transcribe::transcription"
#define TRANSCRIBE_EVENT_END_OF_TRANSCRIPT "aws_transcribe::end_of_transcript"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "aws_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED "aws_transcribe::max_duration_exceeded"
#define TRANSCRIBE_EVENT_VAD_DETECTED "aws_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_CONNECT_SUCCESS "aws::connect"
#define TRANSCRIBE_EVENT_CONNECT_FAIL    "aws::connect_failed"
#define TRANSCRIBE_EVENT_DISCONNECT      "aws::disconnect"
#define TRANSCRIBE_EVENT_BUFFER_OVERRUN  "aws::buffer_overrun"
#define TRANSCRIBE_EVENT_ERROR      "jambonz_transcribe::error"

#define MAX_LANG (12)
#define MAX_REGION (32)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (4096)
#define MAX_SESSION_TOKEN_LEN (4096)

/* per-channel data */
typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished);

struct private_data {
	switch_mutex_t *mutex;
	char sessionId[MAX_SESSION_ID+1];
  char awsAccessKeyId[128];
  char awsSecretAccessKey[128];
  char awsSessionToken[MAX_SESSION_TOKEN_LEN+1];
  SpeexResamplerState *resampler;
	responseHandler_t responseHandler;
	int interim;
	char lang[MAX_LANG+1];
	char region[MAX_REGION+1];
	switch_vad_t * vad;
	uint32_t samples_per_second;
  void *pAudioPipe;
  int ws_state;
  char host[MAX_WS_URL_LEN+1];
  unsigned int port;
  char path[MAX_PATH_LEN+1];
  char bugname[MAX_BUG_LEN+1];
  int sampling;
  int  channels;
  unsigned int id;
  int buffer_overrun_notified:1;
  int is_finished:1;
};

typedef struct private_data private_t;

#endif