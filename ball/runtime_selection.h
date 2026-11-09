/* Public domain. No rights reserved. Wholly AI-authored. */
/* A saved backend is a portable preference; explicit launch overrides are strict. */
#ifndef TB_BALL_RUNTIME_SELECTION_H
#define TB_BALL_RUNTIME_SELECTION_H
#include "ball/store.h"

const char *runtime_effective_backend(TbBall *c, const char *build, const char *requested,
                                      int saved);
#endif
