/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "sesmgr.h"
#include "doscalls.h"
#include "quecalls.h"
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
} SessionWait;
static SessionWait *sessions;
static LxHostMutex *session_mutex;
static LxHostOnce session_once = LX_HOST_ONCE_INIT;
static void destroy_session(SessionWait *session)
{
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
        DosWriteQueue(session->queue, 0, sizeof message, message, 0);
    }
    remove_session(session);
    destroy_session(session);
    return NULL;
}
APIRET DosStartSession(PSTARTDATA data, PULONG session_id, PPID pid)
{
    TRACE_NATIVE("DosStartSession(%p, %p, %p)", data, session_id, pid);
    if (!data || data->Length < 24 || !data->PgmName || !*data->PgmName || !session_id || !pid)
        return ERROR_INVALID_PARAMETER;
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
        APIRET result = DosOpenQueue(&owner, &session->queue, (PSZ)data->TermQ);
        if (result)
        {
            destroy_session(session);
            return result;
        }
    }
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
    RESULTCODES result = {0};
    APIRET error = DosExecPgm(NULL, 0, EXEC_ASYNCRESULT, command, (PSZ)data->Environment, &result,
                              data->PgmName);
    free(command);
    if (error)
    {
        destroy_session(session);
        return error;
    }
    session->pid = result.codeTerminate;
    session->related = data->Related == 1;
    session->selectable = TRUE;
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

APIRET16 Dos16SMSetTitle(PCHAR title)
{
    TRACE_NATIVE("Dos16SMSetTitle(%p)", title);
    return NO_ERROR;
}
