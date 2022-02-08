#ifndef __MOD_AZURE_TRANSCRIBE_H__
#define __MOD_AZURE_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "azure_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "azure_transcribe::transcription"
#define TRANSCRIBE_EVENT_START_OF_UTTERANCE "azure_transcribe::start_of_utterance"
#define TRANSCRIBE_EVENT_END_OF_UTTERANCE "azure_transcribe::end_of_utterance"
#define TRANSCRIBE_EVENT_NO_SPEECH_DETECTED "azure_transcribe::no_speech_detected"

#define MAX_LANG (12)
#define MAX_REGION (32)
#define MAX_SUBSCRIPTION_KEY_LEN (256)

/* per-channel data */
typedef void (*responseHandler_t)(switch_core_session_t* session, const char* event, const char * json);

struct cap_cb {
	switch_mutex_t *mutex;
	char sessionId[256];
  char subscriptionKey[MAX_SUBSCRIPTION_KEY_LEN];
	uint32_t channels;
  SpeexResamplerState *resampler;
	void* streamer;
	responseHandler_t responseHandler;
	int interim;

	char lang[MAX_LANG];
	char region[MAX_REGION];

	switch_vad_t * vad;
};

#endif