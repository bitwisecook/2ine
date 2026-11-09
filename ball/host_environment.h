/* Public domain. No rights reserved. Wholly AI-authored. */
/* The host environment for the loader of a launch. */
#ifndef TB_BALL_HOST_ENVIRONMENT_H
#define TB_BALL_HOST_ENVIRONMENT_H
#include "ball/ball_internal.h"

extern const char *launch_unset_environment_keys[];
cJSON *launch_host_environment(Context *c, const char *build, const char *override,
                               const cJSON *app_env);
#endif
