/* Public domain. No rights reserved. Wholly AI-authored. */
/* Opening objects: folder views, programs and associated programs chosen by
   their ASSOCFILTER. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include "os2/wpsh/pmwp/internal.h"
#include "os2/include/bsedos.h"
#include "emulator/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include <ctype.h>
#include <strings.h>

static MRESULT OS2API folder_view(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_SIZE)
    {
        ULONG size = (ULONG)(uintptr_t)second;
        HWND list = WinWindowFromID(window, 1);
        if (list)
            WinSetWindowPos(list, HWND_TOP, 0, 0, (USHORT)size, (USHORT)(size >> 16),
                            SWP_MOVE | SWP_SIZE);
        return 0;
    }
    if (message == WM_CONTROL && ((ULONG)(uintptr_t)first >> 16) == 5)
    {
        HWND list = WinWindowFromID(window, 1);
        LONG selected = (SHORT)(uintptr_t)WinSendMsg(list, 0x165, 0, 0);
        if (selected >= 0)
        {
            ULONG object =
                (ULONG)(uintptr_t)WinSendMsg(list, 0x16a, (MPARAM)(uintptr_t)selected, 0);
            if (object)
                WinOpenObject(object, 0, TRUE);
        }
        return 0;
    }
    if (message == WM_CLOSE)
    {
        HWND frame = WinQueryWindow(window, 5);
        return (MRESULT)(uintptr_t)WinDestroyWindow(frame ? frame : window);
    }
    return WinDefWindowProc(window, message, first, second);
}

BOOL pmwp_open_object(ULONG handle, ULONG view, BOOL activate, unsigned depth, HWND *opened)
{
    if (depth > 64)
        return FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    PmwpRecord object = {0};
    BOOL valid = profile && (handle == 1 || store_load(profile, handle, &object));
    if (!valid)
    {
        store_end(profile, lock);
        return FALSE;
    }
    if (handle == 1)
    {
        object.class_name = "WPFolder";
        object.title = "Desktop";
        object.location = "";
        object.setup = "";
    }
    if (!strcmp(object.class_name, "WPShadow"))
    {
        char id[1024];
        ULONG target = store_property(object.setup, "SHADOWID", id, sizeof id)
                           ? store_find(profile, id, NULL, NULL)
                           : 0;
        free(object.data);
        store_end(profile, lock);
        return target && pmwp_open_object(target, view, activate, depth + 1, opened);
    }
    /* A view opened by a class method carries `opened`; its subclass record opens as
       the base class it extends. Any other class opens through its SOM methods. */
    char executable[4096], parameters[32768];
    BOOL has_program = store_property(object.setup, "EXENAME", executable, sizeof executable);
    BOOL folder = !strcmp(object.class_name, "WPFolder") || (opened && !has_program);
    BOOL program = !strcmp(object.class_name, "WPProgram") || (opened && has_program);
    if (!folder && !program)
    {
        free(object.data);
        store_end(profile, lock);
        return pmwpViewStored(handle, view);
    }
    BOOL result = FALSE;
    if (folder)
    {
        char folder_id[1024];
        if (!store_ensure_identifier(profile, handle, folder_id, sizeof folder_id))
            goto done;

        typedef struct Child
        {
            ULONG handle;
            char *title;
        } Child;

        Child *children = NULL;
        size_t count = 0;
        ULONG size;
        char *list = store_keys(profile, &size);
        for (char *p = list; p && p < list + size && *p; p += strlen(p) + 1)
        {
            ULONG child = (ULONG)strtoul(p, NULL, 16);
            PmwpRecord entry = {0};
            if (store_load(profile, child, &entry) && !strcmp(entry.location, folder_id))
            {
                Child *next = realloc(children, (count + 1) * sizeof *children);
                if (next)
                {
                    children = next;
                    children[count].handle = child;
                    children[count].title = strdup(entry.title);
                    if (children[count].title)
                        count++;
                }
            }
            free(entry.data);
        }
        free(list);
        char *title = strdup(object.title);
        free(object.data);
        object.data = NULL;
        store_end(profile, lock);
        HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
        WinRegisterClass(hab, "2ine.WorkplaceFolder", folder_view, 0, 4);

        {
            ULONG flags = 0x3b;
            HWND client = 0,
                 frame =
                     WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "2ine.WorkplaceFolder",
                                        title ? title : "Folder", WS_VISIBLE, 0, 0, &client);
            if (frame)
            {
                HWND child =
                    WinCreateWindow(client, (PSZ)(uintptr_t)WC_LISTBOX, "", WS_VISIBLE | 0x20000, 0,
                                    0, 480, 320, client, HWND_TOP, 1, NULL, NULL);
                if (child)
                {
                    for (size_t i = 0; i < count; i++)
                    {
                        LONG at = (LONG)(intptr_t)WinSendMsg(child, 0x161, (MPARAM)(intptr_t)-1,
                                                             children[i].title);
                        if (at >= 0)
                            WinSendMsg(child, 0x169, (MPARAM)(uintptr_t)at,
                                       (MPARAM)(uintptr_t)children[i].handle);
                    }
                    WinSetWindowPos(frame, HWND_TOP, 40, 40, 520, 380,
                                    SWP_MOVE | SWP_SIZE | SWP_SHOW | (activate ? SWP_ACTIVATE : 0));
                    if (opened)
                        *opened = frame;
                    result = TRUE;
                }
                else
                    WinDestroyWindow(frame);
            }
        }
        for (size_t i = 0; i < count; i++)
            free(children[i].title);
        free(children);
        free(title);
        return result;
    }
    if (program)
    {
        if (has_program)
        {
            parameters[0] = 0;
            store_property(object.setup, "PARAMETERS", parameters, sizeof parameters);
            STARTDATA start = {0};
            start.Length = sizeof start;
            start.PgmName = executable;
            start.PgmTitle = object.title;
            start.PgmInputs = (PBYTE)parameters;
            start.FgBg = activate ? 0 : 1;
            start.SessionType = 0;
            ULONG session;
            PID pid;
            result = DosStartSession(&start, &session, &pid) == 0;
        }
    }
done:
    free(object.data);
    store_end(profile, lock);
    return result;
}

BOOL WinOpenObject(ULONG handle, ULONG view, BOOL activate)
{
    return pmwp_open_object(handle, view, activate, 0, NULL);
}

/* '*' and '?' are the only wildcards in an association filter. */
static BOOL filter_matches(const char *filter, const char *name)
{
    for (; *filter; filter++, name++)
    {
        if (*filter == '*')
        {
            while (*filter == '*')
                filter++;
            if (!*filter)
                return TRUE;
            for (; *name; name++)
                if (filter_matches(filter, name))
                    return TRUE;
            return FALSE;
        }
        if (!*name ||
            (*filter != '?' && toupper((unsigned char)*filter) != toupper((unsigned char)*name)))
            return FALSE;
    }
    return !*name;
}

static BOOL filter_list_matches(char *filters, const char *name)
{
    char *state = NULL;
    for (char *item = strtok_r(filters, ",", &state); item; item = strtok_r(NULL, ",", &state))
        if (filter_matches(item, name))
            return TRUE;
    return FALSE;
}

BOOL lx_pmwp_open_associated(PSZ path)
{
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            name = p + 1;
    char executable[4096], parameters[32768], filters[1024];
    BOOL found = FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG size = 0;
    char *list = profile ? store_keys(profile, &size) : NULL;
    for (char *key = list; key && key < list + size && *key && !found; key += strlen(key) + 1)
    {
        PmwpRecord object = {0};
        if (!store_load(profile, (ULONG)strtoul(key, NULL, 16), &object))
            continue;
        if (!strcmp(object.class_name, "WPProgram") &&
            store_property(object.setup, "ASSOCFILTER", filters, sizeof filters) &&
            store_property(object.setup, "EXENAME", executable, sizeof executable) &&
            filter_list_matches(filters, name))
        {
            parameters[0] = 0;
            store_property(object.setup, "PARAMETERS", parameters, sizeof parameters);
            found = TRUE;
        }
        free(object.data);
    }
    free(list);
    store_end(profile, lock);
    if (!found)
        return FALSE;
    size_t bytes = strlen(parameters) + strlen(path) + 4;
    char *inputs = malloc(bytes);
    if (!inputs)
        return FALSE;
    snprintf(inputs, bytes, "%s%s\"%s\"", parameters, *parameters ? " " : "", path);
    STARTDATA start = {0};
    start.Length = sizeof start;
    start.PgmName = executable;
    start.PgmInputs = (PBYTE)inputs;
    ULONG session;
    PID pid;
    BOOL result = DosStartSession(&start, &session, &pid) == 0;
    free(inputs);
    return result;
}
