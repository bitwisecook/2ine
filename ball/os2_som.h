/* Public domain. No rights reserved. Wholly AI-authored. */
/* Original DSOM package companions and ball-owned service configuration. */
#ifndef TB_BALL_OS2_SOM_H
#define TB_BALL_OS2_SOM_H
#include "ball/ball_internal.h"

extern const char *os2_som_companions[];
int os2_som_companion(const char *path);
int os2_som_present(Context *c, const char *guest);
int os2_som_complete(Context *c);
int os2_som_provision(Context *c, int *changed);
#endif
