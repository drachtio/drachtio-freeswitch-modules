#ifndef __MOD_GOOGLE_TTS_H__
#define __MOD_GOOGLE_TTS_H__

#include <switch.h>

struct google_data {
	char *voice_name;
	int rate;
	char *file;
	switch_file_handle_t *fh;
};

typedef struct google_data google_t;

#endif