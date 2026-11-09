/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launch planning: the command, environment and receipt of a launch. */
#ifndef TB_EMULATOR_LAUNCH_PLAN_H
#define TB_EMULATOR_LAUNCH_PLAN_H
#include "ball/store.h"

/* A manifest names at most this many startup application records. */
enum
{
    LAUNCH_MAX_STARTUP_PROGRAMS = 16
};

cJSON *launch_prepare(TbBall *c, const cJSON *request);
#endif
