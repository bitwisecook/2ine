/* Public domain. No rights reserved. Wholly AI-authored. */
/* Project persistent WPProgram registrations into independently managed launchers. */
#ifndef TB_ENVIRONMENT_WPS_SYNC_H
#define TB_ENVIRONMENT_WPS_SYNC_H
#include "ball/store.h"

void wps_hide(TbBall *c, const cJSON *app);
void wps_detach(TbBall *c, cJSON *app);
int wps_sync(TbBall *c);
cJSON *wps_latest(TbBall *c);
#endif
