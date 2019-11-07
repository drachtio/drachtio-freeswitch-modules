/* 
 *
 * mod_dialogflow.c -- Freeswitch module for running a google dialogflow
 *
 */
#include "mod_dialogflow.h"
#include "google_glue.h"

#define DEFAULT_INTENT_TIMEOUT_SECS (30)
#define DIALOGFLOW_INTENT "dialogflow_intent"
#define DIALOGFLOW_INTENT_AUDIO_FILE "dialogflow_intent_audio_file"

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_dialogflow_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_dialogflow_runtime);
SWITCH_MODULE_LOAD_FUNCTION(mod_dialogflow_load);

SWITCH_MODULE_DEFINITION(mod_dialogflow, mod_dialogflow_load, mod_dialogflow_shutdown, NULL);

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

	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, DIALOGFLOW_EVENT_ERROR);
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

			google_dialogflow_session_stop(session, 1);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:

		return google_dialogflow_frame(bug, user_data);
		break;

	case SWITCH_ABC_TYPE_WRITE:
	default:
		break;
	}

	return SWITCH_TRUE;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, char* lang, char*projectId, char* event, char* text)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_codec_implementation_t read_impl = { 0 };
	struct cap_cb *cb = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	if (switch_channel_get_private(channel, MY_BUG_NAME)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "a dialogflow is already running on this channel, we will stop it.\n");
		do_stop(session);
	}

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "channel must have at least early media to run dialogflow.\n");
		status = SWITCH_STATUS_FALSE;
		goto done;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "starting dialogflow with project %s, language %s, event %s, text %s.\n", 
		projectId, lang, event, text);

	switch_core_session_get_read_impl(session, &read_impl);
	if (SWITCH_STATUS_FALSE == google_dialogflow_session_init(session, responseHandler, errorHandler, 
		read_impl.samples_per_second, lang, projectId, event, text, &cb)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing google dialogflow session.\n");
		status = SWITCH_STATUS_FALSE;
		goto done;
	}

	if ((status = switch_core_media_bug_add(session, "dialogflow", NULL, capture_callback, (void *) cb, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
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
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Received user command command to stop dialogflow.\n");
		status = google_dialogflow_session_stop(session, 0);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stopped dialogflow.\n");
	}

	return status;
}

#define DIALOGFLOW_API_START_SYNTAX "<uuid> project-id lang-code [event]"
SWITCH_STANDARD_API(dialogflow_api_start_function)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_READ_STREAM | SMBF_READ_PING;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "command %s\n", cmd);
	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc < 3) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s %s.\n", cmd, argv[0], argv[1]);
		stream->write_function(stream, "-USAGE: %s\n", DIALOGFLOW_API_START_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			char *event = NULL;
			char *text = NULL;
			char *projectId = argv[1];
			char *lang = argv[2];
			if (argc > 3) {
				event = argv[3];
			}
			if (argc > 4) {
				if (0 == strcmp("none", event)) {
					event = NULL;
				}
				text = argv[4];
			}
			status = start_capture(lsession, flags, lang, projectId, event, text);
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

#define DIALOGFLOW_API_STOP_SYNTAX "<uuid>"
SWITCH_STANDARD_API(dialogflow_api_stop_function)
{
	char *mycmd = NULL, *argv[10] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "command %s\n", cmd);
	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc != 1) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s %s.\n", cmd, argv[0], argv[1]);
		stream->write_function(stream, "-USAGE: %s\n", DIALOGFLOW_API_STOP_SYNTAX);
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


/* Macro expands to: switch_status_t mod_dialogflow_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) */
SWITCH_MODULE_LOAD_FUNCTION(mod_dialogflow_load)
{
	switch_api_interface_t *api_interface;

	/* create/register custom event message types */
	if (switch_event_reserve_subclass(DIALOGFLOW_EVENT_INTENT) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", DIALOGFLOW_EVENT_INTENT);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(DIALOGFLOW_EVENT_TRANSCRIPTION) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", DIALOGFLOW_EVENT_TRANSCRIPTION);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(DIALOGFLOW_EVENT_END_OF_UTTERANCE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", DIALOGFLOW_EVENT_END_OF_UTTERANCE);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(DIALOGFLOW_EVENT_AUDIO_PROVIDED) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", DIALOGFLOW_EVENT_AUDIO_PROVIDED);
		return SWITCH_STATUS_TERM;
	}

	if (switch_event_reserve_subclass(DIALOGFLOW_EVENT_ERROR) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", DIALOGFLOW_EVENT_ERROR);
		return SWITCH_STATUS_TERM;
	}


	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Dialogflow API loading..\n");

  if (SWITCH_STATUS_FALSE == google_dialogflow_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing google dialogflow interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Dialogflow API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "dialogflow_start", "Start a google dialogflow", dialogflow_api_start_function, DIALOGFLOW_API_START_SYNTAX);
	SWITCH_ADD_API(api_interface, "dialogflow_stop", "Terminate a google dialogflow", dialogflow_api_stop_function, DIALOGFLOW_API_STOP_SYNTAX);

	switch_console_set_complete("add dialogflow_stop");
	switch_console_set_complete("add dialogflow_start project lang");
	switch_console_set_complete("add dialogflow_start project lang timeout-secs");
	switch_console_set_complete("add dialogflow_start project lang timeout-secs event");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_dialogflow_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_dialogflow_shutdown)
{
	google_dialogflow_cleanup();

	switch_event_free_subclass(DIALOGFLOW_EVENT_INTENT);
	switch_event_free_subclass(DIALOGFLOW_EVENT_TRANSCRIPTION);
	switch_event_free_subclass(DIALOGFLOW_EVENT_END_OF_UTTERANCE);
	switch_event_free_subclass(DIALOGFLOW_EVENT_AUDIO_PROVIDED);
	switch_event_free_subclass(DIALOGFLOW_EVENT_ERROR);

	return SWITCH_STATUS_SUCCESS;
}
