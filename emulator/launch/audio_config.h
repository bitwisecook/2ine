/* Public domain. No rights reserved. Wholly AI-authored. */
/* The audio.conf projection of a ball's MIDI settings for the runtime. */
#ifndef TB_EMULATOR_LAUNCH_AUDIO_CONFIG_H
#define TB_EMULATOR_LAUNCH_AUDIO_CONFIG_H
#include "ball/store.h"

/* Writes audio.conf and names it, and the selected bank, in env. */
int audio_config(TbBall *c, const char *build, cJSON *env);
#endif
