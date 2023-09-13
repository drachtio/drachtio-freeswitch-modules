#ifndef __COBALT_GLUE_H__
#define __COBALT_GLUE_H__

switch_status_t cobalt_speech_init();
switch_status_t cobalt_speech_cleanup();
switch_status_t cobalt_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, char* hostport, 
		uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, void **ppUserData);
switch_status_t cobalt_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug);
switch_bool_t cobalt_speech_frame(switch_media_bug_t *bug, void* user_data);
switch_status_t cobalt_speech_list_models(switch_core_session_t *session, char* hostport);
switch_status_t cobalt_speech_get_version(switch_core_session_t *session, char* hostport);
switch_status_t cobalt_speech_compile_context(switch_core_session_t *session, char* hostport, char* model, char* token, char* phrases);

#endif