#ifndef __LWS_GLUE_H__
#define __LWS_GLUE_H__

#include "mod_audio_fork.h"

int parse_ws_uri(const char* szServerUri, char* host, char *path, unsigned int* pPort, int* pSslFlags);

switch_status_t fork_init();
switch_status_t fork_cleanup();
switch_status_t fork_session_init(switch_core_session_t *session, 
		uint32_t samples_per_second, char *host, unsigned int port, char* path, int sslFlags, int channels, char* metadata, void **ppUserData);
switch_status_t fork_session_cleanup(switch_core_session_t *session);
switch_bool_t fork_frame(switch_media_bug_t *bug, void* user_data);
switch_status_t fork_service_thread();
#endif
