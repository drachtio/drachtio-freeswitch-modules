#ifndef __GOOGLE_GLUE_H__
#define __GOOGLE_GLUE_H__

switch_status_t google_speech_init();
switch_status_t google_speech_cleanup();
switch_status_t google_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, int single_utterence,
		int separate_recognition, int max_alternatives, int profinity_filter, int word_time_offset, int punctuation, const char* model, int enhanced, 
		const char* hints, char* play_file, void **ppUserData);
switch_status_t google_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug);
switch_bool_t google_speech_frame(switch_media_bug_t *bug, void* user_data);

#endif