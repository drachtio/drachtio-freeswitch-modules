#ifndef __AWS_GLUE_H__
#define __AWS_GLUE_H__

switch_status_t aws_lex_init();
switch_status_t aws_lex_cleanup();
switch_status_t aws_lex_session_init(switch_core_session_t *session, responseHandler_t responseHandler, errorHandler_t errorHandler, 
		uint32_t samples_per_second, char* bot, char* alias, char* region, char* locale, char *intent, char* metadata, struct cap_cb **cb);
switch_status_t aws_lex_session_stop(switch_core_session_t *session, int channelIsClosing);
switch_status_t aws_lex_session_dtmf(switch_core_session_t *session, char* dtmf);
switch_status_t aws_lex_session_play_done(switch_core_session_t *session);
switch_bool_t aws_lex_frame(switch_media_bug_t *bug, void* user_data);

void destroyChannelUserData(struct cap_cb* cb);
#endif