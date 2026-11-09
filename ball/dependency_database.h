/* Public domain. No rights reserved. Wholly AI-authored. */
/* Portable metadata only: no host paths, executable bytes, or media contents. */
#ifndef TB_BALL_DEPENDENCY_DATABASE_H
#define TB_BALL_DEPENDENCY_DATABASE_H
#include "ball/ball_internal.h"

void dependency_database_save(Context *c, const cJSON *report);
cJSON *dependency_database_load(Context *c);
#endif
