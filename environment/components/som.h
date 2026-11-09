/* Public domain. No rights reserved. Wholly AI-authored. */
/* Original DSOM package companions and ball-owned service configuration. */
#ifndef TB_ENVIRONMENT_COMPONENTS_SOM_H
#define TB_ENVIRONMENT_COMPONENTS_SOM_H
#include "ball/store.h"

extern const char *os2_som_companions[];
int os2_som_companion(const char *path);
int os2_som_present(TbBall *c, const char *guest);
int os2_som_complete(TbBall *c);
int os2_som_provision(TbBall *c, int *changed);
#endif
