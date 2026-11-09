/* Public domain. No rights reserved. Wholly AI-authored. */
/* The JSON facade over the ball store and the components layered on it. */
#ifndef TB_BALL_FACADE_H
#define TB_BALL_FACADE_H
#include "ball/store.h"

extern const TbBallHooks facade_hooks;
cJSON *facade_snapshot(TbBall *c);
#endif
