/* Public domain. No rights reserved. Wholly AI-authored. */
/* Internal ZIP operations; included after the ball context and manifest helpers. */
#ifndef TB_BALL_BALL_ARCHIVE_H
#define TB_BALL_BALL_ARCHIVE_H
#include "ball/ball_internal.h"

cJSON *export_ball(Context *c, const cJSON *request);
cJSON *import_ball(Context *c, const cJSON *request);
#endif
