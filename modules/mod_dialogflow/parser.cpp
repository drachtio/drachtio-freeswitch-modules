#include "parser.h"
#include <switch.h>

template <typename T> cJSON* GRPCParser::parseCollection(const RepeatedPtrField<T> coll) {
	cJSON* json = cJSON_CreateArray();
	typename RepeatedPtrField<T>::const_iterator it = coll.begin();
	for (; it != coll.end(); it++) {
		cJSON_AddItemToArray(json, parse(*it));
	}
	return json;
}

const std::string& GRPCParser::parseAudio(const StreamingDetectIntentResponse& response) {
	return response.output_audio();
}

cJSON* GRPCParser::parse(const StreamingDetectIntentResponse& response) {
	cJSON * json = cJSON_CreateObject();

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStrGRPCParser - parsing StreamingDetectIntentResponse\n");

	// response_id
	cJSON_AddItemToObject(json, "response_id",cJSON_CreateString(response.response_id().c_str()));

	// recognition_result
	if (response.has_recognition_result()) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStrGRPCParser - adding recognition result\n");
        cJSON_AddItemToObject(json, "recognition_result", parse(response.recognition_result()));
	}

	// query_result
    if (response.has_query_result()) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStrGRPCParser - adding query result\n");
        cJSON_AddItemToObject(json, "query_result", parse(response.query_result()));
    }

	// alternative_query_results
	cJSON_AddItemToObject(json, "alternative_query_results", parseCollection(response.alternative_query_results()));

	// webhook_status
	cJSON_AddItemToObject(json, "webhook_status", parse(response.webhook_status()));

	//
	if (response.has_output_audio_config()) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStrGRPCParser - adding audio config\n");
		cJSON_AddItemToObject(json, "output_audio_config", parse(response.output_audio_config()));
	}

	// XXXX: not doing anything with output_audio for the moment

	return json;
}

cJSON* GRPCParser::parse(const OutputAudioEncoding& o) {
	return cJSON_CreateString(OutputAudioEncoding_Name(o).c_str());
}

cJSON* GRPCParser::parse(const OutputAudioConfig& o) {
	cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "audio_encoding", parse(o.audio_encoding()));
	cJSON_AddItemToObject(json, "sample_rate_hertz", cJSON_CreateNumber(o.sample_rate_hertz()));
	cJSON_AddItemToObject(json, "synthesize_speech_config", parse(o.synthesize_speech_config()));

	return json;
}

cJSON* GRPCParser::parse(const SynthesizeSpeechConfig& o) {
	cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "speaking_rate", cJSON_CreateNumber(o.speaking_rate()));
	cJSON_AddItemToObject(json, "pitch", cJSON_CreateNumber(o.pitch()));
	cJSON_AddItemToObject(json, "volume_gain_db", cJSON_CreateNumber(o.volume_gain_db()));
	cJSON_AddItemToObject(json, "effects_profile_id", parseCollection(o.effects_profile_id()));
	cJSON_AddItemToObject(json, "voice", parse(o.voice()));

	return json;
}

cJSON* GRPCParser::parse(const SsmlVoiceGender& o) {
	return cJSON_CreateString(SsmlVoiceGender_Name(o).c_str());
}

cJSON* GRPCParser::parse(const VoiceSelectionParams& o) {
	cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(o.name().c_str()));
	cJSON_AddItemToObject(json, "ssml_gender", parse(o.ssml_gender()));

	return json;
}

cJSON* GRPCParser::parse(const google::rpc::Status& o) {
	cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "code", cJSON_CreateNumber(o.code()));
	cJSON_AddItemToObject(json, "message", cJSON_CreateString(o.message().c_str()));

	return json;
}

cJSON* GRPCParser::parse(const Value& value) {
	cJSON* json = NULL;

	switch (value.kind_case()) {
		case Value::KindCase::kNullValue:
			json = cJSON_CreateNull();
			break;

		case Value::KindCase::kNumberValue:
			json = cJSON_CreateNumber(value.number_value());
			break;

		case Value::KindCase::kStringValue:
			json = cJSON_CreateString(value.string_value().c_str());
			break;

		case Value::KindCase::kBoolValue:
			json = cJSON_CreateBool(value.bool_value());
			break;

		case Value::KindCase::kStructValue:
			json = parse(value.struct_value());
			break;

		case Value::KindCase::kListValue:
			{
				const ListValue& list = value.list_value();
				json = cJSON_CreateArray();
				for (int i = 0; i < list.values_size(); i++) {
					const Value& val = list.values(i);
					cJSON_AddItemToArray(json, parse(val));
				}
			} 
			break;
	}

	return json;
}

cJSON* GRPCParser::parse(const Struct& rpcStruct) {
	cJSON* json = cJSON_CreateObject();

	for (StructIterator_t it = rpcStruct.fields().begin(); it != rpcStruct.fields().end(); it++) {
		const std::string& key = it->first;
		const Value& value = it->second;
		cJSON_AddItemToObject(json, key.c_str(), parse(value));
	}
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_SimpleResponse& o) {
    cJSON * json = cJSON_CreateObject();
	cJSON_AddItemToObject(json, "ssml", cJSON_CreateString(o.ssml().c_str()));
	cJSON_AddItemToObject(json, "text_to_speech", cJSON_CreateString(o.text_to_speech().c_str()));
	cJSON_AddItemToObject(json, "display_text", cJSON_CreateString(o.display_text().c_str()));
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_SimpleResponses& o) {
    cJSON * json = cJSON_CreateObject();
	cJSON_AddItemToObject(json, "simple_responses", parseCollection(o.simple_responses()));
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_Image& o) {
    cJSON * json = cJSON_CreateObject();
	cJSON_AddItemToObject(json, "accessibility_text", cJSON_CreateString(o.accessibility_text().c_str()));
	cJSON_AddItemToObject(json, "image_uri", cJSON_CreateString(o.image_uri().c_str()));
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_BasicCard_Button_OpenUriAction& o) {
    cJSON * json = cJSON_CreateObject();
	cJSON_AddItemToObject(json, "uri", cJSON_CreateString(o.uri().c_str()));
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_BasicCard_Button& o) {
    cJSON * json = cJSON_CreateObject();
	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "open_uri_action", parse(o.open_uri_action()));
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_Card_Button& o) {
    cJSON * json = cJSON_CreateObject();
	cJSON_AddItemToObject(json, "text", cJSON_CreateString(o.text().c_str()));
	cJSON_AddItemToObject(json, "postback", parse(o.postback()));
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_BasicCard& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "subtitle", cJSON_CreateString(o.subtitle().c_str()));
	cJSON_AddItemToObject(json, "formatted_text", cJSON_CreateString(o.formatted_text().c_str()));
	cJSON_AddItemToObject(json, "image", parse(o.image()));
	cJSON_AddItemToObject(json, "buttons", parseCollection(o.buttons()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_Card& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "subtitle", cJSON_CreateString(o.subtitle().c_str()));
	cJSON_AddItemToObject(json, "image_uri", cJSON_CreateString(o.image_uri().c_str()));
	cJSON_AddItemToObject(json, "buttons", parseCollection(o.buttons()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_Suggestion& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_Suggestions& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "suggestions", parseCollection(o.suggestions()));

	return json;
}

cJSON* GRPCParser::parse(const std::string& val) {
	return cJSON_CreateString(val.c_str());
}
cJSON* GRPCParser::parse(const Intent_Message_LinkOutSuggestion& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "destination_name", cJSON_CreateString(o.destination_name().c_str()));
	cJSON_AddItemToObject(json, "uri", cJSON_CreateString(o.uri().c_str()));
	
	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_SelectItemInfo& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "key", cJSON_CreateString(o.key().c_str()));
	cJSON_AddItemToObject(json, "synonyms", parseCollection(o.synonyms()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_ListSelect_Item& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "info", parse(o.info()));
	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "description", cJSON_CreateString(o.description().c_str()));
	cJSON_AddItemToObject(json, "image", parse(o.image()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_CarouselSelect& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "items", parseCollection(o.items()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_CarouselSelect_Item& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "info", parse(o.info()));
	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "description", cJSON_CreateString(o.description().c_str()));
	cJSON_AddItemToObject(json, "image", parse(o.image()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_ListSelect& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "items", parseCollection(o.items()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_TelephonyPlayAudio& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "audio_uri", cJSON_CreateString(o.audio_uri().c_str()));

	return json;
}
cJSON* GRPCParser::parse(const Intent_Message_TelephonySynthesizeSpeech& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "text", cJSON_CreateString(o.text().c_str()));
	cJSON_AddItemToObject(json, "ssml", cJSON_CreateString(o.ssml().c_str()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_TelephonyTransferCall& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "phone_number", cJSON_CreateString(o.phone_number().c_str()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_QuickReplies& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "title", cJSON_CreateString(o.title().c_str()));
	cJSON_AddItemToObject(json, "quick_replies", parseCollection(o.quick_replies()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message_Text& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "text", parseCollection(o.text()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_TrainingPhrase_Part& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "text", cJSON_CreateString(o.text().c_str()));
	cJSON_AddItemToObject(json, "entity_type", cJSON_CreateString(o.entity_type().c_str()));
	cJSON_AddItemToObject(json, "alias", cJSON_CreateString(o.alias().c_str()));
	cJSON_AddItemToObject(json, "user", cJSON_CreateBool(o.user_defined()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_WebhookState& o) {
	return cJSON_CreateString(Intent_WebhookState_Name(o).c_str());
}

cJSON* GRPCParser::parse(const Intent_TrainingPhrase_Type& o) {
	return cJSON_CreateString(Intent_TrainingPhrase_Type_Name(o).c_str());
}


cJSON* GRPCParser::parse(const Intent_TrainingPhrase& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(o.name().c_str()));
	cJSON_AddItemToObject(json, "type", parse(o.type()));
	cJSON_AddItemToObject(json, "parts", parseCollection(o.parts()));
	cJSON_AddItemToObject(json, "times_added_count", cJSON_CreateNumber(o.times_added_count()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Parameter& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(o.name().c_str()));
	cJSON_AddItemToObject(json, "display_name", cJSON_CreateString(o.display_name().c_str()));
	cJSON_AddItemToObject(json, "value", cJSON_CreateString(o.value().c_str()));
	cJSON_AddItemToObject(json, "default_value", cJSON_CreateString(o.default_value().c_str()));
	cJSON_AddItemToObject(json, "entity_type_display_name", cJSON_CreateString(o.entity_type_display_name().c_str()));
	cJSON_AddItemToObject(json, "mandatory", cJSON_CreateBool(o.mandatory()));
	cJSON_AddItemToObject(json, "prompts", parseCollection(o.prompts()));
	cJSON_AddItemToObject(json, "is_list", cJSON_CreateBool(o.is_list()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_FollowupIntentInfo& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "followup_intent_name", cJSON_CreateString(o.followup_intent_name().c_str()));
	cJSON_AddItemToObject(json, "parent_followup_intent_name", cJSON_CreateString(o.parent_followup_intent_name().c_str()));
	
	return json;
}

cJSON* GRPCParser::parse(const Sentiment& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "score", cJSON_CreateNumber(o.score()));
	cJSON_AddItemToObject(json, "magnitude", cJSON_CreateNumber(o.magnitude()));
	
	return json;
}

cJSON* GRPCParser::parse(const SentimentAnalysisResult& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "query_text_sentiment", parse(o.query_text_sentiment()));
	
	return json;
}

cJSON* GRPCParser::parse(const KnowledgeAnswers_Answer_MatchConfidenceLevel& o) {
	return cJSON_CreateString(KnowledgeAnswers_Answer_MatchConfidenceLevel_Name(o).c_str());
}

cJSON* GRPCParser::parse(const KnowledgeAnswers_Answer& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "source", cJSON_CreateString(o.source().c_str()));
	cJSON_AddItemToObject(json, "faq_question", cJSON_CreateString(o.faq_question().c_str()));
	cJSON_AddItemToObject(json, "answer", cJSON_CreateString(o.answer().c_str()));
	cJSON_AddItemToObject(json, "match_confidence_level", parse(o.match_confidence_level()));
	cJSON_AddItemToObject(json, "match_confidence", cJSON_CreateNumber(o.match_confidence()));
		
	return json;
}

cJSON* GRPCParser::parse(const KnowledgeAnswers& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "answers", parseCollection(o.answers()));
	
	return json;
}

cJSON* GRPCParser::parse(const Intent& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(o.name().c_str()));
	cJSON_AddItemToObject(json, "display_name", cJSON_CreateString(o.display_name().c_str()));
	cJSON_AddItemToObject(json, "webhook_state", parse(o.webhook_state()));
	cJSON_AddItemToObject(json, "priority", cJSON_CreateNumber(o.priority()));
	cJSON_AddItemToObject(json, "is_fallback", cJSON_CreateBool(o.is_fallback()));
	cJSON_AddItemToObject(json, "ml_disabled", cJSON_CreateBool(o.ml_disabled()));
	cJSON_AddItemToObject(json, "end_interaction", cJSON_CreateBool(o.end_interaction()));
	cJSON_AddItemToObject(json, "input_context_names", parseCollection(o.input_context_names()));
	cJSON_AddItemToObject(json, "events", parseCollection(o.events()));
	cJSON_AddItemToObject(json, "training_phrases", parseCollection(o.training_phrases()));
	cJSON_AddItemToObject(json, "action", cJSON_CreateString(o.action().c_str()));
	cJSON_AddItemToObject(json, "output_contexts", parseCollection(o.output_contexts()));
	cJSON_AddItemToObject(json, "reset_contexts", cJSON_CreateBool(o.reset_contexts()));
	cJSON_AddItemToObject(json, "parameters", parseCollection(o.parameters()));
	cJSON_AddItemToObject(json, "messages", parseCollection(o.messages()));

	cJSON* j = cJSON_CreateArray();
	for (int i = 0; i < o.default_response_platforms_size(); i++) {
		cJSON_AddItemToArray(j, cJSON_CreateString(Intent_Message_Platform_Name(o.default_response_platforms(i)).c_str()));
	}
	cJSON_AddItemToObject(json, "default_response_platforms", j);

	cJSON_AddItemToObject(json, "root_followup_intent_name", cJSON_CreateString(o.root_followup_intent_name().c_str()));
	cJSON_AddItemToObject(json, "followup_intent_info", parseCollection(o.followup_intent_info()));
	
	return json;
}

cJSON* GRPCParser::parse(const google::cloud::dialogflow::v2beta1::Context& o) {
    cJSON * json = cJSON_CreateObject();

	cJSON_AddItemToObject(json, "name", cJSON_CreateString(o.name().c_str()));
	cJSON_AddItemToObject(json, "lifespan_count", cJSON_CreateNumber(o.lifespan_count()));
	if (o.has_parameters()) cJSON_AddItemToObject(json, "parameters", parse(o.parameters()));

	return json;
}

cJSON* GRPCParser::parse(const Intent_Message& msg) {
    cJSON * json = cJSON_CreateObject();

	auto platform = msg.platform();
	cJSON_AddItemToObject(json, "platform", cJSON_CreateString(Intent_Message_Platform_Name(platform).c_str()));
				
	if (msg.has_text()) {
		cJSON_AddItemToObject(json, "text", parse(msg.text()));
	}

	if (msg.has_image()) {
		cJSON_AddItemToObject(json, "image", parse(msg.image()));
	}

	if (msg.has_quick_replies()) {
		cJSON_AddItemToObject(json, "quick_replies", parse(msg.quick_replies()));
	}

	if (msg.has_card()) {
		cJSON_AddItemToObject(json, "card", parse(msg.card()));
	}

	if (msg.has_payload()) {
		cJSON_AddItemToObject(json, "payload", parse(msg.payload()));
	}

	if (msg.has_simple_responses()) {
		cJSON_AddItemToObject(json, "simple_responses", parse(msg.simple_responses()));
	}

	if (msg.has_basic_card()) {
		cJSON_AddItemToObject(json, "basic_card", parse(msg.card()));
	}

	if (msg.has_suggestions()) {
		cJSON_AddItemToObject(json, "suggestions", parse(msg.suggestions()));
	}

	if (msg.has_link_out_suggestion()) {
		cJSON_AddItemToObject(json, "link_out_suggestion", parse(msg.link_out_suggestion()));
	}

	if (msg.has_list_select()) {
		cJSON_AddItemToObject(json, "list_select", parse(msg.list_select()));
	}

	if (msg.has_telephony_play_audio()) {
		cJSON_AddItemToObject(json, "telephony_play_audio", parse(msg.telephony_play_audio()));
	}

	if (msg.has_telephony_synthesize_speech()) {
		cJSON_AddItemToObject(json, "telephony_synthesize_speech", parse(msg.telephony_synthesize_speech()));
	}

	if (msg.has_telephony_transfer_call()) {
		cJSON_AddItemToObject(json, "telephony_transfer_call", parse(msg.telephony_transfer_call()));
	}

    return json;
}


cJSON* GRPCParser::parse(const QueryResult& qr) {
    cJSON * json = cJSON_CreateObject();

    cJSON_AddItemToObject(json, "query_text", cJSON_CreateString(qr.query_text().c_str()));
    cJSON_AddItemToObject(json, "language_code", cJSON_CreateString(qr.language_code().c_str()));
		cJSON_AddItemToObject(json, "speech_recognition_confidence", cJSON_CreateNumber(qr.speech_recognition_confidence()));
    cJSON_AddItemToObject(json, "action", cJSON_CreateString(qr.action().c_str()));
    cJSON_AddItemToObject(json, "parameters", parse(qr.parameters()));
    cJSON_AddItemToObject(json, "all_required_params_present", cJSON_CreateBool(qr.all_required_params_present()));
    cJSON_AddItemToObject(json, "fulfillment_text", cJSON_CreateString(qr.fulfillment_text().c_str()));
    cJSON_AddItemToObject(json, "fulfillment_messages", parseCollection(qr.fulfillment_messages()));
    cJSON_AddItemToObject(json, "webhook_source", cJSON_CreateString(qr.webhook_source().c_str()));
		if (qr.has_webhook_payload()) cJSON_AddItemToObject(json, "webhook_payload", parse(qr.webhook_payload()));
    cJSON_AddItemToObject(json, "output_contexts", parseCollection(qr.output_contexts()));
    cJSON_AddItemToObject(json, "intent", parse(qr.intent()));
    cJSON_AddItemToObject(json, "intent_detection_confidence", cJSON_CreateNumber(qr.intent_detection_confidence()));
		if (qr.has_diagnostic_info()) cJSON_AddItemToObject(json, "diagnostic_info", parse(qr.diagnostic_info()));
		cJSON_AddItemToObject(json, "sentiment_analysis_result", parse(qr.sentiment_analysis_result()));
		cJSON_AddItemToObject(json, "knowledge_answers", parse(qr.knowledge_answers()));

    return json;
}
cJSON* GRPCParser::parse(const StreamingRecognitionResult_MessageType& o) {
	return cJSON_CreateString(StreamingRecognitionResult_MessageType_Name(o).c_str());
}

cJSON* GRPCParser::parse(const StreamingRecognitionResult& o) {
    cJSON * json = cJSON_CreateObject();

    cJSON_AddItemToObject(json, "message_type", parse(o.message_type()));
    cJSON_AddItemToObject(json, "transcript", cJSON_CreateString(o.transcript().c_str()));
    cJSON_AddItemToObject(json, "is_final", cJSON_CreateBool(o.is_final()));
    cJSON_AddItemToObject(json, "confidence", cJSON_CreateNumber(o.confidence()));

    return json;
}
