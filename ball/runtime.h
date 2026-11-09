/* Public domain. No rights reserved. Wholly AI-authored. */
/* Runtime build identity and the per-ball runtime snapshot. */
#ifndef TB_BALL_RUNTIME_H
#define TB_BALL_RUNTIME_H
#include "ball/store.h"

const char *runtime_loader_name(TbBall *c, const char *build);
char **runtime_names(const char *build, size_t *count);
char *runtime_id(TbBall *c, const char *build);
int runtime_sync(TbBall *c, const char *build, const char *directory);
#endif
