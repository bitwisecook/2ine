/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launch planning: the command, environment and receipt of a launch. */
#ifndef TB_BALL_PREPARE_H
#define TB_BALL_PREPARE_H
#include "ball/store.h"

cJSON *launch_prepare(TbBall *c, const cJSON *request);
#endif
