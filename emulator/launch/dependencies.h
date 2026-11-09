/* Public domain. No rights reserved. Wholly AI-authored. */
/* Normal launches read the stored graph. No executable parsing, directory
   enumeration, media inspection, recovery, or database writes happen here. */
#ifndef TB_EMULATOR_LAUNCH_DEPENDENCIES_H
#define TB_EMULATOR_LAUNCH_DEPENDENCIES_H
#include "ball/store.h"

cJSON *launch_dependencies(TbBall *parent, const cJSON *request);
#endif
