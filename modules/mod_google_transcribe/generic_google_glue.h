#ifndef __GENERIC_GOOGLE_GLUE_H__
#define __GENERIC_GOOGLE_GLUE_H__

template<typename Streamer>
switch_bool_t google_speech_frame(switch_media_bug_t *bug, void* user_data) {
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);
	struct cap_cb *cb = (struct cap_cb *) user_data;
    if (cb->streamer && (!cb->wants_single_utterance || !cb->got_end_of_utterance)) {
        Streamer* streamer = (Streamer *) cb->streamer;
        uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = {};
        frame.data = data;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

        if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
            while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
                if (frame.datalen) {
                    if (cb->vad && !streamer->isConnected()) {
                        switch_vad_state_t state = switch_vad_process(cb->vad, (int16_t*) frame.data, frame.samples);
                        if (state == SWITCH_VAD_STATE_START_TALKING) {
                            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "detected speech, connect to google speech now\n");
                            streamer->connect();
                            cb->responseHandler(session, "vad_detected", cb->bugname);
                        }
                    }

                    if (cb->resampler) {
                        spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
                        spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
                        spx_uint32_t in_len = frame.samples;
                        size_t written;

                        speex_resampler_process_interleaved_int(cb->resampler,
                            (const spx_int16_t *) frame.data,
                            (spx_uint32_t *) &in_len,
                            &out[0],
                            &out_len);
                        streamer->write( &out[0], sizeof(spx_int16_t) * out_len);
                    }
                    else {
                        streamer->write( frame.data, sizeof(spx_int16_t) * frame.samples);
                    }
                }
            }
            switch_mutex_unlock(cb->mutex);
        }
	}
	return SWITCH_TRUE;
}

template<typename Streamer>
switch_status_t google_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler,
		switch_thread_start_t func, uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang,
		int interim, char *bugname, int single_utterance, int separate_recognition, int max_alternatives,
		int profanity_filter, int word_time_offset, int punctuation, const char* model, int enhanced,
		const char* hints, char* play_file, void **ppUserData) {

	switch_channel_t *channel = switch_core_session_get_channel(session);
	auto read_codec = switch_core_session_get_read_codec(session);
	uint32_t sampleRate = read_codec->implementation->actual_samples_per_second;
	struct cap_cb *cb;
	int err;

	cb =(struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
	strncpy(cb->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
	strncpy(cb->bugname, bugname, MAX_BUG_LEN);
	cb->got_end_of_utterance = 0;
	cb->wants_single_utterance = single_utterance;
	if (play_file != NULL){
		cb->play_file = 1;
	}
	
	switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
	if (sampleRate != to_rate) {
		cb->resampler = speex_resampler_init(channels, sampleRate, to_rate, SWITCH_RESAMPLE_QUALITY, &err);
	if (0 != err) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n",
								switch_channel_get_name(channel), speex_resampler_strerror(err));
		return SWITCH_STATUS_FALSE;
	}
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s: no resampling needed for this call\n", switch_channel_get_name(channel));
	}
	cb->responseHandler = responseHandler;

	// allocate vad if we are delaying connecting to the recognizer until we detect speech
	if (switch_channel_var_true(channel, "START_RECOGNIZING_ON_VAD")) {
		cb->vad = switch_vad_init(sampleRate, channels);
		if (cb->vad) {
			const char* var;
			int mode = 2;
			int silence_ms = 150;
			int voice_ms = 250;
			int debug = 0;

			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_MODE")) {
				mode = atoi(var);
			}
			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_SILENCE_MS")) {
				silence_ms = atoi(var);
			}
			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_VOICE_MS")) {
				voice_ms = atoi(var);
			}
			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_VOICE_MS")) {
				voice_ms = atoi(var);
			}
			switch_vad_set_mode(cb->vad, mode);
			switch_vad_set_param(cb->vad, "silence_ms", silence_ms);
			switch_vad_set_param(cb->vad, "voice_ms", voice_ms);
			switch_vad_set_param(cb->vad, "debug", debug);
		}
	}

    if (!switch_channel_get_variable(channel, "GOOGLE_SPEECH_RECOGNIZER_PARENT")) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
            "%s: Error initializing gstreamer: GOOGLE_SPEECH_RECOGNIZER_PARENT is not set.\n", switch_channel_get_name(channel));
        return SWITCH_STATUS_FALSE;
    }

	Streamer *streamer = NULL;
	try {
	    streamer = new Streamer(session, channels, lang, interim, to_rate, sampleRate, single_utterance, separate_recognition, max_alternatives,
		    profanity_filter, word_time_offset, punctuation, model, enhanced, hints);
	    cb->streamer = streamer;
	} catch (std::exception& e) {
	    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
		switch_channel_get_name(channel), e.what());
	    return SWITCH_STATUS_FALSE;
	}

	if (!cb->vad) streamer->connect();

	// create the read thread
	switch_threadattr_t *thd_attr = NULL;
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);

	switch_threadattr_create(&thd_attr, pool);
	switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
	switch_thread_create(&cb->thread, thd_attr, func, cb, pool);

	*ppUserData = cb;
	return SWITCH_STATUS_SUCCESS;
}

template<typename Streamer>
switch_status_t google_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
	switch_channel_t *channel = switch_core_session_get_channel(session);

	if (bug) {
		struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
		switch_mutex_lock(cb->mutex);

		if (!switch_channel_get_private(channel, cb->bugname)) {
			// race condition
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached (race).\n", switch_channel_get_name(channel));
			switch_mutex_unlock(cb->mutex);
			return SWITCH_STATUS_FALSE;
		}
		switch_channel_set_private(channel, cb->bugname, NULL);

		// stop playback if available
		if (cb->play_file == 1){ 
			if (switch_channel_test_flag(channel, CF_BROADCAST)) {
				switch_channel_stop_broadcast(channel);
			} else {
				switch_channel_set_flag_value(channel, CF_BREAK, 1);
			}
		}

		// close connection and get final responses
		Streamer* streamer = (Streamer *) cb->streamer;

		if (streamer) {
			streamer->writesDone();

			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_speech_session_cleanup: GStreamer (%p) waiting for read thread to complete\n", (void*)streamer);
			switch_status_t st;
			switch_thread_join(&st, cb->thread);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_speech_session_cleanup:  GStreamer (%p) read thread completed\n", (void*)streamer);

			delete streamer;
			cb->streamer = NULL;
		}

		if (cb->resampler) {
			speex_resampler_destroy(cb->resampler);
		}
		if (cb->vad) {
			switch_vad_destroy(&cb->vad);
			cb->vad = nullptr;
		}
		if (!channelIsClosing) {
			switch_core_media_bug_remove(session, &bug);
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_speech_session_cleanup: Closed stream\n");

		switch_mutex_unlock(cb->mutex);

		return SWITCH_STATUS_SUCCESS;
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
	return SWITCH_STATUS_FALSE;
}

#endif