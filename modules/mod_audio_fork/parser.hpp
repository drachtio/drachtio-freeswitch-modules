#ifndef __PARSER_H__
#define __PARSER_H__

#include <string>
#include <switch_json.h>

cJSON* parse_json(const char* sessionId, const std::string& data, std::string& type) ;

#endif
