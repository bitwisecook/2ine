/* Public domain. No rights reserved. Wholly AI-authored. */
/* Validation of the runtime's system.* settings. */
#ifndef TB_EMULATOR_LAUNCH_SETTINGS_SCHEMA_H
#define TB_EMULATOR_LAUNCH_SETTINGS_SCHEMA_H
#include "ball/store.h"

/* Accepted display settings; the runtime applies the same range. */
enum
{
    LAUNCH_MIN_DISPLAY_DPI = 72,
    LAUNCH_MAX_DISPLAY_DPI = 384
};

const char *launch_setting_text(TbBall *c, const cJSON *v);
int launch_validate_system(TbBall *c);
#endif
