/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinStartApp maps program details onto a session. The startup directory and
   environment travel with the launch; this process's own state never changes. */
#include "os2libs/os2native.h"
#include "os2libs/pmshapi.h"
#include "os2libs/sesmgr_start.h"
#include "guest/handles.h"

enum
{
    SAF_INSTALLEDCMDLINE = 0x01,
    SAF_STARTCHILDAPP = 0x02,
    SAF_MAXIMIZED = 0x04,
    SAF_MINIMIZED = 0x08,
    SAF_BACKGROUND = 0x10,
    SAF_VALIDFLAGS = 0x1f,
    SHE_INVISIBLE = 0x01,
    SWP_HIDE_FLAG = 0x0010,
    SWP_MINIMIZE_FLAG = 0x0400,
    SWP_MAXIMIZE_FLAG = 0x0800,
    SWP_NOAUTOCLOSE_FLAG = 0x8000,
    SSF_CONTROL_INVISIBLE = 0x0001,
    SSF_CONTROL_MAXIMIZE = 0x0002,
    SSF_CONTROL_MINIMIZE = 0x0004,
    SSF_CONTROL_NOAUTOCLOSE = 0x0008,
    SSF_CONTROL_SETPOS = 0x8000,
    SWP_MOVE_FLAG = 0x0002,
    PROG_VDM_TYPE = 4,
    PROG_WINDOWEDVDM_TYPE = 7,
    PROG_WINDOW_FIRST = 10,
    PROG_WINDOW_LAST = 20,
    PMERR_INVALID_WINDOW_ID = 0x1206,
    PMERR_INVALID_PARAMETERS_ID = 0x1208,
    PMERR_INVALID_APPL_ID = 0x1530,
    PMERR_DOS_ERROR_ID = 0x1200,
    PMERR_MEMORY_ID = 0x1112,
    SEVERITY_ERROR_ID = 8
};
typedef struct App
{
    HWND notify;
    ULONG handle;
} App;
static void app_ended(void *context, ULONG result)
{
    App *app = context;
    if (app->notify)
        WinPostMsg(app->notify, WM_APPTERMINATENOTIFY, (MPARAM)(uintptr_t)app->handle,
                   (MPARAM)(uintptr_t)result);
    lx_handle_close(app->handle, LX_HANDLE_PM_APP);
}
/* An installed command line holding an unquoted prompt marker is not usable as is. */
static PSZ installed_parameters(PSZ parameters)
{
    BOOL quoted = FALSE;
    for (const char *p = parameters; p && *p; p++)
        if (*p == '"')
            quoted = !quoted;
        else if (!quoted && (*p == '%' || *p == '?'))
            return "";
    return parameters;
}
/* Windows program categories run under WINOS2.COM with a mode switch. */
static const char *windows_switch(ULONG category)
{
    if (category == 10)
        return "/r ";
    if (category == 11 || category == 20 || (category >= 13 && category <= 16))
        return "/s ";
    if (category >= 17 && category <= 19)
        return "/3 ";
    return category == 12 ? " " : NULL;
}
static USHORT session_control(const PROGDETAILS *details, ULONG options)
{
    ULONG flags = details->swpInitial.fl;
    USHORT control = 0;
    if ((flags & SWP_HIDE_FLAG) || (details->fbVisible & SHE_INVISIBLE))
        control |= SSF_CONTROL_INVISIBLE;
    if (flags & SWP_MINIMIZE_FLAG)
        control |= SSF_CONTROL_MINIMIZE;
    if (flags & SWP_MAXIMIZE_FLAG)
        control |= SSF_CONTROL_MAXIMIZE;
    if (flags & SWP_NOAUTOCLOSE_FLAG)
        control |= SSF_CONTROL_NOAUTOCLOSE;
    if (flags & SWP_MOVE_FLAG)
        control |= SSF_CONTROL_SETPOS;
    if (options & SAF_MAXIMIZED)
        control |= SSF_CONTROL_MAXIMIZE;
    else if (options & SAF_MINIMIZED)
        control = (control & ~SSF_CONTROL_MAXIMIZE) | SSF_CONTROL_MINIMIZE;
    return control;
}
static ULONG dos_error(ULONG error)
{
    switch (error)
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_EXE_SIGNATURE:
        case ERROR_EXE_MARKED_INVALID:
        case ERROR_BAD_EXE_FORMAT:
            return PMERR_INVALID_APPL_ID;
        default:
            return PMERR_DOS_ERROR_ID;
    }
}
ULONG lx_pmshapi_start_app(HWND notify, PPROGDETAILS details, PSZ parameters, ULONG options,
                           ULONG *error)
{
    *error = 0;
    if (!details || (options & ~(ULONG)SAF_VALIDFLAGS))
    {
        *error = PMERR_INVALID_PARAMETERS_ID;
        return 0;
    }
    if (notify && !WinIsWindow(0, notify))
    {
        *error = PMERR_INVALID_WINDOW_ID;
        return 0;
    }
    STARTDATA data = {0};
    data.Length = 60;
    data.Related = options & SAF_STARTCHILDAPP ? 1 : 0;
    data.FgBg = (options & SAF_BACKGROUND) || (details->swpInitial.fl & SWP_MINIMIZE_FLAG) ? 1 : 0;
    data.PgmTitle = details->pszTitle;
    data.PgmName = details->pszExecutable;
    data.InheritOpt = 1;
    data.PgmControl = session_control(details, options);
    data.InitXPos = (USHORT)details->swpInitial.x;
    data.InitYPos = (USHORT)details->swpInitial.y;
    data.InitXSize = (USHORT)details->swpInitial.cx;
    data.InitYSize = (USHORT)details->swpInitial.cy;
    if (details->pszEnvironment && *details->pszEnvironment)
        data.Environment = (PBYTE)details->pszEnvironment;
    PSZ inputs = parameters;
    char *windows_inputs = NULL;
    ULONG category = details->progc;
    if (options & SAF_INSTALLEDCMDLINE)
    {
        inputs = installed_parameters(details->pszParameters);
        const char *mode = details->pszExecutable ? windows_switch(category) : NULL;
        if (mode)
        {
            const char *tail = inputs ? inputs : "";
            size_t size = strlen(mode) + strlen(details->pszExecutable) + strlen(tail) + 2;
            if (!(windows_inputs = malloc(size)))
            {
                *error = PMERR_MEMORY_ID;
                return 0;
            }
            snprintf(windows_inputs, size, "%s%s %s", mode, details->pszExecutable, tail);
            inputs = windows_inputs;
            data.PgmName = "WINOS2.COM";
            category = PROG_VDM_TYPE;
        }
    }
    if (category >= PROG_WINDOW_FIRST && category <= PROG_WINDOW_LAST)
        category = PROG_VDM_TYPE;
    if (category > PROG_VDM_TYPE && category != PROG_WINDOWEDVDM_TYPE)
    {
        free(windows_inputs);
        *error = PMERR_INVALID_APPL_ID;
        return 0;
    }
    data.SessionType = (USHORT)category;
    data.PgmInputs = (PBYTE)inputs;
    App *app = calloc(1, sizeof *app);
    if (app)
    {
        app->notify = notify;
        app->handle = lx_handle_create(LX_HANDLE_PM_APP, app, free);
    }
    if (!app || !app->handle)
    {
        free(app);
        free(windows_inputs);
        *error = PMERR_MEMORY_ID;
        return 0;
    }
    /* The wait thread may close the handle as soon as the session starts. */
    ULONG handle = app->handle;
    PSZ directory =
        details->pszStartupDir && *details->pszStartupDir ? details->pszStartupDir : NULL;
    LxSessionStart start = {
        .data = &data, .directory = directory, .ended = app_ended, .context = app};
    ULONG session;
    PID pid;
    APIRET result = lx_session_start(&start, &session, &pid);
    /* Only the program-type check yields an invalid application; WINOS2.COM failing
       to start is a session failure. */
    BOOL windows_session = windows_inputs != NULL;
    free(windows_inputs);
    if (result)
    {
        lx_handle_close(handle, LX_HANDLE_PM_APP);
        *error = windows_session ? PMERR_DOS_ERROR_ID : dos_error(result);
        return 0;
    }
    return handle;
}
ULONG WinStartApp(HWND notify, PPROGDETAILS details, PSZ parameters, PVOID reserved, ULONG options)
{
    TRACE_NATIVE("WinStartApp(%u, %p, '%s', %p, %u)", (unsigned)notify, details, parameters,
                 reserved, (unsigned)options);
    ULONG error = reserved ? PMERR_INVALID_PARAMETERS_ID : 0;
    ULONG handle = error ? 0 : lx_pmshapi_start_app(notify, details, parameters, options, &error);
    if (error)
        WinSetErrorInfo(((ULONG)SEVERITY_ERROR_ID << 16) | error, 0);
    return handle;
}
