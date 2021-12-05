/* 
 *
 * mod_azure_transcribe.c -- Freeswitch module for using azure streaming transcribe api
 *
 */
#include "mod_azure_transcribe.h"
#include "azure_transcribe_glue.h"

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_azure_transcribe_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_azure_transcribe_load);

SWITCH_MODULE_DEFINITION(mod_azure_transcribe, mod_azure_transcribe_load, mod_azure_transcribe_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session);

static void responseHandler(switch_core_session_t* session, const char* eventName, const char * json) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "responseHandler event %s, body %s.\n", eventName, json);
	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName);
	switch_channel_event_set_data(channel, event);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "microsoft");
	if (json) switch_event_add_body(event, "%s", json);
	switch_event_fire(&event);
}

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_INIT.\n");
		break;

	case SWITCH_ABC_TYPE_CLOSE:
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_CLOSE.\n");

			azure_transcribe_session_stop(session, 1);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:

		return azure_transcribe_frame(bug, user_data);
		break;

	case SWITCH_ABC_TYPE_WRITE:
	default:
		break;
	}

	return SWITCH_TRUE;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, 
  char* lang, int interim)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_codec_implementation_t read_impl = { 0 };
	void *pUserData;
	uint32_t samples_per_second;


	if (switch_channel_get_private(channel, MY_BUG_NAME)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "removing bug from previous transcribe\n");
		do_stop(session);
	}

	switch_core_session_get_read_impl(session, &read_impl);

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_FALSE;
	}

	samples_per_second = !strcasecmp(read_impl.iananame, "g722") ? read_impl.actual_samples_per_second : read_impl.samples_per_second;

	if (SWITCH_STATUS_FALSE == azure_transcribe_session_init(session, responseHandler, 
		samples_per_second, flags & SMBF_STEREO ? 2 : 1, lang, interim, &pUserData)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing azure speech session.\n");
		return SWITCH_STATUS_FALSE;
	}
	if ((status = switch_core_media_bug_add(session, "azure_transcribe", NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}
  switch_channel_set_private(channel, MY_BUG_NAME, bug);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "added media bug for azure transcribe\n");

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t do_stop(switch_core_session_t *session)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, MY_BUG_NAME);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "do_stop: Received user command command to stop transcribe.\n");
		status = azure_transcribe_session_stop(session, 0);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "do_stop: stopped transcribe.\n");
	}

	return status;
}

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] lang-code [interim] [stereo|mono]"
SWITCH_STANDARD_API(azure_transcribe_function)
{
	char *mycmd = NULL, *argv[5] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	switch_media_bug_flag_t flags = SMBF_READ_STREAM /* | SMBF_WRITE_STREAM | SMBF_READ_PING */;

	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || 
      (!strcasecmp(argv[1], "stop") && argc < 2) ||
      (!strcasecmp(argv[1], "start") && argc < 3) ||
      zstr(argv[0])) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s %s.\n", cmd, argv[0], argv[1]);
		stream->write_function(stream, "-USAGE: %s\n", TRANSCRIBE_API_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			if (!strcasecmp(argv[1], "stop")) {
    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "stop transcribing\n");
				status = do_stop(lsession);
			} else if (!strcasecmp(argv[1], "start")) {
        char* lang = argv[2];
        int interim = argc > 3 && !strcmp(argv[3], "interim");
				if (argc > 4 && !strcmp(argv[4], "stereo")) {
          flags |= SMBF_WRITE_STREAM ;
          flags |= SMBF_STEREO;
				}
    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "start transcribing %s %s\n", lang, interim ? "interim": "complete");
				status = start_capture(lsession, flags, lang, interim);
			}
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


SWITCH_MODULE_LOAD_FUNCTION(mod_azure_transcribe_load)
{
	switch_api_interface_t *api_interface;

	/* create/register custom event message type */
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_RESULTS) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_RESULTS);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_START_OF_UTTERANCE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_START_OF_UTTERANCE);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_END_OF_UTTERANCE);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_NO_SPEECH_DETECTED) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_NO_SPEECH_DETECTED);
		return SWITCH_STATUS_TERM;
	}

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "azure Speech Transcription API loading..\n");

  if (SWITCH_STATUS_FALSE == azure_transcribe_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing azure speech interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "azure Speech Transcription API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "uuid_azure_transcribe", "azure Speech Transcription API", azure_transcribe_function, TRANSCRIBE_API_SYNTAX);
	switch_console_set_complete("add uuid_azure_transcribe start lang-code [interim|final] [stereo|mono]");
	switch_console_set_complete("add uuid_azure_transcribe stop ");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_azure_transcribe_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_azure_transcribe_shutdown)
{
	azure_transcribe_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	switch_event_free_subclass(TRANSCRIBE_EVENT_START_OF_UTTERANCE);
	switch_event_free_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE);
	switch_event_free_subclass(TRANSCRIBE_EVENT_NO_SPEECH_DETECTED);
	return SWITCH_STATUS_SUCCESS;
}
