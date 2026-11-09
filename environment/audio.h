/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private, portable bank installation and per-ball MIDI preferences. */
#ifndef TB_ENVIRONMENT_AUDIO_H
#define TB_ENVIRONMENT_AUDIO_H
#include "ball/store.h"

cJSON *audio_snapshot(TbBall *c);
cJSON *audio_save(TbBall *c, const cJSON *request);
int audio_config(TbBall *c, const char *build, cJSON *env);
cJSON *audio_import_archive(TbBall *c, const cJSON *request);
#endif
