#ifndef __MOD_AWS_TRANSCRIBE_H__
#define __MOD_AWS_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "aws_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "aws_transcribe::transcription"
#define TRANSCRIBE_EVENT_END_OF_TRANSCRIPT "aws_transcribe::end_of_transcript"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "aws_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED "aws_transcribe::max_duration_exceeded"

#define MAX_LANG (12)
#define MAX_REGION (32)

/* per-channel data */
typedef void (*responseHandler_t)(switch_core_session_t* session, const char * json);

struct cap_cb {
	switch_mutex_t *mutex;
	char sessionId[256];
  char awsAccessKeyId[128];
  char awsSecretAccessKey[128];
	uint32_t channels;
  SpeexResamplerState *resampler;
	void* streamer;
	responseHandler_t responseHandler;
	switch_thread_t* thread;
	int interim;

	char lang[MAX_LANG];
	char region[MAX_REGION];

	switch_vad_t * vad;
	uint32_t samples_per_second;

};

#endif