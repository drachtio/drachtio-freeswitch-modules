#ifndef __GOOGLE_GLUE_H__
#define __GOOGLE_GLUE_H__

#ifdef __cplusplus
extern "C" {
#endif

switch_status_t google_speech_init();
switch_status_t google_speech_cleanup();
switch_status_t google_speech_session_init_v1(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, int single_utterence,
		int separate_recognition, int max_alternatives, int profanity_filter, int word_time_offset, int punctuation, const char* model, int enhanced, 
		const char* hints, char* play_file, void **ppUserData);
switch_status_t google_speech_session_cleanup_v1(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug);
switch_status_t google_speech_session_init_v2(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, int single_utterence,
		int separate_recognition, int max_alternatives, int profanity_filter, int word_time_offset, int punctuation, const char* model, int enhanced, 
		const char* hints, char* play_file, void **ppUserData);
switch_status_t google_speech_session_cleanup_v2(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug);

switch_bool_t google_speech_frame_v1(switch_media_bug_t *bug, void* user_data);
switch_bool_t google_speech_frame_v2(switch_media_bug_t *bug, void* user_data);

#ifdef __cplusplus
}
#endif

#endif