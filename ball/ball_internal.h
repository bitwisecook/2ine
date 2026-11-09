/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private to the ball store's units: running the hooks of the layered components. */
#ifndef TB_BALL_INTERNAL_H
#define TB_BALL_INTERNAL_H
#include "ball/store.h"

int tb_ball_regenerate(TbBall *c);
int tb_ball_validate_settings(TbBall *c, const cJSON *request);
cJSON *tb_ball_client_snapshot(TbBall *c);
void tb_ball_detach_application(TbBall *c, cJSON *application);
void tb_ball_hide_application(TbBall *c, const cJSON *application);
#endif
