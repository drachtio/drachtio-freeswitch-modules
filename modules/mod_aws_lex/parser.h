#ifndef __PARSER_H__
#define __PARSER_H__

#include <switch_json.h>
#include <aws/lexv2-runtime/LexRuntimeV2Client.h>
#include <aws/lexv2-runtime/model/StartConversationRequest.h>

using namespace Aws::LexRuntimeV2;
using namespace Aws::LexRuntimeV2::Model;
 
cJSON* lex2Json(const TranscriptEvent& ev);
cJSON* lex2Json(const TextResponseEvent& ev);
cJSON* lex2Json(const Message& msg);
cJSON* lex2Json(const IntentResultEvent& ev);
cJSON* lex2Json(const PlaybackInterruptionEvent& ev);
cJSON* lex2Json(const Aws::Map<Aws::String, Aws::String>& attr);
cJSON* lex2Json(const Aws::Map<Aws::String, Slot>& slots);
cJSON* lex2Json(const SessionState& state) ;
cJSON* lex2Json(const SentimentResponse& sentiment) ;
cJSON* lex2Json(const Intent& intent) ;
cJSON* lex2Json(const DialogAction& dialogAction);
cJSON* lex2Json(const ActiveContext& context);
cJSON* lex2Json(const SentimentScore& score);
cJSON* lex2Json(const ActiveContextTimeToLive& ttl);
cJSON* lex2Json(const Slot& slot);
cJSON* lex2Json(const Value& value) ;
cJSON* lex2Json(const Aws::Client::AWSError<LexRuntimeV2Errors>& err);

#endif