/* Public domain. No rights reserved. Wholly AI-authored. */
/* Browsing guest directories and copying host files into a drive. */
#ifndef TB_BALL_FILES_H
#define TB_BALL_FILES_H
#include "ball/ball_internal.h"

cJSON *tb_ball_browse(Context *c, const char *path);
int tb_ball_plan_copy(Context *c, cJSON *plan, const char *source, const char *destination,
                      unsigned depth);
cJSON *tb_ball_import_files(Context *c, const cJSON *request);
#endif
