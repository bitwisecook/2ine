/* Public domain. No rights reserved. Wholly AI-authored. */
/* A saved backend is a portable preference; explicit launch overrides are strict. */
#ifndef TB_EMULATOR_LAUNCH_RUNTIME_SELECTION_H
#define TB_EMULATOR_LAUNCH_RUNTIME_SELECTION_H
#include "ball/store.h"

const char *runtime_effective_backend(TbBall *c, const char *build, const char *requested,
                                      int saved);
/* The effective-runtime and effective-backend actions. */
cJSON *launch_effective_runtime(TbBall *c, const cJSON *request);
cJSON *launch_effective_backend(TbBall *c, const cJSON *request);
#endif
