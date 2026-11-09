/* Public domain. No rights reserved. Wholly AI-authored. */
/* Validation of the runtime's system.* settings. */
#ifndef TB_EMULATOR_LAUNCH_SETTINGS_SCHEMA_H
#define TB_EMULATOR_LAUNCH_SETTINGS_SCHEMA_H
#include "ball/store.h"

const char *launch_setting_text(TbBall *c, const cJSON *v);
int launch_validate_system(TbBall *c);
#endif
