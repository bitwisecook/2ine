/* Public domain. No rights reserved. Wholly AI-authored. */
/* Portable metadata only: no host paths, executable bytes, or media contents. */
#ifndef TB_ENVIRONMENT_DEPENDENCIES_DATABASE_H
#define TB_ENVIRONMENT_DEPENDENCIES_DATABASE_H
#include "ball/store.h"

void dependency_database_save(TbBall *c, const cJSON *report);
cJSON *dependency_database_load(TbBall *c);
#endif
