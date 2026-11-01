/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
#include "os2libs/doscalls/host.h"
APIRET DosEnterMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosEnterMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2)LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0xFFFF)
    {
        return ERROR_NESTING_TOO_DEEP;
    }
    else if (tib2->tib2_usMCCount == 0)
    {
        FIXME("block signals here");
    }

    tib2->tib2_usMCCount++;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
}

APIRET DosExitMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosExitMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2)LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0)
    {
        return ERROR_ALREADY_RESET;
    }
    else if (tib2->tib2_usMCCount == 1)
    {
        FIXME("unblock signals here");
    }

    tib2->tib2_usMCCount--;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
}

void destroyThreadResource(void *value)
{
    Thread *thread = value;
    lx_execution_destroy_thread32(GLoaderState.execution, thread->invocation);
    if (thread->stack)
    {
        lx_execution_stack_segments(GLoaderState.execution,
                                    lx_execution_mapping_address(thread->stack),
                                    lx_execution_mapping_size(thread->stack), 1);
        lx_execution_unmap(GLoaderState.execution, thread->stack);
    }
    free(thread);
}

void os2ThreadCleanup(void *arg)
{
    Thread *thread = arg;
    lx_execution_cleanup_thread32(GLoaderState.execution);
    GLoaderState.deinitOs2Tib(thread->selector);
    grabLock(&GMutexDosCalls);
    thread->finished = 1;
    int detached = thread->detached;
    if (detached)
        for (Thread **link = &GThreads; *link; link = &(*link)->next)
            if (*link == thread)
            {
                *link = thread->next;
                break;
            }
    ungrabLock(&GMutexDosCalls);
    if (detached)
    {
        lx_host_thread_detach(thread->thread);
        lx_handle_close(thread->handle, LX_HANDLE_THREAD);
    }
}

APIRET lx_doscalls_detach_thread(TID id)
{
    grabLock(&GMutexDosCalls);
    Thread **link = &GThreads;
    while (*link && (*link)->id != id)
        link = &(*link)->next;
    Thread *thread = *link;
    if (!thread || thread->joining || thread->detached)
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_INVALID_THREADID;
    }
    if (!thread->finished)
    {
        thread->detached = 1;
        ungrabLock(&GMutexDosCalls);
        return NO_ERROR;
    }
    thread->joining = 1;
    ungrabLock(&GMutexDosCalls);
    int result = lx_host_thread_join(thread->thread, NULL);
    grabLock(&GMutexDosCalls);
    if (!result)
    {
        link = &GThreads;
        while (*link && *link != thread)
            link = &(*link)->next;
        if (*link)
            *link = thread->next;
    }
    else
        thread->joining = 0;
    ungrabLock(&GMutexDosCalls);
    if (!result)
        lx_handle_close(thread->handle, LX_HANDLE_THREAD);
    return result ? ERROR_INVALID_THREADID : NO_ERROR;
}

void *os2ThreadEntry(void *arg)
{
    Thread *thread = arg;
    uint8 tibspace[LXTIBSIZE] = {0};
    const size_t size = lx_execution_mapping_size(thread->stack);
    GLoaderState.initOs2Tib(tibspace, (uint8 *)lx_execution_mapping_host(thread->stack) + size,
                            size, thread->id);
    thread->selector = GLoaderState.setOs2Tib(tibspace);
    lx_execution_thread32(GLoaderState.execution, thread->invocation);
    return NULL;
}

APIRET DosCreateThread(PTID ptid, PFNTHREAD entry, ULONG argument, ULONG flags, ULONG bytes)
{
    TRACE_NATIVE("DosCreateThread(%p, %p, %u, %u, %u)", ptid, entry, (uint)argument, (uint)flags,
                 (uint)bytes);
    if (!ptid || !entry || (flags & CREATE_SUSPENDED) || bytes > 0x1000000)
        return ERROR_INVALID_PARAMETER;
    const uint32 size = bytes ? (bytes + 0xffff) & ~0xffffu : 0x10000;
    Thread *thread = calloc(1, sizeof *thread);
    if (!thread)
        return ERROR_NOT_ENOUGH_MEMORY;
    grabLock(&GMutexDosCalls);
    if (GNextThreadId > UINT16_MAX)
    {
        ungrabLock(&GMutexDosCalls);
        free(thread);
        return ERROR_MAX_THRDS_REACHED;
    }
    while (GNextThreadStack <= 0x1fff0000u - size)
    {
        uint32 base = GNextThreadStack;
        GNextThreadStack += size + 0x10000;
        thread->stack = lx_execution_map(GLoaderState.execution, base, size, 3, 1);
        if (thread->stack)
            break;
    }
    if (!thread->stack)
    {
        ungrabLock(&GMutexDosCalls);
        free(thread);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    lx_execution_stack_segments(GLoaderState.execution, lx_execution_mapping_address(thread->stack),
                                size, 0);
    thread->invocation = lx_execution_prepare_thread32(
        GLoaderState.execution, (uint32)(uintptr_t)entry,
        lx_execution_mapping_address(thread->stack) + size - 16, argument);
    thread->id = GNextThreadId++;
    if (thread->invocation)
        thread->handle = lx_handle_create(LX_HANDLE_THREAD, thread, destroyThreadResource);
    if (!thread->handle)
    {
        ungrabLock(&GMutexDosCalls);
        destroyThreadResource(thread);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    thread->thread = lx_host_thread_create(os2ThreadEntry, thread, os2ThreadCleanup);
    if (!thread->thread)
    {
        ungrabLock(&GMutexDosCalls);
        lx_handle_close(thread->handle, LX_HANDLE_THREAD);
        return ERROR_MAX_THRDS_REACHED;
    }
    thread->next = GThreads;
    GThreads = thread;
    *ptid = thread->id;
    /* Process exit must not unload code beneath any guest thread. */
    __atomic_store_n(&GLoaderState.ne_threads_started, 1, __ATOMIC_RELEASE);
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

static uint32 waitKilledThread(void *argument)
{
    LxHandleBorrow *borrow = argument;
    Thread *thread = borrow->resource;
    lx_host_thread_test_cancel();
    for (;;)
    {
        grabLock(&GMutexDosCalls);
        BOOL finished = thread->finished;
        ungrabLock(&GMutexDosCalls);
        if (finished)
            return NO_ERROR;
        lx_host_sleep_ms(1);
    }
}

APIRET DosKillThread(TID id)
{
    TRACE_NATIVE("DosKillThread(%u)", id);
    grabLock(&GMutexDosCalls);
    Thread *thread;
    for (thread = GThreads; thread && thread->id != id; thread = thread->next)
    {
    }
    LxHandleBorrow borrow = {0};
    if (!thread || thread->finished || thread->joining ||
        !lx_handle_borrow(thread->handle, LX_HANDLE_THREAD, &borrow))
    {
        ungrabLock(&GMutexDosCalls);
        return ERROR_INVALID_THREADID;
    }
    LxHostCancelResult cancelled = lx_host_thread_cancel(thread->thread);
    ungrabLock(&GMutexDosCalls);
    APIRET result = cancelled == LX_HOST_CANCEL_UNSUPPORTED ? ERROR_NOT_SUPPORTED
                    : cancelled == LX_HOST_CANCEL_OK        ? NO_ERROR
                                                            : ERROR_INVALID_THREADID;
    if (!result)
        return lx_host_guarded_call(waitKilledThread, &borrow, releaseResourceBorrow, &borrow);
    lx_handle_release(&borrow);
    return result;
}

APIRET DosWaitThread(PTID id, ULONG option)
{
    TRACE_NATIVE("DosWaitThread(%p, %u)", id, (uint)option);
    if (!id || (option != DCWW_WAIT && option != DCWW_NOWAIT))
        return ERROR_INVALID_PARAMETER;
    for (;;)
    {
        grabLock(&GMutexDosCalls);
        Thread *thread;
        for (thread = GThreads; thread; thread = thread->next)
            if (*id ? thread->id == *id : thread->finished && !thread->joining)
                break;
        if (!thread && (*id || !GThreads))
        {
            ungrabLock(&GMutexDosCalls);
            return ERROR_INVALID_THREADID;
        }
        if (!thread || (option == DCWW_NOWAIT && !thread->finished))
        {
            ungrabLock(&GMutexDosCalls);
            if (option == DCWW_NOWAIT)
                return ERROR_THREAD_NOT_TERMINATED;
            lx_host_sleep_ms(1);
            continue;
        }
        if (thread->joining)
        {
            ungrabLock(&GMutexDosCalls);
            return ERROR_INVALID_THREADID;
        }
        LxHandleBorrow object;
        if (!lx_handle_borrow(thread->handle, LX_HANDLE_THREAD, &object))
        {
            ungrabLock(&GMutexDosCalls);
            return ERROR_INVALID_THREADID;
        }
        thread->joining = 1;
        ungrabLock(&GMutexDosCalls);
        int joined = lx_host_thread_join(thread->thread, NULL);
        grabLock(&GMutexDosCalls);
        if (!joined)
        {
            for (Thread **link = &GThreads; *link; link = &(*link)->next)
                if (*link == thread)
                {
                    *link = thread->next;
                    break;
                }
            *id = thread->id;
            lx_handle_close(thread->handle, LX_HANDLE_THREAD);
        }
        else
            thread->joining = 0;
        ungrabLock(&GMutexDosCalls);
        lx_handle_release(&object);
        return joined ? ERROR_INVALID_THREADID : NO_ERROR;
    }
}

APIRET DosAllocThreadLocalMemory(ULONG cb, PULONG *p)
{
    TRACE_NATIVE("DosAllocThreadLocalMemory(%u, %p)", (uint)cb, p);

    if (!cb || cb > 8) // OS/2 allocates one to eight contiguous words.
        return ERROR_INVALID_PARAMETER;
    else if (!p)
        return ERROR_INVALID_PARAMETER;

    APIRET retval = NO_ERROR;
    uint32 mask = 0xFF >> (8 - cb);

    grabLock(&GMutexDosCalls);

    if (GLoaderState.tlspage == NULL)
    {
        if ((GLoaderState.tlspage =
                 (uint32 *)(uintptr_t)lx_execution_tls_window(GLoaderState.execution)) == NULL)
        {
            ungrabLock(&GMutexDosCalls);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    int i;
    for (i = 0; i <= 32 - (int)cb; i++, mask <<= 1)
    {
        if (((GLoaderState.tlsmask ^ mask) & mask) == mask)
            break;
    }

    if (i > 32 - (int)cb)
        retval =
            ERROR_NOT_ENOUGH_MEMORY; // there are only 32 slots, couldn't find anything contiguous.
    else
    {
        assert(GLoaderState.tlsallocs[i] == 0);
        GLoaderState.tlsallocs[i] = (uint8)cb;
        GLoaderState.tlsmask |= mask;
        *p = GLoaderState.tlspage + i;
    }

    ungrabLock(&GMutexDosCalls);

    return retval;
}

APIRET DosFreeThreadLocalMemory(ULONG *p)
{
    TRACE_NATIVE("DosFreeThreadLocalMemory(%p)", p);

    APIRET retval = ERROR_INVALID_PARAMETER;

    grabLock(&GMutexDosCalls);

    if (GLoaderState.tlspage)
    {
        const uintptr_t distance = (uintptr_t)p - (uintptr_t)GLoaderState.tlspage;
        const uint32 slot = distance < 128 && !(distance & 3) ? (uint32)distance / 4 : 32;
        if (slot < 32)
        {
            const uint8 slots = GLoaderState.tlsallocs[slot];
            if (slots > 0)
            {
                const uint32 mask = ((uint32)(0xFF >> (8 - slots))) << slot;
                assert((GLoaderState.tlsmask & mask) == mask);
                GLoaderState.tlsmask &= ~mask;
                GLoaderState.tlsallocs[slot] = 0;
                retval = NO_ERROR;
            }
        }
    }

    ungrabLock(&GMutexDosCalls);

    return retval;
}

APIRET DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt)
{
    TRACE_NATIVE("DosQueryThreadContext(%u, %u, %p)", (uint)tid, (uint)level, pcxt);
    FIXME("Need to be able to suspend threads first");
    return ERROR_INVALID_PARAMETER;
}

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
    lx_host_once(&GCritSecOnce, initCritSec);
    lx_host_mutex_lock(GCritSec, UINT32_MAX);
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
    lx_host_once(&GCritSecOnce, initCritSec);
    lx_host_mutex_unlock(GCritSec);
    return NO_ERROR;
}

void neThreadCleanup(void *argument)
{
    NeThreadStart *start = argument;
    GLoaderState.deinitOs2Tib(start->tib_selector);
    lx_execution_destroy_thread16(GLoaderState.execution, start->invocation);
    free(start);
}

void *neThreadEntry(void *argument)
{
    NeThreadStart *start = argument;
    uint8 tibspace[LXTIBSIZE] = {0};
    GLoaderState.initOs2Tib(tibspace, GLoaderState.convert1616to32(start->stack), 65536,
                            start->tid);
    start->tib_selector = GLoaderState.setOs2Tib(tibspace);
    lx_execution_thread16(GLoaderState.execution, start->invocation);
    return NULL;
}

APIRET16 Dos16CreateThread(PVOID pfnFun, PVOID pTid, PVOID pbStack)
{
    TRACE_NATIVE("Dos16CreateThread(%p, %p, %p)", pfnFun, pTid, pbStack);
    if (!pfnFun || !pTid || !pbStack)
        return ERROR_INVALID_PARAMETER;
    NeThreadStart *start = calloc(1, sizeof(*start));
    if (!start)
        return ERROR_NOT_ENOUGH_MEMORY;
    grabLock(&GMutexDosCalls);
    const uint32 tid = GNextThreadId++;
    ungrabLock(&GMutexDosCalls);
    if (tid > 0xffff)
    {
        free(start);
        return ERROR_MAX_THRDS_REACHED;
    }
    start->tid = (uint16)tid;
    start->entry = GLoaderState.convert32to1616(pfnFun);
    start->stack = GLoaderState.convert32to1616(pbStack);
    if (start->entry && start->stack)
        start->invocation =
            lx_execution_prepare_thread16(GLoaderState.execution, start->entry, start->stack);
    if (!start->invocation)
    {
        free(start);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    LxHostThread *thread;
    __atomic_store_n(&GLoaderState.ne_threads_started, 1, __ATOMIC_RELEASE);
    thread = lx_host_thread_create(neThreadEntry, start, neThreadCleanup);
    if (!thread)
    {
        lx_execution_destroy_thread16(GLoaderState.execution, start->invocation);
        free(start);
        return ERROR_MAX_THRDS_REACHED;
    }
    lx_host_thread_detach(thread);
    *(uint16 *)pTid = (uint16)tid;
    return NO_ERROR;
}

APIRET DosSleep(ULONG msec)
{
    TRACE_NATIVE("DosSleep(%u)", (uint)msec);

    do
    {
        lx_execution_poll_termination(GLoaderState.execution);
        ULONG slice = msec > 10 ? 10 : msec;
        lx_host_sleep_ms(slice);
        msec -= slice;
    } while (msec);
    lx_execution_poll_termination(GLoaderState.execution);
    return NO_ERROR;
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
