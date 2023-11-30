#include <switch.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <unordered_map>

#include "mod_aws_transcribe.h"
#include "simple_buffer.h"
//#include "parser.hpp"
#include "audio_pipe.hpp"
#include "transcribe_manager.hpp"

#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000  320 /*which means each 20ms frame as 320 bytes at 8 khz (1 channel only)*/

namespace {
  static bool hasDefaultCredentials = false;
  static const char* defaultApiKey = nullptr;
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 1, 5));
  static unsigned int idxCallCount = 0;
  static uint32_t playCount = 0;

  //static const char* emptyTranscript = "{\"alternatives\":[{\"transcript\":\"\",\"confidence\":0.0,\"words\":[]}]}";
  static const char* emptyTranscript = "{\"Transcript\":{\"Results\":[]}}";
  static const char* messageStart = "{\"Message\":";

  static void reaper(private_t *tech_pvt) {
    std::shared_ptr<aws::AudioPipe> pAp;
    pAp.reset((aws::AudioPipe *)tech_pvt->pAudioPipe);
    tech_pvt->pAudioPipe = nullptr;

    std::thread t([pAp, tech_pvt]{
      pAp->finish();
      pAp->waitForClose();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s (%u) got remote close\n", tech_pvt->sessionId, tech_pvt->id);
    });
    t.detach();
  }

  static void destroy_tech_pvt(private_t *tech_pvt) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->sessionId, tech_pvt->id);
    if (tech_pvt) {
      if (tech_pvt->pAudioPipe) {
        aws::AudioPipe* p = (aws::AudioPipe *) tech_pvt->pAudioPipe;
        delete p;
        tech_pvt->pAudioPipe = nullptr;
      }
      if (tech_pvt->resampler) {
          speex_resampler_destroy(tech_pvt->resampler);
          tech_pvt->resampler = NULL;
      }

      if (tech_pvt->vad) {
        switch_vad_destroy(&tech_pvt->vad);
        tech_pvt->vad = nullptr;
      }
    }
  }

  static void eventCallback(const char* sessionId, aws::AudioPipe::NotifyEvent_t event, const char* message, bool finished) {
    switch_core_session_t* session = switch_core_session_locate(sessionId);
    if (session) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
      if (bug) {
        private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
        if (tech_pvt) {
          switch (event) {
            case aws::AudioPipe::CONNECT_SUCCESS:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "connection successful\n");
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_SUCCESS, NULL, tech_pvt->bugname, finished);
            break;
            case aws::AudioPipe::CONNECT_FAIL:
            {
              // first thing: we can no longer access the AudioPipe
              std::stringstream json;
              json << "{\"reason\":\"" << message << "\"}";
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_FAIL, (char *) json.str().c_str(), tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "connection failed: %s\n", message);
            }
            break;
            case aws::AudioPipe::CONNECTION_DROPPED:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_DISCONNECT, NULL, tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection dropped from far end\n");
            break;
            case aws::AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection closed gracefully\n");
            break;
            case aws::AudioPipe::MESSAGE:
              if( strstr(message, emptyTranscript)) {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "discarding empty aws transcript\n");
              }
              else if (0 == strncmp( message, messageStart, strlen(messageStart))) {
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_ERROR, message, tech_pvt->bugname, finished);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "error message from aws: %s\n", message);
              }
              else {
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_RESULTS, message, tech_pvt->bugname, finished);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws message: %s.\n", message);
              }
            break;

            default:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "got unexpected msg from aws %d:%s\n", event, message);
              break;
          }
        }
      }
      switch_core_session_rwunlock(session);
    }
  }

  void lws_logger(int level, const char *line) {
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;

    switch (level) {
      case LLL_ERR: llevel = SWITCH_LOG_ERROR; break;
      case LLL_WARN: llevel = SWITCH_LOG_WARNING; break;
      case LLL_NOTICE: llevel = SWITCH_LOG_NOTICE; break;
      case LLL_INFO: llevel = SWITCH_LOG_INFO; break;
      break;
    }
	  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "%s\n", line);
  }
}


extern "C" {
  switch_status_t aws_transcribe_init() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_aws_transcribe: audio buffer (in secs):    %d secs\n", nAudioBufferSecs);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_aws_transcribe: lws service threads:       %d\n", nServiceThreads);
 
    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE || LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;
    
    aws::AudioPipe::initialize(nServiceThreads, logs, lws_logger);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AudioPipe::initialize completed\n");

		return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t aws_transcribe_cleanup() {
    bool cleanup = false;
    cleanup = aws::AudioPipe::deinitialize();
    if (cleanup == true) {
        return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }
	
  	// start transcribe on a channel
	switch_status_t aws_transcribe_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
          uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char* bugname, void **ppUserData
	) {
		switch_status_t status = SWITCH_STATUS_SUCCESS;
		switch_channel_t *channel = switch_core_session_get_channel(session);
		int err;
    uint32_t desiredSampling = 8000;
		switch_threadattr_t *thd_attr = NULL;
		switch_memory_pool_t *pool = switch_core_session_get_pool(session);
		auto read_codec = switch_core_session_get_read_codec(session);
		uint32_t sampleRate = read_codec->implementation->actual_samples_per_second;
    switch_codec_implementation_t read_impl;
    switch_core_session_get_read_impl(session, &read_impl);

		private_t* tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));
		memset(tech_pvt, sizeof(tech_pvt), 0);
		const char* awsAccessKeyId = switch_channel_get_variable(channel, "AWS_ACCESS_KEY_ID");
		const char* awsSecretAccessKey = switch_channel_get_variable(channel, "AWS_SECRET_ACCESS_KEY");
		const char* awsRegion = switch_channel_get_variable(channel, "AWS_REGION");
		const char* awsSessionToken = switch_channel_get_variable(channel, "AWS_SESSION_TOKEN");
		tech_pvt->channels = channels;
		strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
		strncpy(tech_pvt->bugname, bugname, MAX_BUG_LEN);

		if (awsAccessKeyId && awsSecretAccessKey && awsRegion && awsSessionToken) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Using channel vars for aws authentication\n");
			strncpy(tech_pvt->awsAccessKeyId, awsAccessKeyId, 128);
			strncpy(tech_pvt->awsSecretAccessKey, awsSecretAccessKey, 128);
			strncpy(tech_pvt->awsSessionToken, awsSessionToken, MAX_SESSION_TOKEN_LEN);
			strncpy(tech_pvt->region, awsRegion, MAX_REGION);
		}
		else if (std::getenv("AWS_ACCESS_KEY_ID") &&
			std::getenv("AWS_SECRET_ACCESS_KEY") &&
			std::getenv("AWS_REGION")) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "Using env vars for aws authentication\n");
			strncpy(tech_pvt->awsAccessKeyId, std::getenv("AWS_ACCESS_KEY_ID"), 128);
			strncpy(tech_pvt->awsSecretAccessKey, std::getenv("AWS_SECRET_ACCESS_KEY"), 128);		
			strncpy(tech_pvt->region, std::getenv("AWS_REGION"), MAX_REGION);
		}
		else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "No channel vars or env vars for aws authentication..will use default profile if found\n");
		}

		tech_pvt->responseHandler = responseHandler;

		tech_pvt->interim = interim;
		strncpy(tech_pvt->lang, lang, MAX_LANG);
		tech_pvt->samples_per_second = sampleRate;
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "sample rate of rtp stream is %d\n", samples_per_second);

    const char* vocabularyName = switch_channel_get_variable(channel, "AWS_VOCABULARY_NAME");
    const char* vocabularyFilterName = switch_channel_get_variable(channel, "AWS_VOCABULARY_FILTER_NAME");
    const char* vocabularyFilterMethod = switch_channel_get_variable(channel, "AWS_VOCABULARY_FILTER_METHOD");

    std::string host, path;
    TranscribeManager::getSignedWebsocketUrl(
      host,
      path,
      tech_pvt->awsAccessKeyId,
      tech_pvt->awsSecretAccessKey, 
      tech_pvt->awsSessionToken, 
      tech_pvt->region,
      lang,
      vocabularyName,
      vocabularyFilterName,
      vocabularyFilterMethod
    );
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connecting to host %s, path %s\n", host.c_str(), path.c_str());

    strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    strncpy(tech_pvt->host, host.c_str(), MAX_WS_URL_LEN);
    tech_pvt->port = 8443;
    strncpy(tech_pvt->path, path.c_str(), MAX_PATH_LEN);    
    tech_pvt->responseHandler = responseHandler;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->buffer_overrun_notified = 0;

    size_t buflen = LWS_PRE + (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD * nAudioBufferSecs);

    aws::AudioPipe* ap = new aws::AudioPipe(tech_pvt->sessionId, tech_pvt->host, tech_pvt->port, tech_pvt->path, 
      buflen, read_impl.decoded_bytes_per_packet, eventCallback);
    if (!ap) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe\n");
      return SWITCH_STATUS_FALSE;
    }

    tech_pvt->pAudioPipe = static_cast<void *>(ap);

		if (switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error initializing mutex\n");
			status = SWITCH_STATUS_FALSE;
			goto done; 
		}
		if (sampleRate != 8000) {
			tech_pvt->resampler = speex_resampler_init(1, sampleRate, 16000, SWITCH_RESAMPLE_QUALITY, &err);
			if (0 != err) {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n", 
							switch_channel_get_name(channel), speex_resampler_strerror(err));
				status = SWITCH_STATUS_FALSE;
				goto done;
			}
		}

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connecting now\n");
    ap->connect();
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection in progress\n");


		*ppUserData = tech_pvt;
	
	done:
		return status;
	}

	switch_status_t aws_transcribe_session_stop(switch_core_session_t *session,int channelIsClosing, char* bugname) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "aws_transcribe_session_stop: no bug - websocket conection already closed\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    uint32_t id = tech_pvt->id;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) aws_transcribe_session_stop\n", id);

    if (!tech_pvt) return SWITCH_STATUS_FALSE;
      
    // close connection and get final responses
    switch_mutex_lock(tech_pvt->mutex);
    switch_channel_set_private(channel, bugname, NULL);
    if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

    aws::AudioPipe *pAudioPipe = static_cast<aws::AudioPipe *>(tech_pvt->pAudioPipe);
    if (pAudioPipe) reaper(tech_pvt);
    destroy_tech_pvt(tech_pvt);
    switch_mutex_unlock(tech_pvt->mutex);
    switch_mutex_destroy(tech_pvt->mutex);
    tech_pvt->mutex = nullptr;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) aws_transcribe_session_stop\n", id);
    return SWITCH_STATUS_SUCCESS;
  }
	
	switch_bool_t aws_transcribe_frame(switch_media_bug_t *bug, void* user_data) {
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    size_t inuse = 0;
    bool dirty = false;
    char *p = (char *) "{\"msg\": \"buffer overrun\"}";

    if (!tech_pvt) return SWITCH_TRUE;
    
    if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
      if (!tech_pvt->pAudioPipe) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }
      aws::AudioPipe *pAudioPipe = static_cast<aws::AudioPipe *>(tech_pvt->pAudioPipe);
      if (pAudioPipe->getLwsState() != aws::AudioPipe::LWS_CLIENT_CONNECTED) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }

      pAudioPipe->lockAudioBuffer();
      size_t available = pAudioPipe->binarySpaceAvailable();
      if (NULL == tech_pvt->resampler) {
        switch_frame_t frame = { 0 };
        frame.data = pAudioPipe->binaryWritePtr();
        frame.buflen = available;
        while (true) {

          // check if buffer would be overwritten; dump packets if so
          if (available < pAudioPipe->binaryMinSpace()) {
            if (!tech_pvt->buffer_overrun_notified) {
              tech_pvt->buffer_overrun_notified = 1;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
            }
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets!\n", 
              tech_pvt->id);
            pAudioPipe->binaryWritePtrResetToZero();

            frame.data = pAudioPipe->binaryWritePtr();
            frame.buflen = available = pAudioPipe->binarySpaceAvailable();
          }

          switch_status_t rv = switch_core_media_bug_read(bug, &frame, SWITCH_TRUE);
          if (rv != SWITCH_STATUS_SUCCESS) break;
          if (frame.datalen) {
            pAudioPipe->binaryWritePtrAdd(frame.datalen);
            frame.buflen = available = pAudioPipe->binarySpaceAvailable();
            frame.data = pAudioPipe->binaryWritePtr();
            dirty = true;
          }
        }
      }
      else {
        uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = { 0 };
        frame.data = data;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
          if (frame.datalen) {
            spx_uint32_t out_len = available >> 1;  // space for samples which are 2 bytes
            spx_uint32_t in_len = frame.samples;

            speex_resampler_process_interleaved_int(tech_pvt->resampler, 
              (const spx_int16_t *) frame.data, 
              (spx_uint32_t *) &in_len, 
              (spx_int16_t *) ((char *) pAudioPipe->binaryWritePtr()),
              &out_len);

            if (out_len > 0) {
              // bytes written = num samples * 2 * num channels
              size_t bytes_written = out_len << tech_pvt->channels;
              pAudioPipe->binaryWritePtrAdd(bytes_written);
              available = pAudioPipe->binarySpaceAvailable();
              dirty = true;
            }
            if (available < pAudioPipe->binaryMinSpace()) {
              if (!tech_pvt->buffer_overrun_notified) {
                tech_pvt->buffer_overrun_notified = 1;
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "(%u) dropping packets!\n", 
                  tech_pvt->id);
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
              }
              break;
            }
          }
        }
      }

      pAudioPipe->unlockAudioBuffer();
      switch_mutex_unlock(tech_pvt->mutex);
    }
    return SWITCH_TRUE;
  }
}
