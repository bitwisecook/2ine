/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launcher icons and the desktop identity of a launch. */
#ifndef TB_BALL_APPLICATION_ICON_H
#define TB_BALL_APPLICATION_ICON_H
#include "ball/ball_internal.h"

cJSON *launch_application_icon(Context *c, const cJSON *request);
cJSON *launch_desktop_identity(Context *parent, const char *program, const char *name);
#endif
