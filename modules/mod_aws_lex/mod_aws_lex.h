#ifndef __MOD_LEX_H__
#define __MOD_LEX_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "__aws_lex_bug__"
#define AWS_LEX_EVENT_INTENT "lex::intent"
#define AWS_LEX_EVENT_TRANSCRIPTION "lex::transcription"
#define AWS_LEX_EVENT_TEXT_RESPONSE "lex::text_response"
#define AWS_LEX_EVENT_AUDIO_PROVIDED "lex::audio_provided"
#define AWS_LEX_EVENT_PLAYBACK_INTERRUPTION "lex::playback_interruption"
#define AWS_LEX_EVENT_ERROR "lex::error"

#define MAX_LANG (12)
#define MAX_BOTNAME (128)
#define MAX_REGION (16)
#define MAX_LOCALE (7)
#define MAX_INTENT (52)
#define MAX_METADATA (1024)

/* per-channel data */
typedef void (*responseHandler_t)(switch_core_session_t* session, const char * type, char* json);
typedef void (*errorHandler_t)(switch_core_session_t* session, const char * reason);

struct cap_cb {
	switch_mutex_t *mutex;
	char sessionId[256];
  char awsAccessKeyId[128];
  char awsSecretAccessKey[128];
  SpeexResamplerState *resampler;
	void* streamer;
	responseHandler_t responseHandler;
	errorHandler_t errorHandler;
	switch_thread_t* thread;
	char bot[MAX_BOTNAME];
	char alias[MAX_BOTNAME];
	char region[MAX_REGION];
	char locale[MAX_LOCALE];
	char intent[MAX_INTENT];
	char metadata[MAX_METADATA];
};

#endif