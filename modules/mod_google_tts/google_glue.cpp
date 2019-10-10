#include <unordered_set>
#include <switch.h>
#include <grpc++/grpc++.h>
#include <sstream>
#include <fstream>
#include "google/cloud/texttospeech/v1/cloud_tts.grpc.pb.h"

#include "mod_google_tts.h"

using google::cloud::texttospeech::v1::TextToSpeech;
using google::cloud::texttospeech::v1::SynthesizeSpeechRequest;
using google::cloud::texttospeech::v1::SynthesizeSpeechResponse;
using google::cloud::texttospeech::v1::ListVoicesRequest;
using google::cloud::texttospeech::v1::ListVoicesResponse;
using google::cloud::texttospeech::v1::Voice;
using google::cloud::texttospeech::v1::SsmlVoiceGender;
using google::cloud::texttospeech::v1::SsmlVoiceGender_Name;
using google::cloud::texttospeech::v1::SynthesisInput;
using google::cloud::texttospeech::v1::AudioEncoding;

std::shared_ptr<grpc::ChannelCredentials> creds;

static std::unordered_set<std::string> setVoices;

extern "C" {
	switch_status_t google_speech_load() {
		try {
			const char* gcsServiceKeyFile = std::getenv("GOOGLE_APPLICATION_CREDENTIALS");
			if (NULL == gcsServiceKeyFile) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, 
					"Error: \"GOOGLE_APPLICATION_CREDENTIALS\" environment variable must be set to path of the file containing service account json key\n");
				return SWITCH_STATUS_FALSE;     
			}
      creds = grpc::GoogleDefaultCredentials();
			auto channel = grpc::CreateChannel("texttospeech.googleapis.com", creds);
			auto stub = TextToSpeech::NewStub(channel);
			ListVoicesRequest request;
			ListVoicesResponse response;
			grpc::ClientContext context;

			grpc::Status status = stub->ListVoices(&context, request, &response);
			if (!status.ok()) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, 
					"Error retrieving voices: %s\n", status.error_message().c_str());
				return SWITCH_STATUS_FALSE;
			}
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Google has %d available TTS voices:\n", response.voices_size());
			for (int i = 0; i < response.voices_size(); i++) {
				std::stringstream str;
				Voice voice = response.voices(i);
				setVoices.insert(voice.name());
				for (int j = 0; j < voice.language_codes_size(); j++) {
					if (j > 0) str << ", ";
					str << voice.language_codes(j);
				}
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "#%d Name: %s, Gender: %s, Hz: %d, languages: %s\n", 
					i + 1, voice.name().c_str(), SsmlVoiceGender_Name(voice.ssml_gender()).c_str(), voice.natural_sample_rate_hertz(),
					str.str().c_str());
			}
			return SWITCH_STATUS_SUCCESS;

		} catch (const std::exception& e) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, 
				"Error initializing google api: %s\n", e.what());
			return SWITCH_STATUS_FALSE;
		}

	}

	switch_status_t google_speech_open(google_t* google) {
		std::string voice = google->voice_name;
		if (setVoices.find(voice) == setVoices.end()) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
				"google_speech_open: Invalid voice name '%s'; there are %ld voices available, and they were logged at INFO level when freeswitch started, so maybe go have a look..\n",
				voice.c_str(), setVoices.size()); 
			return SWITCH_STATUS_FALSE;
		}
		return SWITCH_STATUS_SUCCESS;
	}
	switch_status_t google_speech_feed_tts(google_t* google, char* text) {
		char langCode[6];
		SynthesizeSpeechRequest request;
		SynthesizeSpeechResponse response;
		grpc::ClientContext context;
		auto input = request.mutable_input();
		auto voice = request.mutable_voice();
		auto audio_config = request.mutable_audio_config();
		auto channel = grpc::CreateChannel("texttospeech.googleapis.com", creds);
		auto stub = TextToSpeech::NewStub(channel);

		memset(langCode, '\0', 6);
		strncpy(langCode, google->voice_name, 5);

		if (strstr(text, "<speak>") == text) {
			input->set_ssml(text);
		}
		else {
			input->set_text(text);
		}
		voice->set_name(google->voice_name);
		voice->set_language_code(langCode);
		audio_config->set_audio_encoding(AudioEncoding::LINEAR16);
		audio_config->set_sample_rate_hertz(google->rate);

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "google_speech_feed_tts: synthesizing using voice: %s, language: %s: %s\n", 
			google->voice_name, langCode, text); 

		grpc::Status status = stub->SynthesizeSpeech(&context, request, &response);
		if (!status.ok()) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
				"google_speech_feed_tts: error synthesizing speech: %s: details: %s\n", 
				status.error_message().c_str(), status.error_details().c_str()); 
			return SWITCH_STATUS_FALSE;
		}

		std::ofstream outfile(google->file, std::ofstream::binary);
		outfile << response.audio_content();
		outfile.close();

		return SWITCH_STATUS_SUCCESS;
	}
	switch_status_t google_speech_unload() {
		return SWITCH_STATUS_SUCCESS;
	}

}
