#ifndef __MOD_GOOGLE_TRANSCRIBE_H__
#define __MOD_GOOGLE_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MAX_SESSION_ID (256)
#define MAX_BUG_LEN (64)
#define MY_BUG_NAME "google_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "google_transcribe::transcription"
#define TRANSCRIBE_EVENT_END_OF_UTTERANCE "google_transcribe::end_of_utterance"
#define TRANSCRIBE_EVENT_START_OF_TRANSCRIPT "google_transcribe::start_of_transcript"
#define TRANSCRIBE_EVENT_END_OF_TRANSCRIPT "google_transcribe::end_of_transcript"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "google_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED "google_transcribe::max_duration_exceeded"
#define TRANSCRIBE_EVENT_PLAY_INTERRUPT "google_transcribe::play_interrupt"
#define TRANSCRIBE_EVENT_VAD_DETECTED "google_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_ERROR      "jambonz_transcribe::error"


// simply write a wave file
//#define DEBUG_TRANSCRIBE 0


#ifdef DEBUG_TRANSCRIBE

/* per-channel data */
struct cap_cb {
	switch_buffer_t *buffer;
	switch_mutex_t *mutex;
	char *base;
    SpeexResamplerState *resampler;
    FILE* fp;
};
#else
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
  int wants_single_utterance;
  int got_end_of_utterance;
	int play_file;
	switch_vad_t * vad;
	uint32_t samples_per_second;
};
#endif

#endif