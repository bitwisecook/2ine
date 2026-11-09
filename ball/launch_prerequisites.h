/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read-only bootstrap requirements and existing-ball navigation facts. */
#ifndef TB_BALL_LAUNCH_PREREQUISITES_H
#define TB_BALL_LAUNCH_PREREQUISITES_H
#include "ball/ball_internal.h"

int installer_support_required(Context *c, const char *host);
int launch_command_file(const char *program);
cJSON *launch_prerequisites(Context *c, const cJSON *request);
cJSON *ball_navigation_state(Context *c);
#endif
