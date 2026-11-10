/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 thread handles retain completion state independently of DOS worker stacks. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/kernel/processes.h"
#include "os2/include/bsedos.h"
#include "host/sync.h"
#include "host/thread.h"
#include "host/clock.h"

typedef struct Open32Thread
{
    LxHostMonitor *monitor;
    LxGuestMapping *arguments;
    LxHandleBorrow entry; /* The worker's own borrow, for a killer to retire. */
    ULONG public_handle, worker_handle, id, result, suspended, adopted_result;
    BOOL finished, started, adopted;
} Open32Thread;

#define CURRENT_THREAD_HANDLE (UINT32_MAX - 1)

enum
{
    /* The per-thread record the entry stub reads: procedure, parameter, worker handle. */
    THREAD_ARGUMENT_PROCEDURE = 0,
    THREAD_ARGUMENT_PARAMETER = 4,
    THREAD_ARGUMENT_HANDLE = 8,
    /* The entry page: the stub at its start, then the start and completion bridges. */
    START_BRIDGE_OFFSET = 0x100,
    COMPLETE_BRIDGE_OFFSET = 0x200,
    BRIDGE_CODE_SIZE = 0x100,
    BRIDGE_ARGUMENT_BYTES = sizeof(uint32),
    /* Where the stub's two "mov eax, imm32" instructions hold the bridge addresses. */
    STUB_START_TARGET = 9,
    STUB_COMPLETE_TARGET = 28,
    STACK_SIZE_LIMIT = 0x1000000,
    DEFAULT_STACK_SIZE = 0x100000,
    US_PER_MS = 1000
};

static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxGuestMapping *entry_code;
static LxHostOnce adoption_once = LX_HOST_ONCE_INIT;
static LxHostThreadLocalKey *adopted_key;
static _Thread_local LxHandleBorrow current_thread;

static void destroy_thread(void *resource)
{
    Open32Thread *thread = resource;
    if (thread->arguments)
        lx_execution_unmap(GProcessState.execution, thread->arguments);
    lx_host_monitor_destroy(thread->monitor);
    free(thread);
}

static uint32 start_thread(uint8 *a)
{
    if (current_thread.resource ||
        !lx_handle_borrow(lx_guest_load_u32(a), LX_HANDLE_OPEN32_THREAD, &current_thread))
        abort();
    Open32Thread *thread = current_thread.resource;
    lx_host_monitor_lock(thread->monitor);
    thread->entry = current_thread;
    while (thread->suspended)
        lx_host_monitor_wait(thread->monitor, UINT32_MAX);
    thread->started = TRUE;
    lx_host_monitor_unlock(thread->monitor);
    return 0;
}

static uint32 complete_thread(uint8 *a)
{
    Open32Thread *thread = current_thread.resource;
    if (!thread)
        abort();
    lx_host_monitor_lock(thread->monitor);
    thread->result = lx_guest_load_u32(a);
    thread->finished = TRUE;
    lx_host_monitor_wake_all(thread->monitor);
    lx_host_monitor_unlock(thread->monitor);
    /* An adopted thread's borrow ends with the host thread, in adopted_thread_ended. */
    if (!thread->adopted)
    {
        lx_handle_close(thread->worker_handle, LX_HANDLE_OPEN32_THREAD);
        lx_handle_release(&current_thread);
    }
    return 0;
}

static void initialize(void)
{
    entry_code = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                  LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!entry_code)
        return;
    uint8 *code = lx_execution_mapping_host(entry_code);
    uint32 base = lx_execution_mapping_address(entry_code);
    LxExecutionApi32 start = {start_thread, BRIDGE_ARGUMENT_BYTES, 0, LX_ABI_STACK32};
    LxExecutionApi32 complete = {complete_thread, BRIDGE_ARGUMENT_BYTES, 0, LX_ABI_STACK32};
    BOOL ready = lx_execution_emit_bridge32(GProcessState.execution, code + START_BRIDGE_OFFSET,
                                            BRIDGE_CODE_SIZE, &start) &&
                 lx_execution_emit_bridge32(GProcessState.execution, code + COMPLETE_BRIDGE_OFFSET,
                                            BRIDGE_CODE_SIZE, &complete);
    /* push esi; mov esi, [esp+8]; start(handle); eax = procedure(parameter);
       complete(eax); pop esi; ret. The DOS thread gets the argument record. */
    uint8 body[] = {0x56, 0x8b, 0x74, 0x24, 0x08, 0xff, 0x76, 0x08, 0xb8, 0,    0,    0,    0,
                    0xff, 0xd0, 0x83, 0xc4, 0x04, 0xff, 0x76, 0x04, 0xff, 0x16, 0x83, 0xc4, 0x04,
                    0x50, 0xb8, 0,    0,    0,    0,    0xff, 0xd0, 0x83, 0xc4, 0x04, 0x5e, 0xc3};
    lx_guest_store_u32(body + STUB_START_TARGET, base + START_BRIDGE_OFFSET);
    lx_guest_store_u32(body + STUB_COMPLETE_TARGET, base + COMPLETE_BRIDGE_OFFSET);
    memcpy(code, body, sizeof body);
    if (!ready || !lx_execution_protect(GProcessState.execution, entry_code,
                                        LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        lx_execution_unmap(GProcessState.execution, entry_code);
        entry_code = NULL;
    }
}

ULONG CreateThread(PVOID security, ULONG stack_bytes, ULONG procedure, ULONG parameter, ULONG flags,
                   PULONG id)
{
    TRACE_NATIVE("CreateThread(%p, %u, %X, %X, %X, %p)", security, stack_bytes, procedure,
                 parameter, flags, id);
    if (security || (flags & ~(unsigned)WIN32_CREATE_SUSPENDED) || stack_bytes > STACK_SIZE_LIMIT ||
        !lx_execution_memory(GProcessState.execution, procedure, 1, LX_GUEST_EXECUTE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    typedef APIRET(OS2API * CreateFn)(PTID, PFNTHREAD, ULONG, ULONG, ULONG);
    typedef APIRET(OS2API * DetachFn)(TID);
    CreateFn create = (CreateFn)open32DosService("DosCreateThread");
    DetachFn detach = (DetachFn)open32DosService("lx_doscalls_detach_thread");
    lx_host_once(&once, initialize);
    if (!entry_code || !create || !detach)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    Open32Thread *thread = calloc(1, sizeof *thread);
    if (thread)
        thread->monitor = lx_host_monitor_create();
    if (!thread || !thread->monitor)
    {
        free(thread);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    thread->arguments = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                         LX_GUEST_READ | LX_GUEST_WRITE, 0);
    thread->result = WIN32_STILL_ACTIVE;
    thread->suspended = (flags & WIN32_CREATE_SUSPENDED) != 0;
    if (thread->arguments)
        thread->public_handle = lx_handle_create(LX_HANDLE_OPEN32_THREAD, thread, destroy_thread);
    if (thread->public_handle)
        thread->worker_handle = lx_handle_create(LX_HANDLE_OPEN32_THREAD, thread, destroy_thread);
    if (!thread->worker_handle)
    {
        if (thread->public_handle)
            lx_handle_close(thread->public_handle, LX_HANDLE_OPEN32_THREAD);
        else
            destroy_thread(thread);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    uint8 *arguments = lx_execution_mapping_host(thread->arguments);
    lx_guest_store_u32(arguments + THREAD_ARGUMENT_PROCEDURE, procedure);
    lx_guest_store_u32(arguments + THREAD_ARGUMENT_PARAMETER, parameter);
    lx_guest_store_u32(arguments + THREAD_ARGUMENT_HANDLE, thread->worker_handle);
    ULONG error =
        create(&thread->id, (PFNTHREAD)(uintptr_t)lx_execution_mapping_address(entry_code),
               lx_execution_mapping_address(thread->arguments), 0,
               stack_bytes ? stack_bytes : DEFAULT_STACK_SIZE);
    if (error)
    {
        ULONG public_handle = thread->public_handle;
        lx_handle_close(thread->worker_handle, LX_HANDLE_OPEN32_THREAD);
        lx_handle_close(public_handle, LX_HANDLE_OPEN32_THREAD);
        open32SetError(error);
        return 0;
    }
    detach(thread->id);
    if (id)
        *id = thread->id;
    return thread->public_handle;
}

BOOL GetExitCodeThread(ULONG handle, PULONG result)
{
    if (result && open32ChildThreadExitCode(handle, result))
        return TRUE;
    LxHandleBorrow borrow = {0};
    if (!result || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &borrow))
    {
        open32SetError(result ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Open32Thread *thread = borrow.resource;
    lx_host_monitor_lock(thread->monitor);
    *result = thread->finished ? thread->result : WIN32_STILL_ACTIVE;
    lx_host_monitor_unlock(thread->monitor);
    lx_handle_release(&borrow);
    return TRUE;
}

static ULONG dos_thread_call(const char *name, ULONG id)
{
    typedef APIRET(OS2API * Fn)(ULONG);
    Fn call = (Fn)open32DosService(name);
    return call ? call(id) : ERROR_CALL_NOT_IMPLEMENTED;
}

ULONG ResumeThread(ULONG handle)
{
    ULONG child_previous;
    if (open32ChildThreadResume(handle, &child_previous))
        return child_previous;
    if (handle == CURRENT_THREAD_HANDLE)
        return 0; /* A caller that is running holds no suspension. */
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    Open32Thread *thread = borrow.resource;
    lx_host_monitor_lock(thread->monitor);
    ULONG previous = thread->suspended;
    /* Before its start gate a thread needs only the count. An adopted thread may
       have suspended itself through the pseudo-handle, which keeps no count. */
    if (thread->started && (previous == 1 || (thread->adopted && !previous)))
    {
        ULONG error = dos_thread_call("DosResumeThread", thread->id);
        if (error)
        {
            lx_host_monitor_unlock(thread->monitor);
            lx_handle_release(&borrow);
            open32SetError(error);
            return UINT32_MAX;
        }
    }
    if (thread->suspended)
        thread->suspended--;
    if (!thread->suspended)
        lx_host_monitor_wake_all(thread->monitor);
    lx_host_monitor_unlock(thread->monitor);
    lx_handle_release(&borrow);
    return previous;
}

ULONG WaitForSingleObject(ULONG handle, ULONG timeout)
{
    static int (*const waiters[])(ULONG, ULONG, ULONG *) = {open32WaitSemaphore, open32WaitSync,
                                                            open32WaitProcess};
    ULONG object_result;
    for (size_t i = 0; i < sizeof waiters / sizeof *waiters; i++)
        if (waiters[i](handle, timeout, &object_result))
            return object_result;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return WIN32_WAIT_FAILED;
    }
    Open32Thread *thread = borrow.resource;
    uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * US_PER_MS;
    lx_host_monitor_lock(thread->monitor);
    while (!thread->finished)
    {
        uint64 now = lx_host_monotonic_us();
        if (timeout != WIN32_INFINITE && now >= deadline)
            break;
        ULONG remaining = timeout == WIN32_INFINITE
                              ? UINT32_MAX
                              : (ULONG)((deadline - now + US_PER_MS - 1) / US_PER_MS);
        lx_host_monitor_wait(thread->monitor, remaining);
    }
    ULONG result = thread->finished ? WIN32_WAIT_OBJECT_0 : WIN32_WAIT_TIMEOUT;
    lx_host_monitor_unlock(thread->monitor);
    lx_handle_release(&borrow);
    return result;
}

BOOL open32CloseThread(ULONG handle)
{
    return lx_handle_close(handle, LX_HANDLE_OPEN32_THREAD);
}

VOID ExitThread(ULONG result)
{
    uint8 arguments[BRIDGE_ARGUMENT_BYTES];
    lx_guest_store_u32(arguments, result);
    if (current_thread.resource)
        complete_thread(arguments);
    GProcessState.dosExit(EXIT_THREAD, result);
    abort();
}

BOOL SetThreadPriority(ULONG handle, LONG priority)
{
    (void)priority;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    lx_handle_release(&borrow);
    /* The host thread provider exposes no scheduling-priority operation. */
    open32SetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

static ULONG suspend_thread(Open32Thread *thread)
{
    lx_host_monitor_lock(thread->monitor);
    if (thread->finished)
    {
        lx_host_monitor_unlock(thread->monitor);
        open32SetError(ERROR_INVALID_THREADID);
        return UINT32_MAX;
    }
    ULONG previous = thread->suspended;
    if (previous || !thread->started)
    {
        thread->suspended++;
        lx_host_monitor_unlock(thread->monitor);
        return previous;
    }
    thread->suspended = 1;
    lx_host_monitor_unlock(thread->monitor);
    /* A thread suspending itself parks here, so the monitor must already be free. */
    ULONG error = dos_thread_call("DosSuspendThread", thread->id);
    if (!error)
        return 0;
    lx_host_monitor_lock(thread->monitor);
    thread->suspended = 0;
    lx_host_monitor_unlock(thread->monitor);
    open32SetError(error);
    return UINT32_MAX;
}

ULONG SuspendThread(ULONG handle)
{
    ULONG child_previous;
    if (open32ChildThreadSuspend(handle, &child_previous))
        return child_previous;
    if (handle == CURRENT_THREAD_HANDLE)
    {
        if (current_thread.resource)
            return suspend_thread(current_thread.resource);
        ULONG error = dos_thread_call("DosSuspendThread", GetCurrentThreadId());
        if (error)
            open32SetError(error);
        return error ? UINT32_MAX : 0;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    ULONG previous = suspend_thread(borrow.resource);
    lx_handle_release(&borrow);
    return previous;
}

BOOL TerminateThread(ULONG handle, ULONG code)
{
    BOOL child_result;
    if (open32ChildThreadTerminate(handle, code, &child_result))
        return child_result;
    Open32Thread *self = current_thread.resource, *thread = self;
    LxHandleBorrow borrow = {0};
    if (handle != CURRENT_THREAD_HANDLE)
    {
        if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &borrow))
        {
            open32SetError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        thread = borrow.resource;
    }
    if (thread && thread == self)
    {
        lx_handle_release(&borrow);
        ExitThread(code);
    }
    ULONG error = dos_thread_call("DosKillThread", thread ? thread->id : GetCurrentThreadId());
    if (error)
    {
        lx_handle_release(&borrow);
        open32SetError(error);
        return FALSE;
    }
    if (thread)
    {
        /* A killed worker never reaches complete_thread; its handle must still signal. */
        lx_host_monitor_lock(thread->monitor);
        BOOL orphaned = !thread->finished && !thread->adopted;
        /* An adopted thread's own end may run first, so this code always stands. */
        if (orphaned || thread->adopted)
        {
            thread->result = code;
            thread->finished = TRUE;
            lx_host_monitor_wake_all(thread->monitor);
        }
        lx_host_monitor_unlock(thread->monitor);
        if (orphaned)
        {
            lx_handle_close(thread->worker_handle, LX_HANDLE_OPEN32_THREAD);
            lx_handle_release(&thread->entry);
        }
    }
    lx_handle_release(&borrow);
    return TRUE;
}

/* Host thread teardown covers every way out: return, DosExit and cancellation. */
static void adopted_thread_ended(void *value)
{
    Open32Thread *thread = value;
    lx_host_monitor_lock(thread->monitor);
    if (!thread->finished)
    {
        thread->result = thread->adopted_result;
        thread->finished = TRUE;
        lx_host_monitor_wake_all(thread->monitor);
    }
    lx_host_monitor_unlock(thread->monitor);
    lx_handle_release(&thread->entry);
}

static void note_thread_exit(ULONG code)
{
    Open32Thread *thread = current_thread.resource;
    if (thread && thread->adopted)
        thread->adopted_result = code;
}

static void initialize_adoption(void)
{
    typedef void(OS2API * ObserveFn)(void (*)(ULONG));
    ObserveFn observe = (ObserveFn)open32DosService("lx_doscalls_observe_thread_exit");
    adopted_key = lx_host_thread_local_create(adopted_thread_ended);
    if (observe)
        observe(note_thread_exit);
}

/* A thread not started by CreateThread gains a record that signals when it ends. */
ULONG open32DuplicateCurrentThread(void)
{
    Open32Thread *thread = current_thread.resource;
    if (thread)
        return lx_handle_create(LX_HANDLE_OPEN32_THREAD, thread, destroy_thread);
    lx_host_once(&adoption_once, initialize_adoption);
    if (!adopted_key)
        return 0;
    thread = calloc(1, sizeof *thread);
    if (thread)
        thread->monitor = lx_host_monitor_create();
    if (!thread || !thread->monitor)
    {
        free(thread);
        return 0;
    }
    thread->id = GetCurrentThreadId();
    thread->result = WIN32_STILL_ACTIVE;
    thread->started = thread->adopted = TRUE;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_THREAD, thread, destroy_thread);
    if (!handle)
    {
        destroy_thread(thread);
        return 0;
    }
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_THREAD, &current_thread))
    {
        lx_handle_close(handle, LX_HANDLE_OPEN32_THREAD);
        return 0;
    }
    thread->entry = current_thread;
    if (!lx_host_thread_local_set(adopted_key, thread))
    {
        lx_handle_release(&current_thread);
        lx_handle_close(handle, LX_HANDLE_OPEN32_THREAD);
        return 0;
    }
    return handle;
}
