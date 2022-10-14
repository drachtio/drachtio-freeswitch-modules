#include <cstdlib>
#include <algorithm>
#include <future>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "nuance/rpc/status.pb.h"
#include "nuance/rpc/status_code.pb.h"
#include "nuance/rpc/error_details.pb.h"
#include "nuance/asr/v1/result.pb.h"
#include "nuance/asr/v1/resource.pb.h"
#include "nuance/asr/v1/recognizer.pb.h"

#include "mod_nuance_transcribe.h"
#include "simple_buffer.h"

using nuance::rpc::Status;
using nuance::rpc::StatusCode;
using nuance::asr::v1::Result;
using nuance::asr::v1::EnumResultType;
using nuance::asr::v1::Hypothesis;
using nuance::asr::v1::Notification;
using nuance::asr::v1::UtteranceInfo;
using nuance::asr::v1::Word;
using nuance::asr::v1::RecognitionResource;
using nuance::asr::v1::WakeupWord;
using nuance::asr::v1::EnumResourceType;
using nuance::asr::v1::AudioFormat;
using nuance::asr::v1::ControlMessage;
using nuance::asr::v1::Formatting;
using nuance::asr::v1::PCM;
using nuance::asr::v1::RecognitionFlags;
using nuance::asr::v1::RecognitionInitMessage;
using nuance::asr::v1::RecognitionRequest;
using nuance::asr::v1::StartOfSpeech;
using nuance::asr::v1::StartTimersControlMessage;

#define CHUNKSIZE (320)

namespace {
  int case_insensitive_match(std::string s1, std::string s2) {
   std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
   std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
   if(s1.compare(s2) == 0)
      return 1; //The strings are same
   return 0; //not matched
  }
}



extern "C" {

    switch_status_t nuance_speech_init() {
      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t nuance_speech_cleanup() {
      return SWITCH_STATUS_SUCCESS;
    }
    switch_status_t nuance_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
          uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, int single_utterance,
          int separate_recognition, int max_alternatives, int profanity_filter, int word_time_offset,
          int punctuation, char* model, int enhanced, char* hints, char* play_file, void **ppUserData) {


      return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t nuance_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
      switch_channel_t *channel = switch_core_session_get_channel(session);

			  return SWITCH_STATUS_SUCCESS;
    }

    switch_bool_t nuance_speech_frame(switch_media_bug_t *bug, void* user_data) {

      return SWITCH_TRUE;
    }
}
