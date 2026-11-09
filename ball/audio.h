/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private, portable bank installation and per-ball MIDI preferences. */
#ifndef TB_BALL_AUDIO_H
#define TB_BALL_AUDIO_H
#include "ball/ball_internal.h"

cJSON *audio_snapshot(Context *c);
cJSON *audio_save(Context *c, const cJSON *request);
int audio_config(Context *c, const char *build, cJSON *env);
cJSON *audio_import_archive(Context *c, const cJSON *request);
#endif
