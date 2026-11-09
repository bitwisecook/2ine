/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read-only bootstrap requirements and existing-ball navigation facts. */
#ifndef TB_BALL_LAUNCH_PREREQUISITES_H
#define TB_BALL_LAUNCH_PREREQUISITES_H
#include "ball/store.h"

int installer_support_required(TbBall *c, const char *host);
int launch_command_file(const char *program);
cJSON *launch_prerequisites(TbBall *c, const cJSON *request);
cJSON *ball_navigation_state(TbBall *c);
#endif
