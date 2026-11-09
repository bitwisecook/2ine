/* Public domain. No rights reserved. Wholly AI-authored. */
/* Module services supplied by 2ine. Optional original SOM and HELPMGR are not
   substitutes for empty/fallback native modules and remain media dependencies. */
#ifndef TB_BALL_SYSTEM_MODULES_H
#define TB_BALL_SYSTEM_MODULES_H
#include "ball/ball_internal.h"

int os2_module_matches(const char *name, const char *module);
int os2_module_disabled(Context *c, const char *name);
int os2_module_provided(Context *c, const char *name);
#endif
