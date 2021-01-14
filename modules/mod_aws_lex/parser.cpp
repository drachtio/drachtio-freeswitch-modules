#include "parser.h"
#include <switch.h>



cJSON* lex2Json(const TranscriptEvent& ev) {
	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "transcript", cJSON_CreateString(ev.GetTranscript().c_str()));
  cJSON_AddItemToObject(json, "eventId", cJSON_CreateString(ev.GetEventId().c_str()));

  return json;
}

cJSON* lex2Json(const TextResponseEvent& ev) {
	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "eventId", cJSON_CreateString(ev.GetEventId().c_str()));

	cJSON* jMessages = cJSON_CreateArray();
  cJSON_AddItemToObject(json, "messages", jMessages);
	for (auto msg : ev.GetMessages()) {
    cJSON_AddItemToArray(jMessages, lex2Json(msg));
  }
  return json;
}

cJSON* lex2Json(const Message& msg) {
	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "msg", cJSON_CreateString(msg.GetContent().c_str()));
  cJSON_AddItemToObject(json, "type", 
    cJSON_CreateString(MessageContentTypeMapper::GetNameForMessageContentType(msg.GetContentType()).c_str()));

  return json;
}

cJSON* lex2Json(const IntentResultEvent& ev) {
 	cJSON * json = cJSON_CreateObject();
 
  cJSON_AddItemToObject(json, "eventId", cJSON_CreateString(ev.GetEventId().c_str()));
  cJSON_AddItemToObject(json, "sessionId", cJSON_CreateString(ev.GetSessionId().c_str()));
  cJSON_AddItemToObject(json, "sessionState", lex2Json(ev.GetSessionState()));
  cJSON_AddItemToObject(json, "requestAttributes", lex2Json(ev.GetRequestAttributes()));

	cJSON* jInterpretations = cJSON_CreateArray();
  cJSON_AddItemToObject(json, "interpretations", jInterpretations);
  for (auto interp : ev.GetInterpretations()) {
    cJSON * jInterp = cJSON_CreateObject();
    cJSON_AddItemToArray(jInterpretations, jInterp);

    cJSON_AddItemToObject(jInterp, "confidence", cJSON_CreateNumber(interp.GetNluConfidence().GetScore()));
    cJSON_AddItemToObject(jInterp, "sentiment", lex2Json(interp.GetSentimentResponse()));
    cJSON_AddItemToObject(jInterp, "intent", lex2Json(interp.GetIntent()));
  }
   return json;
}

cJSON* lex2Json(const PlaybackInterruptionEvent& ev) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "reason", 
    cJSON_CreateString(PlaybackInterruptionReasonMapper::GetNameForPlaybackInterruptionReason(ev.GetEventReason()).c_str()));
  cJSON_AddItemToObject(json, "causedBy", cJSON_CreateString(ev.GetCausedByEventId().c_str()));
  cJSON_AddItemToObject(json, "eventId", cJSON_CreateString(ev.GetEventId().c_str()));

  return json;
}

cJSON* lex2Json(const Aws::Map<Aws::String, Aws::String>& attr) {
 	cJSON * json = cJSON_CreateObject();

  for (auto it: attr) {
		cJSON_AddItemToObject(json, it.first.c_str(), cJSON_CreateString(it.second.c_str()));
  }
  return json;
}

cJSON* lex2Json(const Aws::Map<Aws::String, Slot>& slots) {
 	cJSON * json = cJSON_CreateObject();

  for (auto it: slots) {
		cJSON_AddItemToObject(json, it.first.c_str(), lex2Json(it.second));
  }
  return json;
}

cJSON* lex2Json(const SessionState& state) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "dialogAction", lex2Json(state.GetDialogAction()));
  cJSON_AddItemToObject(json, "intent", lex2Json(state.GetIntent()));

  cJSON* jContexts = cJSON_CreateArray();
  cJSON_AddItemToObject(json, "activeContexts", jContexts);
  for (auto context : state.GetActiveContexts()) {
    cJSON_AddItemToArray(jContexts, lex2Json(context));
  }

  cJSON_AddItemToObject(json, "attributes", lex2Json(state.GetSessionAttributes()));

  return json;
}

cJSON* lex2Json(const SentimentResponse& sentiment) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "type", 
    cJSON_CreateString(SentimentTypeMapper::GetNameForSentimentType(sentiment.GetSentiment()).c_str()));
  cJSON_AddItemToObject(json, "score", lex2Json(sentiment.GetSentimentScore()));

  return json;
}

cJSON* lex2Json(const Intent& intent) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "name", cJSON_CreateString(intent.GetName().c_str()));
  cJSON_AddItemToObject(json, "slots", lex2Json(intent.GetSlots()));
  cJSON_AddItemToObject(json, "intentState", cJSON_CreateString(IntentStateMapper::GetNameForIntentState(intent.GetState()).c_str()));
  cJSON_AddItemToObject(json, "confirmationState", cJSON_CreateString(ConfirmationStateMapper::GetNameForConfirmationState(intent.GetConfirmationState()).c_str()));

  return json;
}

cJSON* lex2Json(const DialogAction& dialogAction) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "type", 
    cJSON_CreateString(DialogActionTypeMapper::GetNameForDialogActionType(dialogAction.GetType()).c_str()));
  cJSON_AddItemToObject(json, "slotToElicit", cJSON_CreateString(dialogAction.GetSlotToElicit().c_str()));
  
  return json;
}

cJSON* lex2Json(const ActiveContext& context) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "name", cJSON_CreateString(context.GetName().c_str()));
  cJSON_AddItemToObject(json, "ttl", lex2Json(context.GetTimeToLive()));
  cJSON_AddItemToObject(json, "attributes", lex2Json(context.GetContextAttributes()));

  return json;
}

cJSON* lex2Json(const SentimentScore& score) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "positive", cJSON_CreateNumber(score.GetPositive()));
  cJSON_AddItemToObject(json, "negative", cJSON_CreateNumber(score.GetNegative()));
  cJSON_AddItemToObject(json, "neutral", cJSON_CreateNumber(score.GetNeutral()));
  cJSON_AddItemToObject(json, "mixed", cJSON_CreateNumber(score.GetMixed()));

  return json;
}

cJSON* lex2Json(const ActiveContextTimeToLive& ttl) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "seconds", cJSON_CreateNumber(ttl.GetTimeToLiveInSeconds()));
  cJSON_AddItemToObject(json, "turns", cJSON_CreateNumber(ttl.GetTurnsToLive()));

  return json;
}

cJSON* lex2Json(const Slot& slot) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "value", lex2Json(slot.GetValue()));

  return json;
}

cJSON* lex2Json(const Value& value) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "originalValue", cJSON_CreateString(value.GetOriginalValue().c_str()));
  cJSON_AddItemToObject(json, "interpretedValue", cJSON_CreateString(value.GetInterpretedValue().c_str()));

	cJSON* jResolved = cJSON_CreateArray();
  cJSON_AddItemToObject(json, "resolvedValues", jResolved);
	for (auto res : value.GetResolvedValues()) {
    cJSON_AddItemToArray(jResolved, cJSON_CreateString(res.c_str()));
  }

  return json;
}

cJSON* lex2Json(const Aws::Client::AWSError<LexRuntimeV2Errors>& err) {
 	cJSON * json = cJSON_CreateObject();

  cJSON_AddItemToObject(json, "message", cJSON_CreateString(err.GetMessage().c_str()));

  return json;
}
