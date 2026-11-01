/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmspl.h"
#include "os2libs/generated/pmspl-lx.h"
ULONG SplQueryQueue(PSZ computer, PSZ queue, ULONG level, PVOID buffer, ULONG capacity,
                    PULONG needed)
{
    (void)computer;
    (void)queue;
    (void)level;
    (void)buffer;
    (void)capacity;
    if (!needed)
        return 87;
    *needed = 0;
    return 0x4009; /* PMERR_SPL_SPOOLER_NOT_INSTALLED */
}
