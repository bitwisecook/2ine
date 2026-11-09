/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Sessions: DosStartSession and its 16-bit form, session titles and termination queues. */

#include "os2/baseos/doscalls/internal.h"
#include "emulator/launch_record.h"
#include "host/thread.h"
#include "host/sync.h"
#include "host/mailbox.h"
#include "host/process.h"
#include "host/library.h"
#include "os2/include/pmwin.h"

enum
{
    /* Strings are read from at most one 64 KB segment. */
    STARTDATA_STRING_LIMIT = 0x10000,
    /* Session IDs run from 1 to 0xFFFE. */
    SESSION_ID_COUNT = 0xFFFE,
    /* lx_host_mailbox_open's result for a name another session holds. */
    MAILBOX_EXISTS = 2
};

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
    /* Start at a pid-derived ID so that concurrent processes rarely collide. */
    for (ULONG offset = 0; offset < SESSION_ID_COUNT; offset++)
    {
        ULONG id = (process + offset) % SESSION_ID_COUNT + 1;
        char name[32];
        snprintf(name, sizeof name, "session-%04x", (unsigned)id);
        uint32 owner;
        int result =
            lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 1, 0, &session->identity, &owner);
        if (result == MAILBOX_EXISTS)
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

/* The desktop belongs to PMMERGE, which implements PMWIN: DOSCALLS does not
   link it, and finds it only when the process has loaded it. */
static void *desktop_entry(const char *name)
{
    for (LxModule *module = GProcessState.loaded_modules; module; module = module->next)
        if (module->nativelib && !strcasecmp(module->name, "PMMERGE"))
            return lx_host_library_symbol(module->nativelib, name);
    return NULL;
}

/* Session 0 is the caller's own session: bring its active window forward. */
static APIRET select_own_session(void)
{
    typedef HWND(OS2API * QueryActiveWindow)(HWND);
    typedef BOOL(OS2API * SetActiveWindow)(HWND, HWND);
    QueryActiveWindow query = (QueryActiveWindow)desktop_entry("WinQueryActiveWindow");
    SetActiveWindow activate = (SetActiveWindow)desktop_entry("WinSetActiveWindow");
    HWND active = query ? query(HWND_DESKTOP) : NULLHANDLE;
    return active && activate(HWND_DESKTOP, active) ? NO_ERROR : ERROR_SMG_NO_TARGET_WINDOW;
}

APIRET DosSelectSession(ULONG session)
{
    TRACE_NATIVE("DosSelectSession(%u)", (unsigned)session);
    if (!session)
        return select_own_session();
    lx_host_once(&session_once, initialize_sessions);
    APIRET result = ERROR_SMG_INVALID_SESSION_ID;
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    for (SessionWait *item = sessions; item; item = item->next)
        if (item->id == session)
        {
            /* Child foreground selection requires a target host window. */
            result = item->related ? ERROR_SMG_NO_TARGET_WINDOW : ERROR_SMG_PROCESS_NOT_PARENT;
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
    if (status->Length != sizeof(STATUSDATA))
        return ERROR_SMG_INVALID_DATA_LENGTH;
    if (status->SelectInd > SET_SESSION_NON_SELECTABLE)
        return ERROR_SMG_INVALID_SELECT_OPT;
    if (status->BondInd > SET_SESSION_NO_BOND)
        return ERROR_SMG_INVALID_BOND_OPTION;
    lx_host_once(&session_once, initialize_sessions);
    lx_host_mutex_lock(session_mutex, UINT32_MAX);
    APIRET result = ERROR_SMG_INVALID_SESSION_ID;
    for (SessionWait *item = sessions; item; item = item->next)
        if (item->id == session)
        {
            result = item->related ? NO_ERROR : ERROR_SMG_PROCESS_NOT_PARENT;
            if (!result)
            {
                if (status->SelectInd)
                    item->selectable = status->SelectInd == SET_SESSION_SELECTABLE;
                if (status->BondInd == SET_SESSION_BOND)
                    for (SessionWait *other = sessions; other; other = other->next)
                        other->bonded = other == item;
                else if (status->BondInd == SET_SESSION_NO_BOND)
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
    if (!data || data->Length < STARTDATA_MINIMUM_LENGTH || !data->PgmName || !*data->PgmName ||
        !session_id || !pid)
        return ERROR_INVALID_PARAMETER;
    if (data->Related > SSF_RELATED_CHILD)
        return ERROR_SMG_INVALID_RELATED_OPT;
    if (data->FgBg > SSF_FGBG_BACK)
        return ERROR_SMG_INVALID_START_MODE;
    if (data->TraceOpt > SSF_TRACEOPT_TRACEALL)
        return ERROR_SMG_INVALID_TRACE_OPTION;
    if (data->TraceOpt)
        return ERROR_NOT_SUPPORTED;
    if (data->InheritOpt > SSF_INHERTOPT_PARENT)
        return ERROR_SMG_INVALID_INHERIT_OPT;
    if (data->SessionType > SSF_TYPE_VDD)
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
    /* The program name and inputs, each NUL-terminated, then an empty string. */
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

/* The title the switch list shows for this session: SWCNTRL.szSwtitle holds
   MAXNAMEL (60) + 4 bytes. */
#define SESSION_TITLE_SIZE 64

static char session_title[SESSION_TITLE_SIZE];
static LxHostMutex *session_title_mutex;
static LxHostOnce session_title_once = LX_HOST_ONCE_INIT;

static void initialize_session_title(void)
{
    session_title_mutex = lx_host_mutex_create(0, 0);
    if (!session_title_mutex)
        abort();
}

APIRET16 Dos16SMSetTitle(PSZ title)
{
    TRACE_NATIVE("Dos16SMSetTitle(%p)", title);
    if (!title)
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&session_title_once, initialize_session_title);
    lx_host_mutex_lock(session_title_mutex, UINT32_MAX);
    snprintf(session_title, sizeof session_title, "%s", title);
    lx_host_mutex_unlock(session_title_mutex);
    return NO_ERROR;
}

void lx_session_title(char *output, size_t capacity)
{
    lx_host_once(&session_title_once, initialize_session_title);
    lx_host_mutex_lock(session_title_mutex, UINT32_MAX);
    snprintf(output, capacity, "%s", session_title);
    lx_host_mutex_unlock(session_title_mutex);
    LxLaunchRecord *record = lx_launch_record();
    if (!*output && record)
        snprintf(output, capacity, "%s", record->presentation.title);
}

APIRET16 Dos16SMPMPresent(PUSHORT present)
{
    TRACE_NATIVE("Dos16SMPMPresent(%p)", present);
    if (!present)
        return ERROR_INVALID_PARAMETER;
    *present = 0; /* No 16-bit PM desktop session is running in this worker. */
    return NO_ERROR;
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
    LxGuestArgument header = {0}, input = {0}, text[STARTDATA_STRING_COUNT] = {{0}},
                    environment = {0}, object = {0};
    APIRET result = ERROR_INVALID_PARAMETER;
    if (!lx_guest_argument_far(GProcessState.execution, pointer, sizeof(USHORT), LX_GUEST_READ, 0,
                               0, &header))
        return result;
    USHORT length = lx_guest_load_u16(header.host);
    lx_guest_argument_finish(&header, 0);
    if (length < STARTDATA_MINIMUM_LENGTH || length > STARTDATA_MAXIMUM_LENGTH)
        return ERROR_SMG_INVALID_DATA_LENGTH;
    if (!lx_guest_argument_far(GProcessState.execution, pointer, length, LX_GUEST_READ, 0, 0,
                               &input))
        return result;
    const uint8 *wire = input.host;
    STARTDATA data = {0};
    data.Length = length;
    data.Related = lx_guest_load_u16(wire + STARTDATA_RELATED_OFFSET);
    data.FgBg = lx_guest_load_u16(wire + STARTDATA_FGBG_OFFSET);
    data.TraceOpt = lx_guest_load_u16(wire + STARTDATA_TRACEOPT_OFFSET);
    const unsigned offsets[STARTDATA_STRING_COUNT] = {
        STARTDATA_TITLE_OFFSET, STARTDATA_NAME_OFFSET, STARTDATA_INPUTS_OFFSET,
        STARTDATA_TERMQ_OFFSET, STARTDATA_ICONFILE_OFFSET};
    for (unsigned i = 0; i < STARTDATA_STRING_COUNT; i++)
    {
        ULONG address =
            offsets[i] + sizeof(ULONG) <= length ? lx_guest_load_u32(wire + offsets[i]) : 0;
        if (!lx_guest_argument_strings(GProcessState.execution, address, 1, STARTDATA_STRING_LIMIT,
                                       1, &text[i]))
            goto done;
    }
    data.PgmTitle = text[0].host;
    data.PgmName = text[1].host;
    data.PgmInputs = text[2].host;
    data.TermQ = text[3].host;
    data.IconFile = text[4].host;
    if (length >= STARTDATA_INHERITOPT_OFFSET &&
        !lx_guest_argument_strings(GProcessState.execution,
                                   lx_guest_load_u32(wire + STARTDATA_ENVIRONMENT_OFFSET), 0,
                                   STARTDATA_STRING_LIMIT, 1, &environment))
        goto done;
    data.Environment = environment.host;
    /* Each later field is read only when Length covers it. */
    if (length >= STARTDATA_SESSIONTYPE_OFFSET)
        data.InheritOpt = lx_guest_load_u16(wire + STARTDATA_INHERITOPT_OFFSET);
    if (length >= STARTDATA_ICONFILE_OFFSET)
        data.SessionType = lx_guest_load_u16(wire + STARTDATA_SESSIONTYPE_OFFSET);
    if (length >= STARTDATA_PGMCONTROL_OFFSET)
        data.PgmHandle = lx_guest_load_u32(wire + STARTDATA_PGMHANDLE_OFFSET);
    if (length >= STARTDATA_INITXPOS_OFFSET)
        data.PgmControl = lx_guest_load_u16(wire + STARTDATA_PGMCONTROL_OFFSET);
    if (length >= STARTDATA_INITYPOS_OFFSET)
        data.InitXPos = lx_guest_load_u16(wire + STARTDATA_INITXPOS_OFFSET);
    if (length >= STARTDATA_INITXSIZE_OFFSET)
        data.InitYPos = lx_guest_load_u16(wire + STARTDATA_INITYPOS_OFFSET);
    if (length >= STARTDATA_INITYSIZE_OFFSET)
        data.InitXSize = lx_guest_load_u16(wire + STARTDATA_INITXSIZE_OFFSET);
    if (length >= STARTDATA_RESERVED_OFFSET)
        data.InitYSize = lx_guest_load_u16(wire + STARTDATA_INITYSIZE_OFFSET);
    if (length >= STARTDATA_OBJECTBUFFER_OFFSET)
        data.Reserved = lx_guest_load_u16(wire + STARTDATA_RESERVED_OFFSET);
    if (length >= STARTDATA_MAXIMUM_LENGTH)
    {
        data.ObjectBuffLen = lx_guest_load_u32(wire + STARTDATA_OBJECTBUFFLEN_OFFSET);
        if (!lx_guest_argument_far(GProcessState.execution,
                                   lx_guest_load_u32(wire + STARTDATA_OBJECTBUFFER_OFFSET),
                                   data.ObjectBuffLen, LX_GUEST_WRITE, 0, 1, &object))
            goto done;
        data.ObjectBuffer = object.host;
    }
    ULONG sid;
    PID process;
    result = DosStartSession(&data, &sid, &process);
    if (!result && data.Related == SSF_RELATED_CHILD)
    {
        *session = (USHORT)sid;
        *pid = (USHORT)process;
    }
done:
    lx_guest_argument_finish(&object, 1);
    lx_guest_argument_finish(&environment, 0);
    for (unsigned i = 0; i < STARTDATA_STRING_COUNT; i++)
        lx_guest_argument_finish(&text[i], 0);
    lx_guest_argument_finish(&input, 0);
    return result;
}
