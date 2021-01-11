/* 
 *
 * mod_lex.c -- Freeswitch module for running a aws lex conversation
 *
 */
#include "mod_aws_lex.h"
#include "aws_lex_glue.h"

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_aws_lex_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_aws_lex_load);

SWITCH_MODULE_DEFINITION(mod_aws_lex, mod_aws_lex_load, mod_aws_lex_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session);

static void responseHandler(switch_core_session_t* session, const char * type, char * json) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "json payload for type %s: %s.\n", type, json);

	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, type);
	switch_channel_event_set_data(channel, event);
	switch_event_add_body(event, "%s", json);
	switch_event_fire(&event);
}
static void errorHandler(switch_core_session_t* session, const char * json) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, AWS_LEX_EVENT_ERROR);
	switch_channel_event_set_data(channel, event);
	switch_event_add_body(event, "%s", json);

	switch_event_fire(&event);

	do_stop(session);
}

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_INIT.\n");
		break;

	case SWITCH_ABC_TYPE_CLOSE:
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_CLOSE.\n");

			aws_lex_session_stop(session, 1);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:

		return aws_lex_frame(bug, user_data);
		break;

	case SWITCH_ABC_TYPE_WRITE:
	default:
		break;
	}

	return SWITCH_TRUE;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, 
	char* bot, char*alias, char* region, char* locale, char* intent, char* metadata)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_codec_implementation_t read_impl = { 0 };
	struct cap_cb *cb = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	if (switch_channel_get_private(channel, MY_BUG_NAME)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "a lex is already running on this channel, we will stop it.\n");
		do_stop(session);
	}

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "channel must have at least early media to run lex.\n");
		status = SWITCH_STATUS_FALSE;
		goto done;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "starting lex with bot %s, alias %s, region %s, locale %s, intent %s, metadata: %s\n", 
		bot, alias, region, locale, intent ? intent : "(none)", metadata ? metadata : "(none)");

	switch_core_session_get_read_impl(session, &read_impl);
	if (SWITCH_STATUS_FALSE == aws_lex_session_init(session, responseHandler, errorHandler, 
		read_impl.samples_per_second, bot, alias, region, locale, intent, metadata, &cb)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing aws lex session.\n");
		status = SWITCH_STATUS_FALSE;
		goto done;
	}

	if ((status = switch_core_media_bug_add(session, "lex", NULL, capture_callback, (void *) cb, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error adding bug.\n");
		status = SWITCH_STATUS_FALSE;
		goto done;
	}
	switch_channel_set_private(channel, MY_BUG_NAME, bug);

done:
	if (status == SWITCH_STATUS_FALSE) {
		if (cb) destroyChannelUserData(cb);
	}

	return status;
}

static switch_status_t do_stop(switch_core_session_t *session)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, MY_BUG_NAME);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Received user command command to stop lex.\n");
		status = aws_lex_session_stop(session, 0);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stopped lex.\n");
	}

	return status;
}

#define LEX_API_START_SYNTAX "<uuid> bot alias region locale [intent] [json-metadata]"
SWITCH_STANDARD_API(aws_lex_api_start_function)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_READ_STREAM | SMBF_READ_PING;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "command %s\n", cmd);
	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc < 5) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s %s.\n", cmd, argv[0], argv[1]);
		stream->write_function(stream, "-USAGE: %s\n", LEX_API_START_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			char *bot = argv[1];
			char *alias = argv[2];
			char *region = argv[3];
			char *locale = argv[4];
			char *intent = NULL;
			char *metadata = NULL;

			if (argc > 5) {
				if ('{' == *argv[5]) {
					metadata = argv[5];
				}
				else {
					intent = argv[5];
					if (argc > 6) metadata = argv[6];
				}
			}
			status = start_capture(lsession, flags, bot, alias, region, locale, intent, metadata);
			switch_core_session_rwunlock(lsession);
		}
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "+OK Success\n");
	} else {
		stream->write_function(stream, "-ERR Operation Failed\n");
	}

  done:

	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;	
}

#define LEX_API_DTMF_SYNTAX "<uuid> dtmf"
SWITCH_STANDARD_API(aws_lex_api_dtmf_function)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "command %s\n", cmd);
	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc < 2) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s %s.\n", cmd, argv[0], argv[1]);
		stream->write_function(stream, "-USAGE: %s\n", LEX_API_DTMF_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			char *dtmf = argv[1];
			status = aws_lex_session_dtmf(lsession, dtmf);
			switch_core_session_rwunlock(lsession);
		}
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "+OK Success\n");
	} else {
		stream->write_function(stream, "-ERR Operation Failed\n");
	}

  done:

	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;	
}

#define LEX_API_PLAY_DONE_SYNTAX "<uuid>"
SWITCH_STANDARD_API(aws_lex_api_play_done_function)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "command %s\n", cmd);
	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc < 1) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s.\n", cmd, argv[0]);
		stream->write_function(stream, "-USAGE: %s\n", LEX_API_DTMF_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			status = aws_lex_session_play_done(lsession);
			switch_core_session_rwunlock(lsession);
		}
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "+OK Success\n");
	} else {
		stream->write_function(stream, "-ERR Operation Failed\n");
	}

  done:

	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;	
}

#define LEX_API_STOP_SYNTAX "<uuid>"
SWITCH_STANDARD_API(aws_lex_api_stop_function)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "command %s\n", cmd);
	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc != 1) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s.\n", cmd);
		stream->write_function(stream, "-USAGE: %s\n", LEX_API_STOP_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			status = do_stop(lsession);
			switch_core_session_rwunlock(lsession);
		}
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "+OK Success\n");
	} else {
		stream->write_function(stream, "-ERR Operation Failed\n");
	}

  done:

	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;
}


/* Macro expands to: switch_status_t mod_lex_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) */
SWITCH_MODULE_LOAD_FUNCTION(mod_aws_lex_load)
{
	switch_api_interface_t *api_interface;

	/* create/register custom event message types */
	if (switch_event_reserve_subclass(AWS_LEX_EVENT_INTENT) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", AWS_LEX_EVENT_INTENT);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(AWS_LEX_EVENT_TRANSCRIPTION) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", AWS_LEX_EVENT_TRANSCRIPTION);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(AWS_LEX_EVENT_TEXT_RESPONSE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", AWS_LEX_EVENT_TEXT_RESPONSE);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(AWS_LEX_EVENT_PLAYBACK_INTERRUPTION) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", AWS_LEX_EVENT_PLAYBACK_INTERRUPTION);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(AWS_LEX_EVENT_AUDIO_PROVIDED) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", AWS_LEX_EVENT_AUDIO_PROVIDED);
		return SWITCH_STATUS_TERM;
	}

	if (switch_event_reserve_subclass(AWS_LEX_EVENT_ERROR) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", AWS_LEX_EVENT_ERROR);
		return SWITCH_STATUS_TERM;
	}


	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_aws_lex API loading..\n");

  if (SWITCH_STATUS_FALSE == aws_lex_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing mod_aws_lex interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_aws_lex API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "aws_lex_start", "Start an aws lex conversation", aws_lex_api_start_function, LEX_API_START_SYNTAX);
	SWITCH_ADD_API(api_interface, "aws_lex_dtmf", "Send a dtmf entry to lex", aws_lex_api_dtmf_function, LEX_API_DTMF_SYNTAX);
	SWITCH_ADD_API(api_interface, "aws_lex_play_done", "Notify lex that a play completed", aws_lex_api_play_done_function, LEX_API_PLAY_DONE_SYNTAX);
	SWITCH_ADD_API(api_interface, "aws_lex_stop", "Terminate a aws lex", aws_lex_api_stop_function, LEX_API_STOP_SYNTAX);

	switch_console_set_complete("add aws_lex_stop");
	switch_console_set_complete("add aws_lex_play_done");
	switch_console_set_complete("add aws_lex_dtmf dtmf-entry");
	switch_console_set_complete("add aws_lex_start project lang");
	switch_console_set_complete("add aws_lex_start bot alias region locale");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_lex_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_aws_lex_shutdown)
{
	aws_lex_cleanup();

	switch_event_free_subclass(AWS_LEX_EVENT_INTENT);
	switch_event_free_subclass(AWS_LEX_EVENT_TRANSCRIPTION);
	switch_event_free_subclass(AWS_LEX_EVENT_TEXT_RESPONSE);
	switch_event_free_subclass(AWS_LEX_EVENT_PLAYBACK_INTERRUPTION);
	switch_event_free_subclass(AWS_LEX_EVENT_AUDIO_PROVIDED);
	switch_event_free_subclass(AWS_LEX_EVENT_ERROR);

	return SWITCH_STATUS_SUCCESS;
}
