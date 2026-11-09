/* Public domain. No rights reserved. Wholly AI-authored. */
/* Launcher icons and the desktop identity of a launch. */
#ifndef TB_EMULATOR_LAUNCH_DESKTOP_IDENTITY_H
#define TB_EMULATOR_LAUNCH_DESKTOP_IDENTITY_H
#include "ball/store.h"

cJSON *launch_application_icon(TbBall *c, const cJSON *request);
cJSON *launch_desktop_identity(TbBall *parent, const char *program, const char *name);
#endif
