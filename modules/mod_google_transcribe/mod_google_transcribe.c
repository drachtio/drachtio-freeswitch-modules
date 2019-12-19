/* 
 *
 * mod_google_transcribe.c -- Freeswitch module for real-time transcription using google's gRPC interface
 *
 */
#include "mod_google_transcribe.h"
#include "google_glue.h"

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_transcribe_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_transcribe_runtime);
SWITCH_MODULE_LOAD_FUNCTION(mod_transcribe_load);

SWITCH_MODULE_DEFINITION(mod_google_transcribe, mod_transcribe_load, mod_transcribe_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session);

static void responseHandler(switch_core_session_t* session, const char * json) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	if (0 == strcmp("end_of_utterance", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_END_OF_UTTERANCE);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("end_of_transcript", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_END_OF_TRANSCRIPT);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("max_duration_exceeded", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("no_audio", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_NO_AUDIO_DETECTED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "json payload: %s.\n", json);

		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_RESULTS);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
		switch_event_add_body(event, "%s", json);
	}
	switch_event_fire(&event);
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

			google_speech_session_cleanup(session, 1);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:

		return google_speech_frame(bug, user_data);
		break;

	case SWITCH_ABC_TYPE_WRITE:
	default:
		break;
	}

	return SWITCH_TRUE;
}

static switch_status_t do_stop(switch_core_session_t *session)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, MY_BUG_NAME);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Received user command command to stop transcription.\n");
		status = google_speech_session_cleanup(session, 0);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stopped transcription.\n");
	}

	return status;
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

	if (SWITCH_STATUS_FALSE == google_speech_session_init(session, responseHandler, samples_per_second, flags & SMBF_STEREO ? 2 : 1, lang, interim, &pUserData)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing google speech session.\n");
		return SWITCH_STATUS_FALSE;
	}
	if ((status = switch_core_media_bug_add(session, "google_transcribe", NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}
  switch_channel_set_private(channel, MY_BUG_NAME, bug);

	return SWITCH_STATUS_SUCCESS;
}

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] [lang-code] [interim]"
SWITCH_STANDARD_API(transcribe_function)
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

SWITCH_MODULE_LOAD_FUNCTION(mod_transcribe_load)
{
	switch_api_interface_t *api_interface;

	/* create/register custom event message type */
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_RESULTS) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_RESULTS);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_END_OF_UTTERANCE);
		return SWITCH_STATUS_TERM;
	}

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech Transcription API loading..\n");

  if (SWITCH_STATUS_FALSE == google_speech_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing google speech interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech Transcription API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "uuid_google_transcribe", "Google Speech Transcription API", transcribe_function, TRANSCRIBE_API_SYNTAX);
	switch_console_set_complete("add uuid_google_transcribe start lang-code");
	switch_console_set_complete("add uuid_google_transcribe stop ");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_google_transcribe_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_transcribe_shutdown)
{
	google_speech_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	switch_event_free_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE);
	return SWITCH_STATUS_SUCCESS;
}
