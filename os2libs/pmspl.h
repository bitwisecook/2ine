/* Public domain. No rights reserved. Wholly AI-authored. */
/* The PDF and PCL file queues use native host save dialogs. */
#ifndef TWOINE_OS2LIBS_PMSPL_H
#define TWOINE_OS2LIBS_PMSPL_H
#include "os2types.h"
ULONG lx_spl_catalogue(const char *queue, ULONG level, PVOID output, ULONG capacity, uint32 base,
                       PULONG returned, PULONG total, PULONG needed);
ULONG lx_spl_device(const char *name, ULONG level, PVOID output, ULONG capacity, uint32 base,
                    PULONG needed);
OS2EXPORT ULONG OS2API SplQueryDevice(PSZ computer, PSZ device, ULONG level, PVOID buffer,
                                      ULONG capacity, PULONG needed) OS2APIINFO(381);
OS2EXPORT ULONG OS2API SplQueryQueue(PSZ computer, PSZ queue, ULONG level, PVOID buffer,
                                     ULONG capacity, PULONG needed) OS2APIINFO(397);
OS2EXPORT ULONG OS2API SplEnumQueue(PSZ computer, ULONG level, PVOID buffer, ULONG capacity,
                                    PULONG returned, PULONG total, PULONG needed, PVOID reserved)
    OS2APIINFO(399);
ULONG lx_spl_printers(ULONG level, ULONG types, PVOID output, ULONG capacity, uint32 base,
                       PULONG returned, PULONG total, PULONG needed);
OS2EXPORT ULONG OS2API SplEnumPrinter(PSZ computer, ULONG level, ULONG types, PVOID buffer,
                                      ULONG capacity, PULONG returned, PULONG total, PULONG needed,
                                      PVOID reserved) OS2APIINFO(441);
#endif
