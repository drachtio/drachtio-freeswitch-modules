#ifndef __GOOGLE_GLUE_H__
#define __GOOGLE_GLUE_H__

switch_status_t google_speech_load();
switch_status_t google_speech_open(google_t* google);
switch_status_t google_speech_feed_tts(google_t* google, char* text);
switch_status_t google_speech_unload();


#endif