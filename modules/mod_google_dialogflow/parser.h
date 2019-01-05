#ifndef __PARSER_H__
#define __PARSER_H__

#include <switch_json.h>
#include <grpc++/grpc++.h>
#include "google/cloud/dialogflow/v2beta1/session.grpc.pb.h"

using google::cloud::dialogflow::v2beta1::Sessions;
using google::cloud::dialogflow::v2beta1::StreamingDetectIntentRequest;
using google::cloud::dialogflow::v2beta1::StreamingDetectIntentResponse;
using google::cloud::dialogflow::v2beta1::AudioEncoding;
using google::cloud::dialogflow::v2beta1::InputAudioConfig;
using google::cloud::dialogflow::v2beta1::OutputAudioConfig;
using google::cloud::dialogflow::v2beta1::SynthesizeSpeechConfig;
using google::cloud::dialogflow::v2beta1::VoiceSelectionParams;
using google::cloud::dialogflow::v2beta1::SsmlVoiceGender;
using google::cloud::dialogflow::v2beta1::SsmlVoiceGender_Name;
using google::cloud::dialogflow::v2beta1::QueryInput;
using google::cloud::dialogflow::v2beta1::QueryResult;
using google::cloud::dialogflow::v2beta1::StreamingRecognitionResult;
using google::cloud::dialogflow::v2beta1::StreamingRecognitionResult_MessageType;
using google::cloud::dialogflow::v2beta1::StreamingRecognitionResult_MessageType_Name;
using google::cloud::dialogflow::v2beta1::EventInput;
using google::cloud::dialogflow::v2beta1::OutputAudioEncoding;
using google::cloud::dialogflow::v2beta1::OutputAudioEncoding_Name;
using google::cloud::dialogflow::v2beta1::Context;
using google::cloud::dialogflow::v2beta1::Sentiment;
using google::cloud::dialogflow::v2beta1::SentimentAnalysisResult;
using google::cloud::dialogflow::v2beta1::KnowledgeAnswers;
using google::cloud::dialogflow::v2beta1::KnowledgeAnswers_Answer;
using google::cloud::dialogflow::v2beta1::KnowledgeAnswers_Answer_MatchConfidenceLevel;
using google::cloud::dialogflow::v2beta1::KnowledgeAnswers_Answer_MatchConfidenceLevel_Name;
using google::cloud::dialogflow::v2beta1::Intent;
using google::cloud::dialogflow::v2beta1::Intent_FollowupIntentInfo;
using google::cloud::dialogflow::v2beta1::Intent_WebhookState;
using google::cloud::dialogflow::v2beta1::Intent_WebhookState_Name;
using google::cloud::dialogflow::v2beta1::Intent_Parameter;
using google::cloud::dialogflow::v2beta1::Intent_TrainingPhrase;
using google::cloud::dialogflow::v2beta1::Intent_TrainingPhrase_Type;
using google::cloud::dialogflow::v2beta1::Intent_TrainingPhrase_Part;
using google::cloud::dialogflow::v2beta1::Intent_TrainingPhrase_Type_Name;
using google::cloud::dialogflow::v2beta1::Intent_Message;
using google::cloud::dialogflow::v2beta1::Intent_Message_QuickReplies;
using google::cloud::dialogflow::v2beta1::Intent_Message_Platform_Name;
using google::cloud::dialogflow::v2beta1::Intent_Message_SimpleResponses;
using google::cloud::dialogflow::v2beta1::Intent_Message_SimpleResponse;
using google::cloud::dialogflow::v2beta1::Intent_Message_BasicCard;
using google::cloud::dialogflow::v2beta1::Intent_Message_Card;
using google::cloud::dialogflow::v2beta1::Intent_Message_Image;
using google::cloud::dialogflow::v2beta1::Intent_Message_Text;
using google::cloud::dialogflow::v2beta1::Intent_Message_Card_Button;
using google::cloud::dialogflow::v2beta1::Intent_Message_BasicCard_Button;
using google::cloud::dialogflow::v2beta1::Intent_Message_BasicCard_Button_OpenUriAction;
using google::cloud::dialogflow::v2beta1::Intent_Message_Suggestion;
using google::cloud::dialogflow::v2beta1::Intent_Message_Suggestions;
using google::cloud::dialogflow::v2beta1::Intent_Message_LinkOutSuggestion;
using google::cloud::dialogflow::v2beta1::Intent_Message_ListSelect;
using google::cloud::dialogflow::v2beta1::Intent_Message_CarouselSelect;
using google::cloud::dialogflow::v2beta1::Intent_Message_CarouselSelect_Item;
using google::cloud::dialogflow::v2beta1::Intent_Message_ListSelect_Item;
using google::cloud::dialogflow::v2beta1::Intent_Message_SelectItemInfo;
using google::cloud::dialogflow::v2beta1::Intent_Message_TelephonyPlayAudio;
using google::cloud::dialogflow::v2beta1::Intent_Message_TelephonySynthesizeSpeech;
using google::cloud::dialogflow::v2beta1::Intent_Message_TelephonyTransferCall;
using google::protobuf::RepeatedPtrField;
using google::rpc::Status;
using google::protobuf::Struct;
using google::protobuf::Value;
using google::protobuf::ListValue;

typedef google::protobuf::Map< std::string, Value >::const_iterator StructIterator_t;

class GRPCParser {
public:
    GRPCParser(switch_core_session_t *session) : m_session(session) {}
    ~GRPCParser() {}

    template <typename T> cJSON* parseCollection(const RepeatedPtrField<T> coll) ;
    
    cJSON* parse(const StreamingDetectIntentResponse& response) ;
    const std::string& parseAudio(const StreamingDetectIntentResponse& response);


    cJSON* parse(const OutputAudioEncoding& o) ;
    cJSON* parse(const OutputAudioConfig& o) ;
    cJSON* parse(const SynthesizeSpeechConfig& o) ;
    cJSON* parse(const SsmlVoiceGender& o) ;
    cJSON* parse(const VoiceSelectionParams& o) ;
    cJSON* parse(const google::rpc::Status& o) ;
    cJSON* parse(const Value& value) ;
    cJSON* parse(const Struct& rpcStruct) ;
    cJSON* parse(const Intent_Message_SimpleResponses& o) ;
    cJSON* parse(const Intent_Message_SimpleResponse& o) ;
    cJSON* parse(const Intent_Message_Image& o) ;
    cJSON* parse(const Intent_Message_BasicCard_Button_OpenUriAction& o) ;
    cJSON* parse(const Intent_Message_BasicCard_Button& o) ;
    cJSON* parse(const Intent_Message_Card_Button& o) ;
    cJSON* parse(const Intent_Message_BasicCard& o) ;
    cJSON* parse(const Intent_Message_Card& o) ;
    cJSON* parse(const Intent_Message_Suggestion& o) ;
    cJSON* parse(const Intent_Message_Suggestions& o) ;
    cJSON* parse(const std::string& val) ;
    cJSON* parse(const Intent_Message_LinkOutSuggestion& o) ;
    cJSON* parse(const Intent_Message_SelectItemInfo& o) ;
    cJSON* parse(const Intent_Message_ListSelect_Item& o) ;
    cJSON* parse(const Intent_Message_CarouselSelect& o) ;
    cJSON* parse(const Intent_Message_CarouselSelect_Item& o) ;
    cJSON* parse(const Intent_Message_ListSelect& o) ;
    cJSON* parse(const Intent_Message_TelephonyPlayAudio& o) ;
    cJSON* parse(const Intent_Message_TelephonySynthesizeSpeech& o) ;
    cJSON* parse(const Intent_Message_TelephonyTransferCall& o) ;
    cJSON* parse(const Intent_Message_QuickReplies& o) ;
    cJSON* parse(const Intent_Message_Text& o) ;
    cJSON* parse(const Intent_TrainingPhrase_Part& o) ;
    cJSON* parse(const Intent_WebhookState& o) ;
    cJSON* parse(const Intent_TrainingPhrase_Type& o) ;
    cJSON* parse(const Intent_TrainingPhrase& o) ;
    cJSON* parse(const Intent_Parameter& o) ;
    cJSON* parse(const Intent_FollowupIntentInfo& o) ;
    cJSON* parse(const Sentiment& o) ;
    cJSON* parse(const SentimentAnalysisResult& o) ;
    cJSON* parse(const KnowledgeAnswers_Answer_MatchConfidenceLevel& o) ;
    cJSON* parse(const KnowledgeAnswers_Answer& o) ;
    cJSON* parse(const KnowledgeAnswers& o) ;
    cJSON* parse(const Intent& o) ;
    cJSON* parse(const google::cloud::dialogflow::v2beta1::Context& o) ;
    cJSON* parse(const Intent_Message& msg) ;
    cJSON* parse(const QueryResult& qr) ;
    cJSON* parse(const StreamingRecognitionResult_MessageType& o) ;
    cJSON* parse(const StreamingRecognitionResult& o) ;

private:
    switch_core_session_t *m_session;
} ;


#endif