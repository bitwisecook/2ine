/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launch planning: the command, environment and receipt of a launch. */
#ifndef TB_EMULATOR_LAUNCH_PLAN_H
#define TB_EMULATOR_LAUNCH_PLAN_H
#include "ball/store.h"

cJSON *launch_prepare(TbBall *c, const cJSON *request);
#endif
