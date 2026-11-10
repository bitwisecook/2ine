/* Public domain. No rights reserved. Wholly AI-authored. */
/* The PDF and PCL file queues use native host save dialogs. */
#ifndef _INCL_PMSPL_H_
#define _INCL_PMSPL_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_SPL: spooler queries */

    enum
    {
        PRQ3_TYPE_RAW = 0x0001,
        PRQ3_TYPE_BYPASS = 0x0002,
        PRQ3_TYPE_APPDEFAULT = 0x0004
    };

    OS2EXPORT ULONG OS2API SplQueryDevice(PSZ computer, PSZ device, ULONG level, PVOID buffer,
                                          ULONG capacity, PULONG needed);

    OS2EXPORT ULONG OS2API SplQueryQueue(PSZ computer, PSZ queue, ULONG level, PVOID buffer,
                                         ULONG capacity, PULONG needed);

    OS2EXPORT ULONG OS2API SplEnumQueue(PSZ computer, ULONG level, PVOID buffer, ULONG capacity,
                                        PULONG returned, PULONG total, PULONG needed,
                                        PVOID reserved);

    OS2EXPORT ULONG OS2API SplEnumPrinter(PSZ computer, ULONG level, ULONG types, PVOID buffer,
                                          ULONG capacity, PULONG returned, PULONG total,
                                          PULONG needed, PVOID reserved);

#ifdef __cplusplus
}
#endif

#endif
