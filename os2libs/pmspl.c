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
    return 2150; /* NERR_QNotFound: the virtual catalogue has no queues. */
}

ULONG SplEnumQueue(PSZ computer, ULONG level, PVOID buffer, ULONG capacity, PULONG returned,
                   PULONG total, PULONG needed, PVOID reserved)
{
    TRACE_NATIVE("SplEnumQueue(%p, %u, %p, %u, %p, %p, %p, %p)", computer, level, buffer, capacity,
                 returned, total, needed, reserved);
    if (!returned || !total || !needed || reserved || level > 6)
        return 87;
    *returned = *total = *needed = 0;
    return 0; /* The virtual queue catalogue contains no configured queues. */
}
