/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Critical sections and thread priorities. */
#include "os2/baseos/doscalls/internal.h"

/* tib2_ulpri holds the priority class in its high byte and the level in its low byte. */
enum
{
    PRIORITY_CLASS_SHIFT = 8,
    PRIORITY_LEVEL_MASK = 0xFF
};

void initCritSec(void)
{
    GCritSec = lx_host_mutex_create(1, 0);
    if (!GCritSec)
        abort();
}

APIRET16 Dos16EnterCritSec(VOID)
{
    TRACE_NATIVE("Dos16EnterCritSec()");
    return (APIRET16)DosEnterCritSec();
}

APIRET DosEnterCritSec(VOID)
{
    TRACE_NATIVE("DosEnterCritSec()");
    LxHostThreadState *thread = lxHostThreadState();
    if (thread->critical_depth == UINT32_MAX)
        return ERROR_CRITSEC_OVERFLOW;
    lx_host_once(&GCritSecOnce, initCritSec);
    LxHostLockResult result = lx_host_mutex_lock(GCritSec, UINT32_MAX);
    if (result != LX_HOST_LOCK_OK && result != LX_HOST_LOCK_OWNER_DIED)
        return ERROR_CRITSEC_OVERFLOW;
    thread->critical_depth++;
    return NO_ERROR;
}

APIRET16 Dos16ExitCritSec(VOID)
{
    TRACE_NATIVE("Dos16ExitCritSec()");
    return (APIRET16)DosExitCritSec();
}

APIRET DosExitCritSec(VOID)
{
    TRACE_NATIVE("DosExitCritSec()");
    LxHostThreadState *thread = lxHostThreadState();
    if (!thread->critical_depth || lx_host_mutex_unlock(GCritSec) != LX_HOST_LOCK_OK)
        return ERROR_CRITSEC_UNDERFLOW;
    thread->critical_depth--;
    return NO_ERROR;
}

void releaseThreadCriticalSection(void)
{
    LxHostThreadState *thread = lxHostThreadState();
    while (thread->critical_depth)
    {
        lx_host_mutex_unlock(GCritSec);
        thread->critical_depth--;
    }
}

APIRET16 Dos16SetPrty(USHORT scope, USHORT prtyclass, SHORT delta, USHORT pid)
{
    TRACE_NATIVE("Dos16SetPrty(%u, %u, %d, %u)", (uint)scope, (uint)prtyclass, (int)delta,
                 (uint)pid);
    return NO_ERROR;
}

APIRET DosSetPriority(ULONG scope, ULONG priority_class, LONG delta, ULONG target)
{
    TRACE_NATIVE("DosSetPriority(%u, %u, %d, %u)", scope, priority_class, (int)delta, target);
    if (scope > PRTYS_THREAD)
        return ERROR_INVALID_SCOPE;
    if (priority_class > PRTYC_FOREGROUNDSERVER)
        return ERROR_INVALID_PCLASS;
    if (delta < PRTYD_MINIMUM || delta > PRTYD_MAXIMUM)
        return ERROR_INVALID_PDELTA;
    PTIB2 tib = (PTIB2)LX_GETTIB2();
    if (scope != PRTYS_THREAD || (target && target != tib->tib2_ultid))
        return ERROR_INVALID_PROCID;
    ULONG previous = tib->tib2_ulpri;
    /* A new class starts its level at the delta; PRTYC_NOCHANGE adjusts the current level. */
    LONG level =
        priority_class != PRTYC_NOCHANGE ? delta : (LONG)(previous & PRIORITY_LEVEL_MASK) + delta;
    if (level < 0)
        level = 0;
    if (level > PRTYD_MAXIMUM)
        level = PRTYD_MAXIMUM;
    /* Guest scheduling priority is distinct from host process privileges. */
    tib->tib2_ulpri =
        ((priority_class != PRTYC_NOCHANGE ? priority_class : previous >> PRIORITY_CLASS_SHIFT)
         << PRIORITY_CLASS_SHIFT) |
        (ULONG)level;
    return NO_ERROR;
}
