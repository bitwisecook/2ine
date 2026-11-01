/* Public domain. No rights reserved. Wholly AI-authored. */
/* Queue queries report the absence of a host spooler. */
#ifndef TWOINE_OS2LIBS_PMSPL_H
#define TWOINE_OS2LIBS_PMSPL_H
#include "os2types.h"
OS2EXPORT ULONG OS2API SplQueryQueue(PSZ computer, PSZ queue, ULONG level, PVOID buffer,
                                     ULONG capacity, PULONG needed) OS2APIINFO(397);
#endif
