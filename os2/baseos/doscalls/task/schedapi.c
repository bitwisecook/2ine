/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

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
    if (scope > 2)
        return ERROR_INVALID_SCOPE;
    if (priority_class > 4)
        return ERROR_INVALID_PCLASS;
    if (delta < -31 || delta > 31)
        return ERROR_INVALID_PDELTA;
    PTIB2 tib = (PTIB2)LX_GETTIB2();
    if (scope != 2 || (target && target != tib->tib2_ultid))
        return ERROR_INVALID_PROCID;
    ULONG previous = tib->tib2_ulpri;
    LONG level = priority_class ? delta : (LONG)(previous & 0xff) + delta;
    if (level < 0)
        level = 0;
    if (level > 31)
        level = 31;
    /* Guest scheduling priority is distinct from host process privileges. */
    tib->tib2_ulpri = ((priority_class ? priority_class : previous >> 8) << 8) | (ULONG)level;
    return NO_ERROR;
}
