/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child enumeration snapshots the descendant tree before guest callbacks run. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmp.h"

typedef struct WindowSnapshot
{
    ULONG *handles;
    size_t count, capacity;
} WindowSnapshot;

static BOOL collect(WindowSnapshot *snapshot, ULONG parent, unsigned depth)
{
    if (depth > 256)
        return FALSE;
    for (ULONG child = WinQueryWindow(open32ClientWindow(parent), 2); child;
         child = WinQueryWindow(child, 0))
    {
        if (open32FindWindow(child) || open32NativeDialogWindow(child))
        {
            if (snapshot->count == snapshot->capacity)
            {
                size_t capacity = snapshot->capacity ? snapshot->capacity * 2 : 32;
                if (capacity > 65536)
                    return FALSE;
                ULONG *next = realloc(snapshot->handles, capacity * sizeof *next);
                if (!next)
                    return FALSE;
                snapshot->handles = next;
                snapshot->capacity = capacity;
            }
            snapshot->handles[snapshot->count++] = child;
        }
        if (!collect(snapshot, child, depth + 1))
            return FALSE;
    }
    return TRUE;
}

BOOL EnumChildWindows(ULONG parent, ULONG procedure, ULONG parameter)
{
    TRACE_NATIVE("EnumChildWindows(%u, %X, %X)", parent, procedure, parameter);
    if (!parent)
        parent = HWND_DESKTOP;
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), parent) ||
        !open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return FALSE;
    }
    WindowSnapshot snapshot = {0};
    if (!collect(&snapshot, parent, 0))
    {
        free(snapshot.handles);
        open32SetError(8);
        return FALSE;
    }
    BOOL result = TRUE;
    for (size_t i = 0; i < snapshot.count; i++)
    {
        ULONG child = snapshot.handles[i];
        if (!open32FindWindow(child) && !open32NativeDialogWindow(child))
            continue;
        ULONG ancestor = WinQueryWindow(child, 5);
        while (ancestor && ancestor != parent)
            ancestor = WinQueryWindow(ancestor, 5);
        if (!ancestor)
            continue;
        if (!lx_execution_callback32_4(GProcessState.execution, procedure, child, parameter, 0, 0))
        {
            result = FALSE;
            break;
        }
    }
    free(snapshot.handles);
    return result;
}

ULONG GetWindowThreadProcessId(ULONG window, PVOID process)
{
    if (!open32FindWindow(window) || !WinIsWindow(open32Presentation(), window))
    {
        open32SetError(1400);
        return 0;
    }
    if (process)
        lx_guest_store_u32(process, GetCurrentProcessId());
    return GetCurrentThreadId();
}

ULONG GetTopWindow(ULONG parent)
{
    return open32PublicWindow(
        WinQueryWindow(parent ? open32ClientWindow(parent) : HWND_DESKTOP, 2));
}

ULONG GetNextWindow(ULONG window, ULONG direction)
{
    if (direction != 2 && direction != 3)
        return 0;
    return open32PublicWindow(
        WinQueryWindow(window ? window : HWND_DESKTOP, direction == 2 ? 0 : 1));
}

BOOL IsChild(ULONG parent, ULONG child)
{
    if (!parent)
        parent = HWND_DESKTOP;
    for (unsigned depth = 0; child && depth < 256; depth++)
    {
        child = WinQueryWindow(child, 5);
        if (child == open32ClientWindow(parent) || child == parent)
            return TRUE;
    }
    return FALSE;
}

ULONG GetLastActivePopup(ULONG window)
{
    ULONG target = window ? window : HWND_DESKTOP;
    if (target != HWND_DESKTOP && !WinIsWindow(0, target))
        return 0;
    if (open32PublicWindow(GetActiveWindow()) == target)
        return window;
    /* The uppermost of the window and its owned popups is the answer. */
    for (ULONG at = WinQueryWindow(HWND_DESKTOP, 2); at; at = WinQueryWindow(at, 0))
    {
        if (at == target)
            return at;
        if (((ULONG)GetWindowLong(at, -16) & 0x80000000u) &&
            open32PublicWindow(WinQueryWindow(at, 4)) == target)
            return at;
    }
    return window;
}

ULONG ChildWindowFromPoint(ULONG parent, LONG x, LONG y)
{
    RECTL bounds;
    ULONG native = open32ClientWindow(parent);
    if (!WinQueryWindowRect(native, &bounds))
    {
        open32SetError(1400);
        return 0;
    }
    POINTL point = {x, bounds.yTop - y};
    ULONG child = WinWindowFromPoint(native, &point, TRUE);
    return !child ? 0 : child == native ? parent : open32PublicWindow(child);
}

typedef struct WindowNames
{
    ULONG window, atom;
    char name[256], text[100];
} WindowNames;

/* Runs on the window's own thread, where its Open32 record and PM window can be read. */
static uintptr_t read_window_names(void *context)
{
    WindowNames *names = context;
    Open32Window *w = open32FindWindow(names->window);
    if (w)
    {
        names->atom = w->class->atom;
        GetClassName(names->window, names->name, sizeof names->name);
    }
    else
        WinQueryClassName(names->window, sizeof names->name, names->name);
    WinQueryWindowText(names->window, sizeof names->text, names->text);
    return 1;
}

ULONG FindWindow(PSZ class_name, PSZ title)
{
    if (!class_name)
    {
        open32SetError(87);
        return 0;
    }
    for (ULONG window = WinQueryWindow(HWND_DESKTOP, 2); window; window = WinQueryWindow(window, 0))
    {
        WindowNames names = {.window = window};
        uintptr_t ignored;
        if (!lx_pm_window_call(window, read_window_names, &names, sizeof names, &ignored))
            read_window_names(&names);
        if ((uintptr_t)class_name <= 0xFFFF ? names.atom != (ULONG)(uintptr_t)class_name
                                            : strcasecmp(names.name, class_name) != 0)
            continue;
        if (title && strcmp(names.text, title))
            continue;
        return open32PublicWindow(window);
    }
    return 0;
}

static BOOL enum_top(ULONG thread, ULONG procedure, ULONG parameter)
{
    if (!open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG *snapshot = NULL;
    size_t count = 0, capacity = 0;
    /* EnumWindows walks the desktop's children and then the object windows of every thread. */
    size_t objects = thread ? 0 : lx_pm_object_windows(NULL, 0);
    HWND *object_list = objects ? calloc(objects, sizeof *object_list) : NULL;
    objects = object_list ? lx_pm_object_windows(object_list, objects) : 0;
    ULONG window = WinQueryWindow(HWND_DESKTOP, 2);
    for (size_t next_object = 0; window || next_object < objects;)
    {
        BOOL from_object = !window;
        ULONG current = from_object ? object_list[next_object++] : window;
        window = from_object ? 0 : WinQueryWindow(window, 0);
        if (open32ThreadQueueWindow(current))
            continue;
        ULONG pid = 0, tid = 0;
        if (thread && (!WinQueryWindowProcess(current, &pid, &tid) || tid != thread))
            continue;
        if (count == capacity)
        {
            size_t next = capacity ? capacity * 2 : 32;
            ULONG *grown = realloc(snapshot, next * sizeof *grown);
            if (!grown)
            {
                free(snapshot);
                free(object_list);
                open32SetError(8);
                return FALSE;
            }
            snapshot = grown;
            capacity = next;
        }
        snapshot[count++] = open32PublicWindow(current);
    }
    free(object_list);
    BOOL result = TRUE;
    for (size_t i = 0; i < count; i++)
        if (WinIsWindow(0, snapshot[i]) &&
            !lx_execution_callback32_4(GProcessState.execution, procedure, snapshot[i], parameter,
                                       0, 0))
        {
            result = FALSE;
            break;
        }
    free(snapshot);
    return result;
}

BOOL EnumWindows(ULONG procedure, ULONG parameter)
{
    return enum_top(0, procedure, parameter);
}

BOOL EnumThreadWindows(ULONG thread, ULONG procedure, ULONG parameter)
{
    return enum_top(thread, procedure, parameter);
}
