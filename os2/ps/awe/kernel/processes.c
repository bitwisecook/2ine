/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 process objects. Handles are process-local, so one object lives only in
   its launcher. A watcher thread records completion, and a shared launch record
   carries the full exit code and input-idle state that a host status cannot. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/kernel/processes.h"
#include "os2/include/private/bsep.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include "emulator/handles.h"
#include "emulator/launch_record.h"
#include "host/clock.h"
#include "host/process.h"
#include "host/shared_memory.h"
#include "host/sync.h"
#include "host/thread.h"
#include <errno.h>
#include <stdio.h>
#include <strings.h>

enum
{
    /* Longest program name taken from the command line, before ".EXE" is added. */
    NAME_LIMIT = 0x103,
    PRIMARY_THREAD_ID = 1,
    PRIORITY_CLASS_MASK = WIN32_NORMAL_PRIORITY_CLASS | WIN32_IDLE_PRIORITY_CLASS |
                          WIN32_HIGH_PRIORITY_CLASS | WIN32_REALTIME_PRIORITY_CLASS,
    /* A host child killed by a signal reports 128 plus the signal, as a shell does. */
    SIGNAL_EXIT_BASE = 128,
    LAUNCH_RECORD_NAME_SIZE = 40,
    TITLE_LIMIT = 65536,
    US_PER_MS = 1000,
    /* WaitForInputIdle polls the launch record this often. */
    IDLE_POLL_MS = 10,
    /* WinExec returns a value above 31 on success. */
    WINEXEC_SUCCESS = 0x20,
    /* Bits 24-31 of an access mask (generic and special rights) are refused. */
    ACCESS_SPECIFIC_STANDARD_BITS = 24
};

#define PSEUDO_PROCESS UINT32_MAX
#define PSEUDO_THREAD (UINT32_MAX - 1)

typedef struct Open32Process
{
    LxHostMonitor *monitor;
    LxHostChild *child;
    LxHostShared *shared;
    LxLaunchRecord *record;
    ULONG id, exit_code, requested_exit, suspended, references, priority_class;
    BOOL finished, terminated;
    struct Open32Process *next;
} Open32Process;

/* One record per handle family, so a closing handle cannot race a new reference. */
typedef struct ProcessHandle
{
    Open32Process *process;
} ProcessHandle;

typedef struct ProcessResult
{
    ULONG process, thread, id;
} ProcessResult;

static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *registry;
static Open32Process *processes;
static Open32Process current;

static void initialize(void)
{
    registry = lx_host_monitor_create();
    current.monitor = lx_host_monitor_create();
    if (!registry || !current.monitor)
        abort();
    current.id = GetCurrentProcessId();
    current.exit_code = WIN32_STILL_ACTIVE;
}

static void retain_process(Open32Process *process)
{
    lx_host_monitor_lock(registry);
    process->references++;
    lx_host_monitor_unlock(registry);
}

static void release_process(Open32Process *process)
{
    if (process == &current)
        return;
    lx_host_monitor_lock(registry);
    BOOL last = !--process->references;
    if (last)
        for (Open32Process **link = &processes; *link; link = &(*link)->next)
            if (*link == process)
            {
                *link = process->next;
                break;
            }
    lx_host_monitor_unlock(registry);
    if (!last)
        return;
    lx_host_child_release(process->child);
    lx_launch_record_close(process->shared, process->record);
    if (process->monitor)
        lx_host_monitor_destroy(process->monitor);
    free(process);
}

static void destroy_handle(void *resource)
{
    ProcessHandle *handle = resource;
    release_process(handle->process);
    free(handle);
}

static ULONG make_handle(LxHandleKind kind, Open32Process *process)
{
    ProcessHandle *reference = calloc(1, sizeof *reference);
    if (!reference)
        return 0;
    reference->process = process;
    retain_process(process);
    ULONG handle = lx_handle_create(kind, reference, destroy_handle);
    if (!handle)
        destroy_handle(reference);
    return handle;
}

static Open32Process *resolve(ULONG handle, LxHandleKind kind, LxHandleBorrow *borrow)
{
    lx_host_once(&once, initialize);
    memset(borrow, 0, sizeof *borrow);
    if (kind == LX_HANDLE_OPEN32_PROCESS && handle == PSEUDO_PROCESS)
        return &current;
    return lx_handle_borrow(handle, kind, borrow) ? ((ProcessHandle *)borrow->resource)->process
                                                  : NULL;
}

static ULONG finished_code(const Open32Process *process, const LxHostChildStatus *status)
{
    if (process->record && __atomic_load_n(&process->record->exit_recorded, __ATOMIC_ACQUIRE))
        return __atomic_load_n(&process->record->exit_code, __ATOMIC_RELAXED);
    if (process->terminated)
        return process->requested_exit;
    return status->signal ? SIGNAL_EXIT_BASE + status->signal : status->result;
}

static void *watch_process(void *argument)
{
    Open32Process *process = argument;
    LxHostChildStatus status = {0};
    int waited = lx_host_child_wait(process->child, 0, &status);
    lx_host_monitor_lock(process->monitor);
    process->exit_code = waited == 1 ? finished_code(process, &status) : 1;
    process->finished = TRUE;
    lx_host_monitor_wake_all(process->monitor);
    lx_host_monitor_unlock(process->monitor);
    release_process(process);
    return NULL;
}

/* The launcher's reference covers failure paths; handles and the watcher add theirs. */
static ULONG launch_process(PSZ program, PSZ arguments, PSZ environment, PSZ directory,
                            BOOL suspended, int priority, ULONG priority_class,
                            const LxLaunchPresentation *presentation, ProcessResult *result)
{
    lx_host_once(&once, initialize);
    typedef APIRET(OS2API * LaunchFn)(const LxDosLaunch *, LxHostChild **);
    LaunchFn launch = (LaunchFn)open32DosService("lx_doscalls_launch");
    if (!launch)
        return ERROR_CALL_NOT_IMPLEMENTED;
    Open32Process *process = calloc(1, sizeof *process);
    if (process)
    {
        process->references = 1;
        process->exit_code = WIN32_STILL_ACTIVE;
        process->suspended = suspended ? 1 : 0;
        process->priority_class = priority_class;
        process->monitor = lx_host_monitor_create();
    }
    char name[LAUNCH_RECORD_NAME_SIZE];
    if (process && process->monitor)
        process->record = lx_launch_record_create('o', &process->shared, name, sizeof name);
    if (!process || !process->record)
    {
        if (process)
            release_process(process);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    lx_launch_record_present(process->record, presentation);
    /* A requested priority is set while the child is held, so it never runs without it. */
    BOOL held = suspended || priority >= 0;
    LxDosLaunch request = {.program = program,
                           .arguments = arguments,
                           .environment = environment,
                           .directory = directory,
                           .record = name,
                           .flags = held ? LX_DOS_LAUNCH_SUSPENDED : 0};
    ULONG error = launch(&request, &process->child);
    if (!error && priority >= 0 &&
        lx_host_child_set_priority(process->child, (LxHostProcessPriority)priority))
        error = errno == EPERM || errno == EACCES ? ERROR_ACCESS_DENIED : ERROR_NOT_SUPPORTED;
    if (!error && held && !suspended && lx_host_child_resume(process->child))
        error = ERROR_INVALID_FUNCTION;
    if (error)
    {
        if (process->child)
            lx_host_child_terminate(process->child);
        release_process(process);
        return error;
    }
    process->id = lx_host_child_id(process->child);
    lx_host_monitor_lock(registry);
    process->next = processes;
    processes = process;
    lx_host_monitor_unlock(registry);
    result->id = process->id;
    result->process = make_handle(LX_HANDLE_OPEN32_PROCESS, process);
    result->thread = result->process ? make_handle(LX_HANDLE_OPEN32_CHILD_THREAD, process) : 0;
    retain_process(process);
    LxHostThread *watcher =
        result->thread ? lx_host_thread_create(watch_process, process, NULL) : NULL;
    if (watcher)
    {
        lx_host_thread_detach(watcher);
        release_process(process);
        return 0;
    }
    lx_host_child_terminate(process->child);
    release_process(process);
    if (result->thread)
        lx_handle_close(result->thread, LX_HANDLE_OPEN32_CHILD_THREAD);
    if (result->process)
        lx_handle_close(result->process, LX_HANDLE_OPEN32_PROCESS);
    release_process(process);
    return ERROR_NOT_ENOUGH_MEMORY;
}

/* The profile value holds ';'-separated settings for a Win-OS/2 session. */
static char *windows_environment(void)
{
    ULONG size = 0;
    /* The block gets two terminators, ending the list of strings made from it. */
    if (!PrfQueryProfileSize(HINI_USERPROFILE, "WINOS2", "PM_GlobalWindows31Settings", &size))
        return NULL;
    char *block = calloc(1, (size_t)size + 2);
    if (!block)
        return NULL;
    if (!PrfQueryProfileData(HINI_USERPROFILE, "WINOS2", "PM_GlobalWindows31Settings", block,
                             &size))
    {
        free(block);
        return NULL;
    }
    for (char *p = block; *p; p++)
        if (*p == ';')
            *p = 0;
    return block;
}

static char *argument_block(const char *program, const char *arguments)
{
    size_t name = strlen(program) + 1, tail = arguments ? strlen(arguments) : 0;
    /* Program name, NUL, arguments and a double NUL, as DosExecPgm takes them. */
    char *block = calloc(1, name + tail + 2);
    if (block)
    {
        memcpy(block, program, name);
        if (tail)
            memcpy(block + name, arguments, tail);
    }
    return block;
}

/* Negative inherits the caller's level, as a host child does by default. */
static int requested_priority(ULONG flags)
{
    ULONG selected = flags & PRIORITY_CLASS_MASK;
    if (!selected)
        return -1;
    if (selected & WIN32_NORMAL_PRIORITY_CLASS)
        return LX_HOST_PROCESS_PRIORITY_NORMAL;
    if (selected & WIN32_IDLE_PRIORITY_CLASS)
        return LX_HOST_PROCESS_PRIORITY_IDLE;
    return selected & WIN32_HIGH_PRIORITY_CLASS ? LX_HOST_PROCESS_PRIORITY_NORMAL
                                                : LX_HOST_PROCESS_PRIORITY_REALTIME;
}

/* Without a request a child reports idle only when its creator is idle. */
static ULONG reported_priority_class(ULONG flags)
{
    ULONG selected = flags & PRIORITY_CLASS_MASK;
    if (selected)
        return selected;
    return GetPriorityClass(PSEUDO_PROCESS) == WIN32_IDLE_PRIORITY_CLASS
               ? WIN32_IDLE_PRIORITY_CLASS
               : WIN32_NORMAL_PRIORITY_CLASS;
}

static BOOL create_process(PSZ application, PSZ command, ULONG flags, PSZ environment,
                           PSZ directory, const LxLaunchPresentation *presentation,
                           ProcessResult *result)
{
    if (!application && !command)
    {
        open32SetError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    /* No host offers a realtime class, so refuse before anything starts. */
    int priority = requested_priority(flags);
    if (priority == LX_HOST_PROCESS_PRIORITY_REALTIME)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    /* The first space ends the program name; the command has no quoting rules. */
    const char *space = command ? strchr(command, ' ') : NULL;
    PSZ tail = space ? (PSZ)space + 1 : NULL;
    const char *source = application ? application : command;
    size_t length = application || !space ? strlen(source) : (size_t)(space - command);
    if (length > NAME_LIMIT)
        length = NAME_LIMIT;
    char name[NAME_LIMIT + 6] = {0}, found[NAME_LIMIT + 1];
    memcpy(name, source, length);
    name[length] = 0;
    PSZ program = name;
    if (name[0] != '\\' && (name[1] != ':' || name[2] != '\\'))
    {
        if (!strchr(name, '.'))
            strcat(name, ".EXE");
        typedef APIRET(OS2API * SearchFn)(ULONG, PSZ, PSZ, PBYTE, ULONG);
        SearchFn search = (SearchFn)open32DosService("DosSearchPath");
        ULONG error = search
                          ? search(SEARCH_CUR_DIRECTORY | SEARCH_ENVIRONMENT | SEARCH_IGNORENETERRS,
                                   "PATH", name, (PBYTE)found, sizeof found)
                          : ERROR_CALL_NOT_IMPLEMENTED;
        if (error)
        {
            open32SetError(error);
            return FALSE;
        }
        program = found;
    }
    char *settings = NULL, *windows_inputs = NULL;
    const char *inputs = tail;
    if (flags & WIN32_CREATE_NEW_PROCESS_GROUP)
    {
        typedef APIRET(OS2API * TypeFn)(PSZ, PULONG);
        TypeFn query = (TypeFn)open32DosService("DosQueryAppType");
        ULONG type = 0, error = query ? query(program, &type) : ERROR_CALL_NOT_IMPLEMENTED;
        if (error)
        {
            open32SetError(error);
            return FALSE;
        }
        if (type & FAPPTYP_WINDOWSPROT31)
        {
            environment = settings = windows_environment();
            inputs = program;
            if (settings)
            {
                /* "/s ", a space and the terminator. */
                size_t size = strlen(program) + (tail ? strlen(tail) : 0) + 6;
                if (!(windows_inputs = malloc(size)))
                {
                    free(settings);
                    open32SetError(ERROR_NOT_ENOUGH_MEMORY);
                    return FALSE;
                }
                snprintf(windows_inputs, size, "/s %s%s%s", program, tail ? " " : "",
                         tail ? tail : "");
                inputs = windows_inputs;
            }
            program = "WINOS2.COM";
        }
        else if (type & FAPPTYP_DOS)
            environment = NULL;
    }
    else if ((flags & WIN32_DEBUG_PROCESS) &&
             !(flags & (WIN32_DETACHED_PROCESS | WIN32_CREATE_SUSPENDED)))
    {
        /* The DOS layer refuses debug execution with this same error. */
        open32SetError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    char *block = argument_block(program, inputs);
    ULONG error = block ? launch_process(program, block, environment, directory,
                                         (flags & WIN32_CREATE_SUSPENDED) != 0, priority,
                                         reported_priority_class(flags), presentation, result)
                        : ERROR_NOT_ENOUGH_MEMORY;
    free(block);
    free(windows_inputs);
    free(settings);
    if (error)
        open32SetError(error);
    return !error;
}

static uint32 show_control(ULONG show)
{
    switch (show)
    {
        case WIN32_SW_HIDE:
            return LX_LAUNCH_INVISIBLE;
        case WIN32_SW_SHOWMINIMIZED:
            return LX_LAUNCH_MINIMIZE;
        case WIN32_SW_SHOWMAXIMIZED:
            return LX_LAUNCH_MAXIMIZE;
        default:
            return 0;
    }
}

static void startup_presentation(const uint8 *startup, LxLaunchPresentation *presentation)
{
    if (!startup)
        return;
    ULONG flags = lx_guest_load_u32(startup + WIN32_STARTUPINFO_FLAGS);
    if (flags & WIN32_STARTF_USESHOWWINDOW)
        presentation->control |=
            show_control(lx_guest_load_u16(startup + WIN32_STARTUPINFO_SHOW_WINDOW));
    if (flags & WIN32_STARTF_USEPOSITION)
    {
        presentation->control |= LX_LAUNCH_SET_POSITION;
        presentation->x = (int32_t)lx_guest_load_u32(startup + WIN32_STARTUPINFO_X);
        presentation->y = (int32_t)lx_guest_load_u32(startup + WIN32_STARTUPINFO_Y);
    }
    if (flags & WIN32_STARTF_USESIZE)
    {
        presentation->control |= LX_LAUNCH_SET_SIZE;
        presentation->cx = (int32_t)lx_guest_load_u32(startup + WIN32_STARTUPINFO_X_SIZE);
        presentation->cy = (int32_t)lx_guest_load_u32(startup + WIN32_STARTUPINFO_Y_SIZE);
    }
    ULONG title = lx_guest_load_u32(startup + WIN32_STARTUPINFO_TITLE);
    char *text =
        title ? lx_execution_copy_string(GProcessState.execution, title, TITLE_LIMIT) : NULL;
    if (text)
        snprintf(presentation->title, sizeof presentation->title, "%s", text);
    free(text);
}

BOOL CreateProcess(PSZ application, PSZ command, PVOID process_security, PVOID thread_security,
                   BOOL inherit, ULONG flags, PVOID environment, PSZ directory, PVOID startup,
                   PVOID information)
{
    TRACE_NATIVE("CreateProcess('%s', '%s', %X, '%s')", application, command, (unsigned)flags,
                 directory);
    /* The original reads neither the security records nor the inherit flag. */
    (void)process_security;
    (void)thread_security;
    (void)inherit;
    LxLaunchPresentation presentation = {0};
    startup_presentation(startup, &presentation);
    ProcessResult result = {0};
    if (!create_process(application, command, flags, environment, directory, &presentation,
                        &result))
        return FALSE;
    lx_guest_store_u32((uint8 *)information + WIN32_PROCESS_INFORMATION_PROCESS, result.process);
    lx_guest_store_u32((uint8 *)information + WIN32_PROCESS_INFORMATION_THREAD, result.thread);
    lx_guest_store_u32((uint8 *)information + WIN32_PROCESS_INFORMATION_PROCESS_ID, result.id);
    lx_guest_store_u32((uint8 *)information + WIN32_PROCESS_INFORMATION_THREAD_ID,
                       PRIMARY_THREAD_ID);
    return TRUE;
}

BOOL open32CloseProcess(ULONG handle)
{
    return lx_handle_close(handle, LX_HANDLE_OPEN32_PROCESS) ||
           lx_handle_close(handle, LX_HANDLE_OPEN32_CHILD_THREAD);
}

typedef struct ProcessWait
{
    LxHandleBorrow borrow;
    Open32Process *process;
    ULONG timeout;
    BOOL input_idle;
} ProcessWait;

static void release_wait(void *argument)
{
    lx_handle_release(&((ProcessWait *)argument)->borrow);
}

/* Input idle has no wake source in this process, so that wait polls the record. */
static uint32 wait_process(void *argument)
{
    ProcessWait *wait = argument;
    Open32Process *process = wait->process;
    uint64 deadline = lx_host_monotonic_us() + (uint64)wait->timeout * US_PER_MS;
    uint32 result = WIN32_WAIT_TIMEOUT;
    lx_host_monitor_lock(process->monitor);
    for (;;)
    {
        if (wait->input_idle ? __atomic_load_n(&process->record->input_idle, __ATOMIC_ACQUIRE) != 0
                             : process->finished)
        {
            result = WIN32_WAIT_OBJECT_0;
            break;
        }
        if (wait->input_idle && process->finished)
            break;
        uint64 now = lx_host_monotonic_us();
        if (wait->timeout != WIN32_INFINITE && now >= deadline)
            break;
        ULONG remaining = wait->timeout == WIN32_INFINITE
                              ? UINT32_MAX
                              : (ULONG)((deadline - now + US_PER_MS - 1) / US_PER_MS);
        if (wait->input_idle && remaining > IDLE_POLL_MS)
            remaining = IDLE_POLL_MS;
        lx_host_monitor_wait(process->monitor, remaining);
    }
    lx_host_monitor_unlock(process->monitor);
    return result;
}

int open32WaitProcess(ULONG handle, ULONG timeout, ULONG *result)
{
    ProcessWait wait = {.timeout = timeout};
    wait.process = resolve(handle, LX_HANDLE_OPEN32_PROCESS, &wait.borrow);
    if (!wait.process)
        wait.process = resolve(handle, LX_HANDLE_OPEN32_CHILD_THREAD, &wait.borrow);
    if (!wait.process)
        return 0;
    *result = lx_host_guarded_call(wait_process, &wait, release_wait, &wait);
    return 1;
}

ULONG WaitForInputIdle(ULONG handle, ULONG timeout)
{
    ProcessWait wait = {.timeout = timeout, .input_idle = TRUE};
    wait.process = resolve(handle, LX_HANDLE_OPEN32_PROCESS, &wait.borrow);
    if (!wait.process)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return WIN32_WAIT_FAILED;
    }
    if (!wait.process->record)
    {
        /* Only a launched child has a record; this process's own queue reports nowhere. */
        lx_handle_release(&wait.borrow);
        open32SetError(ERROR_NOT_SUPPORTED);
        return WIN32_WAIT_FAILED;
    }
    return lx_host_guarded_call(wait_process, &wait, release_wait, &wait);
}

BOOL GetExitCodeProcess(ULONG handle, PULONG code)
{
    LxHandleBorrow borrow;
    Open32Process *process = code ? resolve(handle, LX_HANDLE_OPEN32_PROCESS, &borrow) : NULL;
    if (!process)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lx_host_monitor_lock(process->monitor);
    *code = process->exit_code;
    lx_host_monitor_unlock(process->monitor);
    lx_handle_release(&borrow);
    return TRUE;
}

int open32ChildThreadExitCode(ULONG handle, PULONG code)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_CHILD_THREAD, &borrow);
    if (!process)
        return 0;
    lx_host_monitor_lock(process->monitor);
    *code = process->exit_code;
    lx_host_monitor_unlock(process->monitor);
    lx_handle_release(&borrow);
    return 1;
}

int open32ChildThreadResume(ULONG handle, PULONG previous)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_CHILD_THREAD, &borrow);
    if (!process)
        return 0;
    lx_host_monitor_lock(process->monitor);
    ULONG count = process->suspended;
    int resumed = count == 1 ? lx_host_child_resume(process->child) : 0;
    if (resumed < 0)
    {
        open32SetError(ERROR_INVALID_THREADID);
        *previous = UINT32_MAX;
    }
    else
    {
        if (count)
            process->suspended = count - 1;
        *previous = count;
    }
    lx_host_monitor_unlock(process->monitor);
    lx_handle_release(&borrow);
    return 1;
}

int open32ChildThreadSuspend(ULONG handle, PULONG previous)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_CHILD_THREAD, &borrow);
    if (!process)
        return 0;
    lx_host_monitor_lock(process->monitor);
    ULONG count = process->suspended;
    if (!count && lx_host_child_suspend(process->child))
    {
        open32SetError(ERROR_INVALID_THREADID);
        *previous = UINT32_MAX;
    }
    else
    {
        process->suspended = count + 1;
        *previous = count;
    }
    lx_host_monitor_unlock(process->monitor);
    lx_handle_release(&borrow);
    return 1;
}

int open32ChildThreadTerminate(ULONG handle, ULONG code, BOOL *result)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_CHILD_THREAD, &borrow);
    if (!process)
        return 0;
    /* A primary thread cannot end alone here: the host child is one unit. */
    lx_host_monitor_lock(process->monitor);
    process->requested_exit = code;
    process->terminated = TRUE;
    lx_host_monitor_unlock(process->monitor);
    *result = lx_host_child_terminate(process->child) == 0;
    if (!*result)
        open32SetError(ERROR_INVALID_THREADID);
    lx_handle_release(&borrow);
    return 1;
}

int open32ChildSetPriority(ULONG handle, LxHostProcessPriority priority, ULONG priority_class,
                           int *error)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_PROCESS, &borrow);
    if (!process)
        return -1;
    if (process == &current)
        return 0;
    lx_host_monitor_lock(process->monitor);
    *error = lx_host_child_set_priority(process->child, priority) ? errno : 0;
    if (!*error)
        process->priority_class = priority_class;
    lx_host_monitor_unlock(process->monitor);
    lx_handle_release(&borrow);
    return 1;
}

int open32ChildPriorityClass(ULONG handle, PULONG priority_class)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_PROCESS, &borrow);
    if (!process)
        return -1;
    if (process == &current)
        return 0;
    lx_host_monitor_lock(process->monitor);
    *priority_class = process->priority_class;
    lx_host_monitor_unlock(process->monitor);
    lx_handle_release(&borrow);
    return 1;
}

ULONG OpenProcess(ULONG access, BOOL inherit, ULONG id)
{
    (void)access;
    (void)inherit;
    if (!id)
        return 0;
    lx_host_once(&once, initialize);
    if (id == current.id)
        return make_handle(LX_HANDLE_OPEN32_PROCESS, &current);
    /* A listed object has a live reference, so it survives until retained below. */
    Open32Process *found = NULL;
    lx_host_monitor_lock(registry);
    for (Open32Process *process = processes; process; process = process->next)
        if (process->id == id)
        {
            found = process;
            found->references++;
            break;
        }
    lx_host_monitor_unlock(registry);
    if (!found)
        return 0;
    ULONG handle = make_handle(LX_HANDLE_OPEN32_PROCESS, found);
    release_process(found);
    return handle;
}

static BOOL local_process(ULONG handle, BOOL *known)
{
    LxHandleBorrow borrow;
    Open32Process *process = resolve(handle, LX_HANDLE_OPEN32_PROCESS, &borrow);
    *known = process != NULL;
    BOOL local = process == &current;
    lx_handle_release(&borrow);
    return local;
}

static ULONG duplicate_object(ULONG source, const uint32_t *access)
{
    static const LxHandleKind kinds[] = {LX_HANDLE_OPEN32_FILE,        LX_HANDLE_OPEN32_EVENT,
                                         LX_HANDLE_OPEN32_MUTEX,       LX_HANDLE_OPEN32_SEMAPHORE,
                                         LX_HANDLE_OPEN32_THREAD,      LX_HANDLE_OPEN32_PROCESS,
                                         LX_HANDLE_OPEN32_CHILD_THREAD};
    if (source == PSEUDO_PROCESS)
        return make_handle(LX_HANDLE_OPEN32_PROCESS, &current);
    if (source == PSEUDO_THREAD)
        return open32DuplicateCurrentThread();
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++)
    {
        ULONG copy = lx_handle_duplicate(source, kinds[i], access);
        if (copy)
            return copy;
    }
    /* Anything else may be a plain DOS file handle from the _l file calls. */
    typedef APIRET(OS2API * DupFn)(HFILE, PHFILE);
    DupFn duplicate = (DupFn)open32DosService("DosDupHandle");
    HFILE copy = (HFILE)UINT32_MAX;
    return duplicate && !duplicate((HFILE)source, &copy) ? (ULONG)copy : 0;
}

BOOL DuplicateHandle(ULONG source_process, ULONG source, ULONG target_process, PULONG output,
                     ULONG access, BOOL inherit, ULONG options)
{
    if (access >> ACCESS_SPECIFIC_STANDARD_BITS)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Handle tables are private to each worker, so no child could receive this one. */
    if (inherit)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    BOOL source_known, target_known;
    BOOL local = local_process(source_process, &source_known);
    local = local_process(target_process, &target_known) && local;
    if (!source_known || !target_known)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!local)
    {
        /* Handle tables are private to each worker process. */
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    /* Only event and semaphore operations consult these rights, as in the original. */
    uint32_t rights = access;
    ULONG copy = duplicate_object(source, options & WIN32_DUPLICATE_SAME_ACCESS ? NULL : &rights);
    if (!copy)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    *output = copy;
    if (options & WIN32_DUPLICATE_CLOSE_SOURCE)
        CloseHandle(source);
    return TRUE;
}

VOID ExitProcess(ULONG code)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(current.monitor);
    current.exit_code = code;
    lx_host_monitor_unlock(current.monitor);
    GProcessState.dosExit(EXIT_PROCESS, code);
}

VOID FatalExit(ULONG code)
{
    GProcessState.dosExit(EXIT_PROCESS, code);
}

VOID FatalAppExit(ULONG action, PSZ message)
{
    if (WinQueryCapture(HWND_DESKTOP))
        WinSetCapture(HWND_DESKTOP, 0);
    ULONG answer = WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, message, "FATAL EXIT", 0,
                                 MB_SYSTEMMODAL | MB_ICONHAND);
    /* A thread without a message queue cannot display the box. */
    if (!answer || answer == MBID_ERROR)
        fprintf(stderr, "FATAL EXIT: %s\n", message ? message : "");
    GProcessState.dosExit(EXIT_PROCESS, action);
}

/* The session is the ball: this worker, the children it started, and every other
   live PM worker through its mailbox. The host is never shut down. */
BOOL ExitWindows(ULONG reserved, ULONG code)
{
    (void)reserved;
    (void)code;
    if (!WinPostMsg(0, WM_QUIT, 0, 0))
        return FALSE;
    BOOL delivered = lx_pm_end_session();
    if (!delivered)
        open32SetError(ERROR_GEN_FAILURE);
    WinBroadcastMsg(HWND_DESKTOP, WM_SAVEAPPLICATION, 0, 0, BMSG_SEND | BMSG_FRAMEONLY);
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(registry);
    for (Open32Process *process = processes; process; process = process->next)
        lx_host_child_terminate(process->child);
    lx_host_monitor_unlock(registry);
    return delivered;
}

ULONG WinExec(PSZ command, ULONG show)
{
    if (!command)
        return 0;
    LxLaunchPresentation presentation = {.control = show_control(show)};
    ProcessResult result = {0};
    if (!create_process(NULL, command, WIN32_CREATE_NEW_PROCESS_GROUP, NULL, NULL, &presentation,
                        &result))
    {
        ULONG error = GetLastError();
        /* WinExec returns only these errors; anything else reports 0, out of memory. */
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                       error == ERROR_BAD_FORMAT
                   ? error
                   : 0;
    }
    open32CloseProcess(result.process);
    open32CloseProcess(result.thread);
    return WINEXEC_SUCCESS;
}

ULONG GetCurrentProcessId(void)
{
    return GProcessState.pib.process_id;
}

ULONG GetCurrentProcess(void)
{
    return PSEUDO_PROCESS;
}
