#ifndef __SONIOX_GLUE_H__
#define __SONIOX_GLUE_H__

switch_status_t soniox_speech_init();
switch_status_t soniox_speech_cleanup();
switch_status_t soniox_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, void **ppUserData);
switch_status_t soniox_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug);
switch_bool_t soniox_speech_frame(switch_media_bug_t *bug, void* user_data);

#endif