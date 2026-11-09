/* Public domain. No rights reserved. Wholly AI-authored. */
/* Add installed component directories to OS/2's configuration search lists. */
#ifndef TB_BALL_OS2_PATHS_H
#define TB_BALL_OS2_PATHS_H
#include "ball/store.h"

int os2_configure_paths(TbBall *c, const cJSON *files);
#endif
