/* Public domain. No rights reserved. Wholly AI-authored. */
/* Opening, creating and saving a ball's manifest. */
#ifndef TB_BALL_MANIFEST_H
#define TB_BALL_MANIFEST_H
#include "ball/ball_internal.h"

int tb_ball_save_manifest(Context *c);
int tb_ball_load(Context *c, const cJSON *request);
int tb_ball_create(Context *c, const char *path);
#endif
