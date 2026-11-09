/* Public domain. No rights reserved. Wholly AI-authored. */
/* The host environment for the loader of a launch. */
#ifndef TB_EMULATOR_LAUNCH_HOST_ENVIRONMENT_H
#define TB_EMULATOR_LAUNCH_HOST_ENVIRONMENT_H
#include "ball/store.h"

extern const char *launch_unset_environment_keys[];
cJSON *launch_host_environment(TbBall *c, const char *build, const char *override,
                               const cJSON *app_env);
#endif
