#ifndef __MOD_COBALT_TRANSCRIBE_H__
#define __MOD_COBALT_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MAX_SESSION_ID (256)
#define MAX_BUG_LEN (64)
#define MY_BUG_NAME "cobalt_speech"
#define TRANSCRIBE_EVENT_RESULTS "cobalt_speech::transcription"
#define TRANSCRIBE_EVENT_ERROR      "jambonz::error"
#define TRANSCRIBE_EVENT_VAD_DETECTED "cobalt_speech::vad_detected"
#define TRANSCRIBE_EVENT_MODEL_LIST_RESPONSE "cobalt_speech::model_list_response"
#define TRANSCRIBE_EVENT_VERSION_RESPONSE "cobalt_speech::version_response"
#define TRANSCRIBE_EVENT_COMPILE_CONTEXT_RESPONSE "cobalt_speech::compile_context_response"


/* per-channel data */
typedef void (*responseHandler_t)(switch_core_session_t* session, 
	const char* json, const char* bugname, 
	const char* details);

struct cap_cb {
	switch_mutex_t *mutex;
	char bugname[MAX_BUG_LEN+1];
	char sessionId[MAX_SESSION_ID+1];
	char *base;
  SpeexResamplerState *resampler;
	void* streamer;
	responseHandler_t responseHandler;
	switch_thread_t* thread;
	int end_of_utterance;
	switch_vad_t * vad;
	uint32_t samples_per_second;
};

#endif