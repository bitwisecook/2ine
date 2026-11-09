/* Public domain. No rights reserved. Wholly AI-authored. */
/* Normal launches read the stored graph. No executable parsing, directory
   enumeration, media inspection, recovery, or database writes happen here. */
#ifndef TB_BALL_LAUNCH_DEPENDENCIES_H
#define TB_BALL_LAUNCH_DEPENDENCIES_H
#include "ball/ball_internal.h"

cJSON *launch_dependencies(Context *parent, const cJSON *request);
#endif
