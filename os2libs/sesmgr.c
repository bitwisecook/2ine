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
#include "os2libs/pmwin.h"

#include "os2libs/generated/sesmgr-lx.h"

typedef struct SessionWait
{
    struct SessionWait *next;
    PID pid;
    HQUEUE queue;
    BOOL related;
    BOOL selectable, bonded;
} SessionWait;
static SessionWait *sessions;
static LxHostMutex *session_mutex;
static LxHostOnce session_once = LX_HOST_ONCE_INIT;
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
        if (item->pid == session)
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
        if (item->pid == session)
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
        USHORT message[2] = {(USHORT)session->pid, (USHORT)result.codeResult};
        DosWriteQueue(session->queue, 0, sizeof message, message, 0);
    }
    if (session->queue)
        DosCloseQueue(session->queue);
    remove_session(session);
    free(session);
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
    if (data->TermQ && *data->TermQ)
    {
        PID owner;
        APIRET result = DosOpenQueue(&owner, &session->queue, (PSZ)data->TermQ);
        if (result)
        {
            free(session);
            return result;
        }
    }
    const char *inputs = data->PgmInputs ? (const char *)data->PgmInputs : "";
    size_t name_size = strlen(data->PgmName) + 1;
    char *command = malloc(name_size + strlen(inputs) + 2);
    if (!command)
    {
        if (session->queue)
            DosCloseQueue(session->queue);
        free(session);
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
        if (session->queue)
            DosCloseQueue(session->queue);
        free(session);
        return error;
    }
    session->pid = result.codeTerminate;
    session->related = data->Related == 1;
    session->selectable = TRUE;
    *session_id = session->pid;
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
        if (session->queue)
            DosCloseQueue(session->queue);
        free(session);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return NO_ERROR;
}

APIRET16 Dos16SMSetTitle(PCHAR title)
{
    TRACE_NATIVE("Dos16SMSetTitle(%p)", title);
    return NO_ERROR;
}
