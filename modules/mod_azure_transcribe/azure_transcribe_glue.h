#ifndef __AZURE_GLUE_H__
#define __AZURE_GLUE_H__

switch_status_t azure_transcribe_init();
switch_status_t azure_transcribe_cleanup();
switch_status_t azure_transcribe_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t samples_per_second, uint32_t channels, char* lang, int interim,  void **ppUserData);
switch_status_t azure_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing);
switch_bool_t azure_transcribe_frame(switch_media_bug_t *bug, void* user_data);

#endif