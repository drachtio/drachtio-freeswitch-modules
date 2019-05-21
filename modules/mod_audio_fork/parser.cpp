#include "parser.hpp"
#include <switch.h>

cJSON* parse_json(const char* sessionId, const std::string& data, std::string& type) {
  cJSON* json = NULL;
  const char *szType = NULL;
  switch_core_session_t* session = switch_core_session_locate(sessionId);
	if (session) {
    json = cJSON_Parse(data.c_str());
    if (!json) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "parse - failed parsing json: %s\n", data.c_str());
      goto done;
    }

    szType = cJSON_GetObjectCstr(json, "type");
    if (!szType) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "parse - no type property found in: %s\n", data.c_str());
      cJSON_Delete(json);
      json = NULL;
      goto done;
    }

    type.assign(szType);

done:
		switch_core_session_rwunlock(session);
    return json;
  }
  return NULL;
}
