/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host-library clients may release a DOS worker's join ownership. */
#ifndef TWOINE_DOSCALLS_HOST_H
#define TWOINE_DOSCALLS_HOST_H
#include "os2libs/os2types.h"
OS2EXPORT APIRET OS2API lx_doscalls_detach_thread(TID id);
#endif
