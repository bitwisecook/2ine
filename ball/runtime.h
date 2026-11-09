/* Public domain. No rights reserved. Wholly AI-authored. */
/* Runtime build identity and the per-ball runtime snapshot. */
#ifndef TB_BALL_RUNTIME_H
#define TB_BALL_RUNTIME_H
#include "ball/ball_internal.h"

const char *runtime_loader_name(Context *c, const char *build);
char **runtime_names(const char *build, size_t *count);
char *runtime_id(Context *c, const char *build);
int runtime_sync(Context *c, const char *build, const char *directory);
#endif
