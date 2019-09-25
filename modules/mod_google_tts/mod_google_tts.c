/* 
 *
 * mod_google_tts.c -- Google GRPC-based text to speech
 *
 */
#include "mod_google_tts.h"
#include "google_glue.h"

#include <unistd.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_google_tts_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_google_tts_shutdown);
SWITCH_MODULE_DEFINITION(mod_google_tts, mod_google_tts_load, mod_google_tts_shutdown, NULL);


static switch_status_t speech_open(switch_speech_handle_t *sh, const char *voice_name, int rate, int channels, switch_speech_flag_t *flags)
{	
	switch_uuid_t uuid;
	char uuid_str[SWITCH_UUID_FORMATTED_LENGTH + 1];
	char outfile[512] = "";
	google_t *google = switch_core_alloc(sh->memory_pool, sizeof(*google));

	google->voice_name = switch_core_strdup(sh->memory_pool, voice_name);
	google->rate = rate;

	/* Construct temporary file name with a new UUID */
	switch_uuid_get(&uuid);
	switch_uuid_format(uuid_str, &uuid);
	switch_snprintf(outfile, sizeof(outfile), "%s%s%s.tmp.wav", SWITCH_GLOBAL_dirs.temp_dir, SWITCH_PATH_SEPARATOR, uuid_str);
	google->file = switch_core_strdup(sh->memory_pool, outfile);

	google->fh = (switch_file_handle_t *) switch_core_alloc(sh->memory_pool, sizeof(switch_file_handle_t));

	sh->private_info = google;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "speech_open - created file %s for name %s, rate %d\n", 
		google->file, google->voice_name, rate);

	return google_speech_open(google);

}

static switch_status_t speech_close(switch_speech_handle_t *sh, switch_speech_flag_t *flags)
{
	google_t *google = (google_t *) sh->private_info;
	assert(google != NULL);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "speech_close - closing file %s\n", google->file);
	if (switch_test_flag(google->fh, SWITCH_FILE_OPEN)) {
		switch_core_file_close(google->fh);
	}
	unlink(google->file);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t speech_feed_tts(switch_speech_handle_t *sh, char *text, switch_speech_flag_t *flags)
{
	google_t *google = (google_t *) sh->private_info;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "speech_feed_tts\n");
	if (SWITCH_STATUS_SUCCESS != google_speech_feed_tts(google, text)) {
		return SWITCH_STATUS_FALSE;
	}

	if (switch_core_file_open(google->fh, google->file, 0,	//number_of_channels,
							  google->rate,	//samples_per_second,
							  SWITCH_FILE_FLAG_READ | SWITCH_FILE_DATA_SHORT, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to open file: %s\n", google->file);
		return SWITCH_STATUS_FALSE;
	}

	return SWITCH_STATUS_SUCCESS;
}

static void speech_flush_tts(switch_speech_handle_t *sh)
{
	google_t *google = (google_t *) sh->private_info;
	assert(google != NULL);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "speech_flush_tts\n");

	if (google->fh != NULL && google->fh->file_interface != NULL) {
		switch_core_file_close(google->fh);
	}
}

static switch_status_t speech_read_tts(switch_speech_handle_t *sh, void *data, size_t *datalen, switch_speech_flag_t *flags)
{
	google_t *google = (google_t *) sh->private_info;
	size_t my_datalen = *datalen / 2;

	assert(google != NULL);

	if (google->fh->file_interface == (void *)0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "file [%s] has already been closed\n", google->file);
		unlink(google->file);
		return SWITCH_STATUS_FALSE;
	}

	if (switch_core_file_read(google->fh, data, &my_datalen) != SWITCH_STATUS_SUCCESS) {
		switch_core_file_close(google->fh);
		unlink(google->file);
		return SWITCH_STATUS_FALSE;
	}
	*datalen = my_datalen * 2;
	if (datalen == 0) {
		switch_core_file_close(google->fh);
		unlink(google->file);
		return SWITCH_STATUS_BREAK;
	}
	return SWITCH_STATUS_SUCCESS;

}

static void text_param_tts(switch_speech_handle_t *sh, char *param, const char *val)
{

}

static void numeric_param_tts(switch_speech_handle_t *sh, char *param, int val)
{

}

static void float_param_tts(switch_speech_handle_t *sh, char *param, double val)
{

}

SWITCH_MODULE_LOAD_FUNCTION(mod_google_tts_load)
{
	switch_speech_interface_t *speech_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	speech_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_SPEECH_INTERFACE);
	speech_interface->interface_name = "google_tts";
	speech_interface->speech_open = speech_open;
	speech_interface->speech_close = speech_close;
	speech_interface->speech_feed_tts = speech_feed_tts;
	speech_interface->speech_read_tts = speech_read_tts;
	speech_interface->speech_flush_tts = speech_flush_tts;
	speech_interface->speech_text_param_tts = text_param_tts;
	speech_interface->speech_numeric_param_tts = numeric_param_tts;
	speech_interface->speech_float_param_tts = float_param_tts;

	return google_speech_load();
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_google_tts_shutdown)
{

	return SWITCH_STATUS_UNLOAD;
}
