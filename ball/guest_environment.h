/* Public domain. No rights reserved. Wholly AI-authored. */
/* The guest environment: CONFIG.SYS assignments and ball overrides. */
#ifndef TB_BALL_GUEST_ENVIRONMENT_H
#define TB_BALL_GUEST_ENVIRONMENT_H
#include "ball/ball_internal.h"

extern const char *os2_default_environment_names[];
extern const char *os2_default_environment_values[];
cJSON *tb_ball_guest_environment(Context *c, const cJSON *overrides);
#endif
