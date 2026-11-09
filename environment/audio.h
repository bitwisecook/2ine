/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private, portable bank installation and per-ball MIDI preferences. */
#ifndef TB_ENVIRONMENT_AUDIO_H
#define TB_ENVIRONMENT_AUDIO_H
#include "ball/store.h"

/* MIDI settings: the General MIDI mapping with the bundled bank. */
cJSON *audio_defaults(void);
int audio_validate(TbBall *c, const cJSON *settings);
/* The host path of an installed bank; "default" is the build's bundled bank. */
char *audio_bank_path(TbBall *c, const char *id, const char *build);
cJSON *audio_snapshot(TbBall *c);
cJSON *audio_save(TbBall *c, const cJSON *request);
cJSON *audio_import_archive(TbBall *c, const cJSON *request);
#endif
