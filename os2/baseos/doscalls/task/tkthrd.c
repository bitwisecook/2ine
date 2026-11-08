/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

void destroyThreadResource(void *value)
{
    Thread *thread = value;
    lx_execution_destroy_thread32(GProcessState.execution, thread->invocation);
    if (thread->stack)
    {
        lx_execution_stack_segments(GProcessState.execution,
                                    lx_execution_mapping_address(thread->stack),
                                    lx_execution_mapping_size(thread->stack), 1);
        lx_execution_unmap(GProcessState.execution, thread->stack);
    }
    free(thread);
}

void os2ThreadCleanup(void *arg)
{
    Thread *thread = arg;
    releaseThreadCriticalSection();
    abandonThreadMutexes(thread->id);
    lx_execution_set_park_flag(GProcessState.execution, NULL);
    removeThreadControl(thread->control);
    thread->control = NULL;
    lx_execution_cleanup_thread32(GProcessState.execution);
    GProcessState.deinitOs2Tib(thread->selector);
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
    GProcessState.initOs2Tib(tibspace, (uint8 *)lx_execution_mapping_host(thread->stack) + size,
                             size, thread->id);
    thread->selector = GProcessState.setOs2Tib(tibspace);
    bindThreadControl(thread->control);
    lx_execution_checkpoint(GProcessState.execution);
    lx_execution_thread32(GProcessState.execution, thread->invocation);
    return NULL;
}

APIRET DosCreateThread(PTID ptid, PFNTHREAD entry, ULONG argument, ULONG flags, ULONG bytes)
{
    TRACE_NATIVE("DosCreateThread(%p, %p, %u, %u, %u)", ptid, entry, (uint)argument, (uint)flags,
                 (uint)bytes);
    if (!ptid || !entry || (flags & ~3u) || bytes > 0x1000000)
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
        thread->stack = lx_execution_map(GProcessState.execution, base, size, 3, 1);
        if (thread->stack)
            break;
    }
    if (!thread->stack)
    {
        ungrabLock(&GMutexDosCalls);
        free(thread);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    lx_execution_stack_segments(GProcessState.execution,
                                lx_execution_mapping_address(thread->stack), size, 0);
    thread->invocation = lx_execution_prepare_thread32(
        GProcessState.execution, (uint32)(uintptr_t)entry,
        lx_execution_mapping_address(thread->stack) + size - 16, argument);
    thread->id = GNextThreadId++;
    thread->control = createThreadControl(thread->id, (flags & CREATE_SUSPENDED) != 0);
    if (thread->invocation && thread->control)
        thread->handle = lx_handle_create(LX_HANDLE_THREAD, thread, destroyThreadResource);
    if (!thread->handle)
    {
        ungrabLock(&GMutexDosCalls);
        removeThreadControl(thread->control);
        destroyThreadResource(thread);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    thread->thread = lx_host_thread_create(os2ThreadEntry, thread, os2ThreadCleanup);
    if (!thread->thread)
    {
        ungrabLock(&GMutexDosCalls);
        removeThreadControl(thread->control);
        lx_handle_close(thread->handle, LX_HANDLE_THREAD);
        return ERROR_MAX_THRDS_REACHED;
    }
    thread->next = GThreads;
    GThreads = thread;
    *ptid = thread->id;
    /* Process exit must not unload code beneath any guest thread. */
    __atomic_store_n(&GProcessState.ne_threads_started, 1, __ATOMIC_RELEASE);
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

APIRET DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt)
{
    TRACE_NATIVE("DosQueryThreadContext(%u, %u, %p)", (uint)tid, (uint)level, pcxt);
    FIXME("Need to be able to suspend threads first");
    return ERROR_INVALID_PARAMETER;
}

void neThreadCleanup(void *argument)
{
    NeThreadStart *start = argument;
    releaseThreadCriticalSection();
    abandonThreadMutexes(start->tid);
    lx_execution_set_park_flag(GProcessState.execution, NULL);
    removeThreadControl(start->control);
    GProcessState.deinitOs2Tib(start->tib_selector);
    lx_execution_destroy_thread16(GProcessState.execution, start->invocation);
    free(start);
}

void *neThreadEntry(void *argument)
{
    NeThreadStart *start = argument;
    uint8 tibspace[LXTIBSIZE] = {0};
    GProcessState.initOs2Tib(tibspace, GProcessState.convert1616to32(start->stack), 65536,
                             start->tid);
    start->tib_selector = GProcessState.setOs2Tib(tibspace);
    bindThreadControl(start->control);
    lx_execution_checkpoint(GProcessState.execution);
    lx_execution_thread16(GProcessState.execution, start->invocation);
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
    start->entry = GProcessState.convert32to1616(pfnFun);
    start->stack = GProcessState.convert32to1616(pbStack);
    if (start->entry && start->stack)
        start->invocation =
            lx_execution_prepare_thread16(GProcessState.execution, start->entry, start->stack);
    if (start->invocation)
        start->control = createThreadControl(tid, FALSE);
    if (!start->control)
    {
        lx_execution_destroy_thread16(GProcessState.execution, start->invocation);
        free(start);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    LxHostThread *thread;
    __atomic_store_n(&GProcessState.ne_threads_started, 1, __ATOMIC_RELEASE);
    thread = lx_host_thread_create(neThreadEntry, start, neThreadCleanup);
    if (!thread)
    {
        removeThreadControl(start->control);
        lx_execution_destroy_thread16(GProcessState.execution, start->invocation);
        free(start);
        return ERROR_MAX_THRDS_REACHED;
    }
    lx_host_thread_detach(thread);
    *(uint16 *)pTid = (uint16)tid;
    return NO_ERROR;
}
