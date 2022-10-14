#ifndef __MOD_NUANCE_TRANSCRIBE_H__
#define __MOD_NUANCE_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MAX_SESSION_ID (256)
#define MAX_BUG_LEN (64)
#define MY_BUG_NAME "nuance_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "nuance_transcribe::transcription"
#define TRANSCRIBE_EVENT_END_OF_UTTERANCE "nuance_transcribe::end_of_utterance"
#define TRANSCRIBE_EVENT_START_OF_TRANSCRIPT "nuance_transcribe::start_of_transcript"
#define TRANSCRIBE_EVENT_END_OF_TRANSCRIPT "nuance_transcribe::end_of_transcript"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "nuance_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED "nuance_transcribe::max_duration_exceeded"
#define TRANSCRIBE_EVENT_PLAY_INTERRUPT "nuance_transcribe::play_interrupt"
#define TRANSCRIBE_EVENT_VAD_DETECTED "nuance_transcribe::vad_detected"


/* per-channel data */
typedef void (*responseHandler_t)(switch_core_session_t* session, const char* json, const char* bugname);

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