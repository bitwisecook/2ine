/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2libs/os2native16.h"
#include "os2libs/sesmgr.h"
#include "os2libs/sesmgr_start.h"
#include "os2libs/doscalls.h"
#include "os2libs/doscalls/launch.h"
#include "guest/launch_record.h"
#include "os2libs/quecalls.h"
#include "hal/host_thread.h"
#include "hal/host_sync.h"
#include "hal/host_mailbox.h"
#include "hal/host.h"
#include "os2libs/pmwin.h"

#include "os2libs/generated/sesmgr-lx.h"

typedef struct SessionWait
{
    struct SessionWait *next;
    PID pid;
    ULONG id;
    LxHostMailbox *identity;
    HQUEUE queue;
    BOOL related;
    BOOL selectable, bonded;
    LxSessionEnded ended;
    void *context;
    LxHostShared *shared;
    LxLaunchRecord *record;
} SessionWait;

static SessionWait *sessions;
static LxHostMutex *session_mutex;
static LxHostOnce session_once = LX_HOST_ONCE_INIT;
static char session_title[64];

static void destroy_session(SessionWait *session)
{
    lx_launch_record_close(session->shared, session->record);
    if (session->queue)
        DosCloseQueue(session->queue);
    if (session->identity)
        lx_host_mailbox_close(session->identity);
    free(session);
}

static BOOL reserve_session(SessionWait *session)
{
    uint32 process, parent;
    lx_host_process_ids(&process, &parent);
    for (ULONG offset = 0; offset < 0xfffe; offset++)
    {
        ULONG id = (process + offset) % 0xfffe + 1;
        char name[32];
        snprintf(name, sizeof name, "session-%04x", (unsigned)id);
        uint32 owner;
        int result =
            lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 1, 0, &session->identity, &owner);
        if (result == 2)
            continue;
        if (result)
            return FALSE;
        session->id = id;
        return TRUE;
    }
    return FALSE;
}

static void initialize_sessions(void)
{
    session_mutex = lx_host_mutex_create(0, 0);
    if (!session_mutex)
        abort();
}

static void remove_session(SessionWait *session)
{
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    for (SessionWait **link = &sessions; *link; link = &(*link)->next)
        if (*link == session)
        {
            *link = session->next;
            break;
        }
    lx_host_mutex_unlock(session_mutex);
}

APIRET DosSelectSession(ULONG session)
{
    TRACE_NATIVE("DosSelectSession(%u)", (unsigned)session);
    if (!session)
    {
        HWND active = WinQueryActiveWindow(HWND_DESKTOP);
        return active && WinSetActiveWindow(HWND_DESKTOP, active) ? NO_ERROR : 224;
    }
    lx_host_once(&session_once, initialize_sessions);
    APIRET result = 369; /* ERROR_SMG_INVALID_SESSION_ID */
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    for (SessionWait *item = sessions; item; item = item->next)
        if (item->id == session)
        {
            /* Child foreground selection requires a target host window. */
            result = item->related ? 224 : 460; /* NO_TARGET_WINDOW / PROCESS_NOT_PARENT */
            break;
        }
    lx_host_mutex_unlock(session_mutex);
    return result;
}

APIRET DosSetSession(ULONG session, PSTATUSDATA status)
{
    TRACE_NATIVE("DosSetSession(%u, %p)", session, status);
    if (!status)
        return ERROR_INVALID_PARAMETER;
    if (status->Length != 6)
        return 461;
    if (status->SelectInd > 2)
        return 456;
    if (status->BondInd > 2)
        return 455;
    lx_host_once(&session_once, initialize_sessions);
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    APIRET result = 369;
    for (SessionWait *item = sessions; item; item = item->next)
        if (item->id == session)
        {
            result = item->related ? NO_ERROR : 460;
            if (!result)
            {
                if (status->SelectInd)
                    item->selectable = status->SelectInd == 1;
                if (status->BondInd == 1)
                    for (SessionWait *other = sessions; other; other = other->next)
                        other->bonded = other == item;
                else if (status->BondInd == 2)
                    item->bonded = FALSE;
            }
            break;
        }
    lx_host_mutex_unlock(session_mutex);
    return result;
}

static void *session_wait(void *value)
{
    SessionWait *session = value;
    RESULTCODES result = {0};
    PID ended = 0;
    if (!DosWaitChild(0, 0, &result, &ended, session->pid) && session->queue)
    {
        USHORT message[2] = {(USHORT)session->id, (USHORT)result.codeResult};
        lx_queue_write_copy(session->queue, 0, sizeof message, message, 0);
    }
    if (session->ended)
    {
        LxLaunchRecord *record = session->record;
        BOOL recorded = __atomic_load_n(&record->exit_recorded, __ATOMIC_ACQUIRE);
        session->ended(session->context, recorded ? record->exit_code : result.codeResult);
    }
    remove_session(session);
    destroy_session(session);
    return NULL;
}

APIRET lx_session_start(const LxSessionStart *start, PULONG session_id, PPID pid)
{
    PSTARTDATA data = start->data;
    if (!data || data->Length < 24 || !data->PgmName || !*data->PgmName || !session_id || !pid)
        return ERROR_INVALID_PARAMETER;
    if (data->Related > 1)
        return ERROR_SMG_INVALID_RELATED_OPT;
    if (data->FgBg > 1)
        return ERROR_SMG_INVALID_START_MODE;
    if (data->TraceOpt > 2)
        return ERROR_SMG_INVALID_TRACE_OPTION;
    if (data->TraceOpt)
        return ERROR_NOT_SUPPORTED;
    if (data->InheritOpt > 1)
        return ERROR_SMG_INVALID_INHERIT_OPT;
    if (data->SessionType > 9)
        return ERROR_SMG_INVALID_PROGRAM_TYPE;
    if (data->Reserved)
        return ERROR_SMG_BAD_RESERVE;
    SessionWait *session = calloc(1, sizeof *session);
    if (!session)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!reserve_session(session))
    {
        destroy_session(session);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (data->TermQ && *data->TermQ)
    {
        PID owner;
        APIRET result = lx_queue_open_private(&owner, &session->queue, (PSZ)data->TermQ);
        if (result)
        {
            destroy_session(session);
            return result;
        }
    }
    char record_name[40];
    session->record =
        lx_launch_record_create('s', &session->shared, record_name, sizeof record_name);
    if (!session->record)
    {
        destroy_session(session);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    LxLaunchPresentation presentation = {.control = data->PgmControl &
                                                    (LX_LAUNCH_INVISIBLE | LX_LAUNCH_MAXIMIZE |
                                                     LX_LAUNCH_MINIMIZE | LX_LAUNCH_NO_AUTOCLOSE |
                                                     LX_LAUNCH_SET_POSITION),
                                         .x = (SHORT)data->InitXPos,
                                         .y = (SHORT)data->InitYPos,
                                         .cx = (SHORT)data->InitXSize,
                                         .cy = (SHORT)data->InitYSize};
    if (presentation.control & LX_LAUNCH_SET_POSITION)
        presentation.control |= LX_LAUNCH_SET_SIZE;
    if (data->FgBg)
        presentation.control |= LX_LAUNCH_BACKGROUND;
    if (data->PgmTitle)
        snprintf(presentation.title, sizeof presentation.title, "%s", data->PgmTitle);
    lx_launch_record_present(session->record, &presentation);
    const char *inputs = data->PgmInputs ? (const char *)data->PgmInputs : "";
    size_t name_size = strlen(data->PgmName) + 1;
    char *command = malloc(name_size + strlen(inputs) + 2);
    if (!command)
    {
        destroy_session(session);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(command, data->PgmName, name_size);
    strcpy(command + name_size, inputs);
    command[name_size + strlen(inputs) + 1] = 0;
    if (data->ObjectBuffer && data->ObjectBuffLen)
        *data->ObjectBuffer = 0;
    LxDosLaunch launch = {.program = data->PgmName,
                          .arguments = command,
                          .environment = (PSZ)data->Environment,
                          .directory = start->directory,
                          .record = record_name,
                          .flags = start->directory ? LX_DOS_LAUNCH_SEARCH_DIRECTORY : 0};
    PID child = 0;
    APIRET error = lx_doscalls_launch_tracked(&launch, &child);
    free(command);
    if (error)
    {
        destroy_session(session);
        return error;
    }
    session->pid = child;
    session->related = data->Related == 1;
    session->selectable = TRUE;
    session->ended = start->ended;
    session->context = start->context;
    *session_id = session->id;
    *pid = session->pid;
    lx_host_once(&session_once, initialize_sessions);
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    session->next = sessions;
    sessions = session;
    lx_host_mutex_unlock(session_mutex);
    LxHostThread *thread = lx_host_thread_create(session_wait, session, NULL);
    if (thread)
        lx_host_thread_detach(thread);
    else
    {
        remove_session(session);
        destroy_session(session);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return NO_ERROR;
}

APIRET DosStartSession(PSTARTDATA data, PULONG session_id, PPID pid)
{
    TRACE_NATIVE("DosStartSession(%p, %p, %p)", data, session_id, pid);
    LxSessionStart start = {.data = data};
    return lx_session_start(&start, session_id, pid);
}

APIRET16 Dos16SMSetTitle(PCHAR title)
{
    TRACE_NATIVE("Dos16SMSetTitle(%p)", title);
    if (!title)
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&session_once, initialize_sessions);
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    snprintf(session_title, sizeof session_title, "%s", title);
    lx_host_mutex_unlock(session_mutex);
    return NO_ERROR;
}

void lx_session_title(char *output, size_t capacity)
{
    lx_host_once(&session_once, initialize_sessions);
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    snprintf(output, capacity, "%s", session_title);
    lx_host_mutex_unlock(session_mutex);
    LxLaunchRecord *record = lx_launch_record();
    if (!*output && record)
        snprintf(output, capacity, "%s", record->presentation.title);
}

APIRET DosStopSession(ULONG scope, ULONG session)
{
    if (scope > 1)
        return ERROR_SMG_INVALID_STOP_OPTION;
    lx_host_once(&session_once, initialize_sessions);
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    size_t count = 0;
    APIRET result = scope ? NO_ERROR : ERROR_SMG_INVALID_SESSION_ID;
    for (SessionWait *item = sessions; item; item = item->next)
        if (scope || item->id == session)
        {
            if (!item->related)
            {
                if (!scope)
                    result = ERROR_SMG_PROCESS_NOT_PARENT;
                continue;
            }
            count++;
        }
    PID *targets = count ? malloc(count * sizeof *targets) : NULL;
    if (count && !targets)
        result = ERROR_NOT_ENOUGH_MEMORY;
    else
    {
        size_t next = 0;
        for (SessionWait *item = sessions; item; item = item->next)
            if (item->related && (scope || item->id == session))
                targets[next++] = item->pid;
        if (count)
            result = NO_ERROR;
    }
    lx_host_mutex_unlock(session_mutex);
    if (!result)
        for (size_t i = 0; i < count; i++)
        {
            APIRET stopped = DosKillProcess(0, targets[i]);
            if (stopped && stopped != ERROR_INVALID_PROCID && !result)
                result = stopped;
        }
    free(targets);
    return result;
}

APIRET16 Dos16StopSession(USHORT scope, USHORT session, ULONG reserved)
{
    return reserved ? ERROR_SMG_BAD_RESERVE : DosStopSession(scope, session);
}

APIRET16 Dos16SetSession(USHORT session, PSTATUSDATA status)
{
    return DosSetSession(session, status);
}

APIRET16 Dos16StartSession(ULONG pointer, PUSHORT session, PUSHORT pid)
{
    if (!session || !pid)
        return ERROR_INVALID_PARAMETER;
    LxGuestArgument header = {0}, input = {0}, text[5] = {{0}}, environment = {0}, object = {0};
    APIRET result = ERROR_INVALID_PARAMETER;
    if (!lx_guest_argument_far(GLoaderState.execution, pointer, 2, LX_GUEST_READ, 0, 0, &header))
        return result;
    USHORT length = lx_guest_load_u16(header.host);
    lx_guest_argument_finish(&header, 0);
    if (length < 24 || length > 60)
        return ERROR_SMG_INVALID_DATA_LENGTH;
    if (!lx_guest_argument_far(GLoaderState.execution, pointer, length, LX_GUEST_READ, 0, 0,
                               &input))
        return result;
    const uint8 *wire = input.host;
    STARTDATA data = {0};
    data.Length = length;
    data.Related = lx_guest_load_u16(wire + 2);
    data.FgBg = lx_guest_load_u16(wire + 4);
    data.TraceOpt = lx_guest_load_u16(wire + 6);
    const unsigned offsets[] = {8, 12, 16, 20, 32};
    for (unsigned i = 0; i < 5; i++)
    {
        ULONG address = offsets[i] + 4 <= length ? lx_guest_load_u32(wire + offsets[i]) : 0;
        if (!lx_guest_argument_strings(GLoaderState.execution, address, 1, 65536, 1, &text[i]))
            goto done;
    }
    data.PgmTitle = text[0].host;
    data.PgmName = text[1].host;
    data.PgmInputs = text[2].host;
    data.TermQ = text[3].host;
    data.IconFile = text[4].host;
    if (length >= 28 &&
        !lx_guest_argument_strings(GLoaderState.execution, lx_guest_load_u32(wire + 24), 0, 65536,
                                   1, &environment))
        goto done;
    data.Environment = environment.host;
    if (length >= 30)
        data.InheritOpt = lx_guest_load_u16(wire + 28);
    if (length >= 32)
        data.SessionType = lx_guest_load_u16(wire + 30);
    if (length >= 40)
        data.PgmHandle = lx_guest_load_u32(wire + 36);
    if (length >= 42)
        data.PgmControl = lx_guest_load_u16(wire + 40);
    if (length >= 44)
        data.InitXPos = lx_guest_load_u16(wire + 42);
    if (length >= 46)
        data.InitYPos = lx_guest_load_u16(wire + 44);
    if (length >= 48)
        data.InitXSize = lx_guest_load_u16(wire + 46);
    if (length >= 50)
        data.InitYSize = lx_guest_load_u16(wire + 48);
    if (length >= 52)
        data.Reserved = lx_guest_load_u16(wire + 50);
    if (length >= 60)
    {
        data.ObjectBuffLen = lx_guest_load_u32(wire + 56);
        if (!lx_guest_argument_far(GLoaderState.execution, lx_guest_load_u32(wire + 52),
                                   data.ObjectBuffLen, LX_GUEST_WRITE, 0, 1, &object))
            goto done;
        data.ObjectBuffer = object.host;
    }
    ULONG sid;
    PID process;
    result = DosStartSession(&data, &sid, &process);
    if (!result && data.Related == 1)
    {
        *session = (USHORT)sid;
        *pid = (USHORT)process;
    }
done:
    lx_guest_argument_finish(&object, 1);
    lx_guest_argument_finish(&environment, 0);
    for (unsigned i = 0; i < 5; i++)
        lx_guest_argument_finish(&text[i], 0);
    lx_guest_argument_finish(&input, 0);
    return result;
}
