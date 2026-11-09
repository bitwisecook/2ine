/* Public domain. No rights reserved. Wholly AI-authored. */
/* Project persistent WPProgram registrations into independently managed launchers. */
#ifndef TB_BALL_WPS_APPLICATIONS_H
#define TB_BALL_WPS_APPLICATIONS_H
#include "ball/ball_internal.h"

void wps_hide(Context *c, const cJSON *app);
void wps_detach(Context *c, cJSON *app);
int wps_sync(Context *c);
cJSON *wps_latest(Context *c);
#endif
