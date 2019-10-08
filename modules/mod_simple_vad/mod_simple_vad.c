/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Anthony Minessale II <anthm@freeswitch.org>
 *
 *
 *
 */
#include <switch.h>
#include <switch_vad.h>
#include <switch_json.h>

#include <sys/time.h>

#define EVENT_VAD_CHANGE   "mod_simple_vad::change"
#define EVENT_VAD_SUMMARY   "mod_simple_vad::summary"

/* Prototypes */
SWITCH_MODULE_LOAD_FUNCTION(mod_simple_vad_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_simple_vad_shutdown);

/* SWITCH_MODULE_DEFINITION(name, load, shutdown, runtime)
 * Defines a switch_loadable_module_function_table_t and a static const char[] modname
 */
SWITCH_MODULE_DEFINITION(mod_simple_vad, mod_simple_vad_load, mod_simple_vad_shutdown, NULL);

struct cap_cb {
	switch_vad_t *vad;
	switch_mutex_t *mutex;
	struct timeval start;
	struct timeval last_segment_start;
	uint32_t speech_segments;
	long long speech_duration;
	switch_vad_state_t vad_state;
};

long long
timeval_diff(struct timeval *difference,
             struct timeval *end_time,
             struct timeval *start_time
            )
{
  struct timeval temp_diff;

  if(difference==NULL)
  {
    difference=&temp_diff;
  }

  difference->tv_sec =end_time->tv_sec -start_time->tv_sec ;
  difference->tv_usec=end_time->tv_usec-start_time->tv_usec;

  /* Using while instead of if below makes the code slightly more robust. */

  while(difference->tv_usec<0)
  {
    difference->tv_usec+=1000000;
    difference->tv_sec -=1;
  }

  return 1000000LL*difference->tv_sec+
                   difference->tv_usec;

}

static void responseHandler(switch_core_session_t *session, const char * eventName, char * json) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	if (json) switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "responseHandler: sending event payload: %s.\n", json);
	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName);
	switch_channel_event_set_data(channel, event);
	if (json) switch_event_add_body(event, "%s", json);
	switch_event_fire(&event);
}

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);
	switch_channel_t *channel = switch_core_session_get_channel(session);
	struct cap_cb *cb = (struct cap_cb *) user_data;

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
		break;
	case SWITCH_ABC_TYPE_CLOSE:
		{
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "simple_vad SWITCH_ABC_TYPE_CLOSE\n");

			if (cb && cb->vad) {
				switch_vad_destroy(&cb->vad);
				cb->vad = NULL;
			}
			if (cb && cb->mutex) {
				switch_mutex_destroy(cb->mutex);
				cb->mutex = NULL;
			}
			switch_channel_set_private(channel, "simple_vad", NULL);
		}

		break;
	case SWITCH_ABC_TYPE_READ:

		if (cb->vad) {
			uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
			switch_frame_t frame = { 0 };
			frame.data = data;
			frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

			if (cb->start.tv_sec == 0) {
				gettimeofday(&cb->start, NULL);
				gettimeofday(&cb->last_segment_start, NULL);
			}

			if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
				switch_vad_state_t old_state = cb->vad_state;
				while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
					if (frame.datalen) {
						switch_vad_state_t new_state = switch_vad_process(cb->vad, frame.data, frame.samples);
						if (new_state != old_state /* && !(new_state == SWITCH_VAD_STATE_TALKING && old_state == SWITCH_VAD_STATE_START_TALKING ) */) {
							struct timeval now;
							char* json;
							long long duration;
							cJSON *jEvent, *jOldState, *jNewState, *jDuration;

							gettimeofday(&now, NULL);
							jEvent = cJSON_CreateObject();
							jOldState = cJSON_CreateString(switch_vad_state2str(old_state));
							jNewState = cJSON_CreateString(switch_vad_state2str(new_state));
							cJSON_AddItemToObject(jEvent, "oldState", jOldState);							
							cJSON_AddItemToObject(jEvent, "newState", jNewState);
							duration = timeval_diff(NULL, &now, &cb->last_segment_start);
							jDuration = cJSON_CreateNumber(duration/1000);
							cJSON_AddItemToObject(jEvent, "duration", jDuration);
							json = cJSON_PrintUnformatted(jEvent);
							responseHandler(session, EVENT_VAD_CHANGE, json);	
							free(json);
							cJSON_Delete(jEvent);
							cb->vad_state = new_state;
							if (cb->vad_state == SWITCH_VAD_STATE_STOP_TALKING ) cb->speech_duration += duration;
							if (cb->vad_state == SWITCH_VAD_STATE_START_TALKING) cb->speech_segments++;
							gettimeofday(&cb->last_segment_start, NULL);
						}
					}
				}
				switch_mutex_unlock(cb->mutex);
			}
		}
		break;
	default:
		break;
	}

	return SWITCH_TRUE;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, int mode)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_codec_implementation_t read_impl = { 0 };
	struct cap_cb *cb;

	if (switch_channel_get_private(channel, "simple_vad")) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Already Running.\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_core_session_get_read_impl(session, &read_impl);

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_FALSE;
	}

	cb = switch_core_session_alloc(session, sizeof(*cb));
	switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
	cb->vad = switch_vad_init(read_impl.samples_per_second, 1);
	if (!cb->vad) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating vad\n");
		switch_mutex_destroy(cb->mutex);
		return SWITCH_STATUS_FALSE;
	}
	switch_vad_set_mode(cb->vad, mode);
	//switch_vad_set_param(cb->vad, "debug", 10);
	cb->start.tv_sec = 0;
	cb->start.tv_usec = 0;
	cb->speech_segments = 0;
	cb->speech_duration = 0;
	cb->vad_state = SWITCH_VAD_STATE_NONE;

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "simple_vad: starting vad with mode %d\n", mode);

	if ((status = switch_core_media_bug_add(session, "simple_vad", NULL, capture_callback, cb, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}

	switch_channel_set_private(channel, "simple_vad", bug);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t do_stop(switch_core_session_t *session)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, "simple_vad");

	if (bug) {
		struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
		struct timeval now;
		char* json;
		long long duration, speechDuration;
		cJSON *jEvent, *jSpeechSegments, *jDuration, *jSpeechDuration;

		if (!cb) {
			return SWITCH_STATUS_FALSE;
		}

		switch_mutex_lock(cb->mutex);
		gettimeofday(&now, NULL);
		duration = timeval_diff(NULL, &now, &cb->start);
		speechDuration = cb->speech_duration;
		if (cb->vad_state == SWITCH_VAD_STATE_TALKING) {
			speechDuration += timeval_diff(NULL, &now, &cb->last_segment_start);
		}
		jEvent = cJSON_CreateObject();
		jDuration = cJSON_CreateNumber(duration/1000);
		jSpeechDuration = cJSON_CreateNumber(speechDuration/1000);
		jSpeechSegments = cJSON_CreateNumber(cb->speech_segments);
		cJSON_AddItemToObject(jEvent, "speechDuration", jSpeechDuration);
		cJSON_AddItemToObject(jEvent, "speechSegments", jSpeechSegments);
		cJSON_AddItemToObject(jEvent, "totalDuration", jDuration);
		json = cJSON_PrintUnformatted(jEvent);
		responseHandler(session, EVENT_VAD_SUMMARY, json);	
		free(json);
		cJSON_Delete(jEvent);

/*
		if (cb->vad) {
			switch_vad_destroy(&cb->vad);
			cb->vad = NULL;
			switch_channel_set_private(channel, "simple_vad", NULL);
		}
*/
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, 
			"simple_vad: stopped with %lld ms speech in %u segments over a total %lld duration\n",
			speechDuration, cb->speech_segments, duration);

		switch_mutex_unlock(cb->mutex);

		switch_core_media_bug_remove(session, &bug);


		return SWITCH_STATUS_SUCCESS;
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s Bug is not attached.\n", switch_channel_get_name(channel));
	return SWITCH_STATUS_FALSE;

}

#define VAD_API_SYNTAX "<uuid> start|stop [mode]"
SWITCH_STANDARD_API(vad_function)
{
	char *mycmd = NULL, *argv[3] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;

	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || argc < 2 || zstr(argv[0])) {
		stream->write_function(stream, "-USAGE: %s\n", VAD_API_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			if (!strcasecmp(argv[1], "stop")) {
				status = do_stop(lsession);
			} else if (!strcasecmp(argv[1], "start")) {
				switch_media_bug_flag_t flags = SMBF_READ_STREAM;
				int mode = 0;

				if (argc > 2) {
					mode = atoi(argv[2]);
					if (mode < 0 || mode > 3) {
						switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "invalid vad mode %d, must be 0, 1, 2, or 3\n", mode);
					}
				}
				if (mode >= 0 && mode <= 3) status = start_capture(lsession, flags, mode);
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

SWITCH_MODULE_LOAD_FUNCTION(mod_simple_vad_load)
{
	switch_api_interface_t *api_interface;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_simple_vad API loading..\n");

	/* create/register custom event message types */
	if (switch_event_reserve_subclass(EVENT_VAD_CHANGE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register an event subclass EVENT_VAD_CHANGE for mod_simple_vad API.\n");
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(EVENT_VAD_SUMMARY) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register an event subclass EVENT_VAD_SUMMARY for mod_simple_vad API.\n");
		return SWITCH_STATUS_TERM;
	}

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_API(api_interface, "uuid_simple_vad", "mod_simple_vad API", vad_function, VAD_API_SYNTAX);
	switch_console_set_complete("add uuid_simple_vad secs");

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_simple_vad API successfully loaded\n");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_simple_vad_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_simple_vad_shutdown)
{
	switch_event_free_subclass(EVENT_VAD_CHANGE);
	switch_event_free_subclass(EVENT_VAD_SUMMARY);

	return SWITCH_STATUS_SUCCESS;
}
