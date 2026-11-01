/* Public domain. No rights reserved. Wholly AI-authored. */
/* The virtual queue catalogue is empty; no host printing backend is configured. */
#ifndef TWOINE_OS2LIBS_PMSPL_H
#define TWOINE_OS2LIBS_PMSPL_H
#include "os2types.h"
OS2EXPORT ULONG OS2API SplQueryQueue(PSZ computer, PSZ queue, ULONG level, PVOID buffer,
                                     ULONG capacity, PULONG needed) OS2APIINFO(397);
OS2EXPORT ULONG OS2API SplEnumQueue(PSZ computer, ULONG level, PVOID buffer, ULONG capacity,
                                    PULONG returned, PULONG total, PULONG needed, PVOID reserved)
    OS2APIINFO(399);
#endif
