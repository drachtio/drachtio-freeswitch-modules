#ifndef __LWS_GLUE_H__
#define __LWS_GLUE_H__

#include <switch.h>

#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (128)

typedef void (*responseHandler_t)(const char* sessionId, const char* eventName, char* json);

#endif
