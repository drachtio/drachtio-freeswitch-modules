#include <switch.h>
#include <grpc++/grpc++.h>
#include <unistd.h>

#include "mod_google_transcribe.h"
#include "google_glue.h"
#include "generic_google_glue.h"

extern "C" {
	switch_status_t google_speech_init() {
		const char* gcsServiceKeyFile = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
		if (gcsServiceKeyFile) {
		try {
			auto creds = grpc::GoogleDefaultCredentials();
		} catch (const std::exception& e) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, 
			"Error initializing google api with provided credentials in %s: %s\n", gcsServiceKeyFile, e.what());
			return SWITCH_STATUS_FALSE;
		}
		}
		return SWITCH_STATUS_SUCCESS;
	}

	switch_status_t google_speech_cleanup() {
		return SWITCH_STATUS_SUCCESS;
	}
}
