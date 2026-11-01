/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 custom windows translate their lifecycle callbacks through PM. */
#include "os2libs/pmwinx/classes.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include <stdio.h>
typedef struct Open32Window
{
    HWND handle;
    Open32Class *class;
    ULONG style, extended_style, parameter, instance, menu, parent, procedure, user_data;
    uint8 *extra;
    LONG x, y, width, height;
    char *title;
    LxGuestMapping *creation;
    int destroyed;
    struct Open32Window *next;
} Open32Window;
static _Thread_local Open32Window *windows, *creating;
static Open32Window *find_window(HWND handle)
{
    for (Open32Window *w = windows; w; w = w->next)
        if (w->handle == handle)
            return w;
    if (creating && creating->handle == handle)
        return creating;
    return NULL;
}
static ULONG callback(Open32Window *w, ULONG message, ULONG first, ULONG second)
{
    return lx_execution_callback32_4(GLoaderState.execution, w->procedure, w->handle, message,
                                     first, second);
}
static MRESULT OS2API window_procedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    if (creating && !creating->handle)
        creating->handle = handle;
    Open32Window *w = find_window(handle);
    if (!w)
        return WinDefWindowProc(handle, message, first, second);
    switch (message)
    {
        case WM_CREATE:
        {
            ULONG record = lx_execution_mapping_address(w->creation);
            if (!callback(w, 0x81, 0, record))
                return (MRESULT)(uintptr_t)1;
            return (MRESULT)(uintptr_t)(callback(w, 1, 0, record) == UINT32_MAX);
        }
        case WM_DESTROY:
            w->destroyed = 1;
            callback(w, 2, 0, 0);
            callback(w, 0x82, 0, 0);
            w->destroyed = 1;
            return NULL;
        case WM_SIZE:
            return (MRESULT)(uintptr_t)callback(w, 5, 0, (ULONG)(uintptr_t)second);
        case WM_PAINT:
            return (MRESULT)(uintptr_t)callback(w, 15, 0, 0);
        case WM_CLOSE:
            return (MRESULT)(uintptr_t)callback(w, 16, 0, 0);
        case WM_SHOW:
            return (MRESULT)(uintptr_t)callback(w, 24, (ULONG)(uintptr_t)first, 0);
        case WM_ENABLE:
            return (MRESULT)(uintptr_t)callback(w, 10, (ULONG)(uintptr_t)first, 0);
        case WM_SETFOCUS:
            return (MRESULT)(uintptr_t)callback(w, second ? 7 : 8, (ULONG)(uintptr_t)first, 0);
        case WM_TIMER:
            return (MRESULT)(uintptr_t)callback(w, 0x113, (ULONG)(uintptr_t)first, 0);
        default:
            if (message >= WM_USER)
                return (MRESULT)(uintptr_t)callback(w, message - WM_USER + 0x400,
                                                    (ULONG)(uintptr_t)first,
                                                    (ULONG)(uintptr_t)second);
            return WinDefWindowProc(handle, message, first, second);
    }
}
static void discard_window(Open32Window *w)
{
    if (w->creation)
        lx_execution_unmap(GLoaderState.execution, w->creation);
    open32ClassRelease(w->class);
    free(w->extra);
    free(w->title);
    free(w);
}
ULONG CreateWindowEx(ULONG extended_style, PSZ class_name, PSZ title, ULONG style, LONG x, LONG y,
                     LONG width, LONG height, ULONG parent, ULONG menu, ULONG instance,
                     ULONG parameter)
{
    TRACE_NATIVE("CreateWindowEx(%X, %p, '%s', %X, %d, %d, %d, %d, %u, %u, %u, %X)", extended_style,
                 class_name, title, style, x, y, width, height, parent, menu, instance, parameter);
    Open32Class *class = open32ClassAcquire(instance, class_name);
    if (!class)
    {
        open32SetError(1407);
        return 0;
    }
    HAB hab = open32Presentation();
    if (!hab)
    {
        open32ClassRelease(class);
        open32SetError(8);
        return 0;
    }
    char pm_class[32];
    snprintf(pm_class, sizeof pm_class, "Open32_%u", class->atom);
    CLASSINFO info;
    if (!WinQueryClassInfo(hab, pm_class, &info) &&
        !WinRegisterClass(hab, pm_class, window_procedure, CS_SIZEREDRAW, 0))
    {
        open32ClassRelease(class);
        open32SetError(8);
        return 0;
    }
    Open32Window *w = calloc(1, sizeof *w);
    if (!w)
    {
        open32ClassRelease(class);
        open32SetError(8);
        return 0;
    }
    w->class = class;
    w->procedure = class->procedure;
    w->extra = class->window_extra ? calloc(1, class->window_extra) : NULL;
    w->title = strdup(title ? title : "");
    w->style = style;
    w->extended_style = extended_style;
    w->x = x == INT32_MIN ? 20 : x;
    w->y = y == INT32_MIN ? 20 : y;
    w->width = width == INT32_MIN ? 640 : width;
    w->height = height == INT32_MIN ? 480 : height;
    w->instance = instance;
    w->parent = parent;
    w->menu = menu;
    w->parameter = parameter;
    size_t title_bytes = strlen(title ? title : "") + 1;
    if (w->width < 0 || w->height < 0 || title_bytes > 65536)
    {
        discard_window(w);
        open32SetError(87);
        return 0;
    }
    w->creation = lx_execution_map(GLoaderState.execution, 0,
                                   (title_bytes + 48 + 4095) & ~(size_t)4095, 3, 0);
    if (!w->title || !w->creation || (class->window_extra && !w->extra))
    {
        discard_window(w);
        open32SetError(8);
        return 0;
    }
    uint32 creation = lx_execution_mapping_address(w->creation);
    uint8 *record = lx_execution_mapping_host(w->creation);
    uint32 fields[] = {parameter,
                       instance,
                       menu,
                       parent,
                       (uint32)w->height,
                       (uint32)w->width,
                       (uint32)w->y,
                       (uint32)w->x,
                       style,
                       creation + 48,
                       lx_execution_mapping_address(class->names),
                       extended_style};
    for (unsigned i = 0; i < 12; i++)
        lx_guest_store_u32(record + 4 * i, fields[i]);
    memcpy(record + 48, w->title, title_bytes);
    Open32Window *previous = creating;
    creating = w;
    RECTL bounds;
    HWND pm_parent = parent ? parent : HWND_DESKTOP;
    if (!WinQueryWindowRect(pm_parent, &bounds))
    {
        creating = previous;
        discard_window(w);
        open32SetError(1400);
        return 0;
    }
    ULONG pm_style =
        ((style & 0x10000000) ? WS_VISIBLE : 0) | ((style & 0x08000000) ? WS_DISABLED : 0);
    HWND handle = WinCreateWindow(
        pm_parent, pm_class, w->title, pm_style, w->x, bounds.yTop - w->y - w->height, w->width,
        w->height, parent ? parent : HWND_DESKTOP, HWND_TOP, parent ? menu : 0, NULL, NULL);
    creating = previous;
    lx_execution_unmap(GLoaderState.execution, w->creation);
    w->creation = NULL;
    if (!handle)
    {
        discard_window(w);
        open32SetError(1407);
        return 0;
    }
    w->handle = handle;
    w->next = windows;
    windows = w;
    return handle;
}
BOOL open32EraseBackground(ULONG handle, ULONG dc)
{
    Open32Window *w = find_window(handle);
    return w && callback(w, 14, dc, 0) != 0;
}
LONG DefWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    if (message == 0x81)
        return 1;
    if (message == 1 || message == 0x82)
        return 0;
    if (message == 16)
        return DestroyWindow(handle) ? 0 : -1;
    if (message == 14)
    {
        Open32Window *w = find_window(handle);
        RECTL rect;
        if (!w || !w->class->brush || !WinQueryWindowRect(handle, &rect))
            return 0;
        uint8 wire[16] = {0};
        lx_guest_store_u32(wire + 8, (ULONG)rect.xRight);
        lx_guest_store_u32(wire + 12, (ULONG)rect.yTop);
        return FillRect(first, wire, w->class->brush);
    }
    if (message == 15)
    {
        uint8 paint[64];
        if (BeginPaint(handle, paint))
            EndPaint(handle, paint);
        return 0;
    }
    ULONG pm_message;
    switch (message)
    {
        case 2:
            pm_message = WM_DESTROY;
            break;
        case 5:
            pm_message = WM_SIZE;
            break;
        case 15:
            pm_message = WM_PAINT;
            break;
        case 24:
            pm_message = WM_SHOW;
            break;
        default:
            return 0;
    }
    return (LONG)(uintptr_t)WinDefWindowProc(handle, pm_message, (MPARAM)(uintptr_t)first,
                                             (MPARAM)(uintptr_t)second);
}
BOOL DestroyWindow(ULONG handle)
{
    Open32Window **link = &windows;
    while (*link && (*link)->handle != handle)
        link = &(*link)->next;
    if (!*link || (*link)->destroyed)
    {
        open32SetError(1400);
        return FALSE;
    }
    Open32Window *w = *link;
    if (!WinDestroyWindow(handle))
        return FALSE;
    link = &windows;
    while (*link && *link != w)
        link = &(*link)->next;
    if (*link)
        *link = w->next;
    discard_window(w);
    return TRUE;
}

static ULONG *window_long(Open32Window *w, LONG index)
{
    switch (index)
    {
        case -4:
            return &w->procedure;
        case -6:
            return &w->instance;
        case -8:
            return &w->parent;
        case -12:
            return &w->menu;
        case -16:
            return &w->style;
        case -20:
            return &w->extended_style;
        case -21:
            return &w->user_data;
        default:
            return NULL;
    }
}
LONG GetWindowLong(ULONG handle, LONG index)
{
    Open32Window *w = find_window(handle);
    if (!w)
    {
        open32SetError(1400);
        return 0;
    }
    if (index >= 0 && (ULONG)index <= w->class->window_extra &&
        w->class->window_extra - (ULONG)index >= 4)
        return (LONG)lx_guest_load_u32(w->extra + index);
    ULONG *value = window_long(w, index);
    if (!value)
    {
        open32SetError(1413);
        return 0;
    }
    return (LONG)*value;
}
LONG SetWindowLong(ULONG handle, LONG index, LONG value)
{
    Open32Window *w = find_window(handle);
    if (!w)
    {
        open32SetError(1400);
        return 0;
    }
    if (index == -4 &&
        !lx_execution_memory(GLoaderState.execution, (ULONG)value, 1, LX_GUEST_EXECUTE))
    {
        open32SetError(87);
        return 0;
    }
    if (index >= 0 && (ULONG)index <= w->class->window_extra &&
        w->class->window_extra - (ULONG)index >= 4)
    {
        LONG old = (LONG)lx_guest_load_u32(w->extra + index);
        lx_guest_store_u32(w->extra + index, (ULONG)value);
        return old;
    }
    ULONG *field = window_long(w, index);
    if (!field)
    {
        open32SetError(1413);
        return 0;
    }
    ULONG old = *field;
    *field = (ULONG)value;
    return (LONG)old;
}

BOOL SetWindowPos(ULONG handle, ULONG after, LONG x, LONG y, LONG width, LONG height, ULONG flags)
{
    Open32Window *w = find_window(handle);
    if (!w || ((flags & 1) == 0 && (width < 0 || height < 0)))
    {
        open32SetError(w ? 87 : 1400);
        return FALSE;
    }
    LONG new_width = flags & 1 ? w->width : width;
    LONG new_height = flags & 1 ? w->height : height;
    LONG new_x = flags & 2 ? w->x : x;
    LONG new_y = flags & 2 ? w->y : y;
    RECTL bounds;
    if (!WinQueryWindowRect(w->parent ? w->parent : HWND_DESKTOP, &bounds))
        return FALSE;
    ULONG pm_flags = (flags & 1 ? 0 : SWP_SIZE) | (flags & 2 ? 0 : SWP_MOVE) |
                     (flags & 4 ? 0 : SWP_ZORDER) | (flags & 8 ? SWP_NOREDRAW : 0) |
                     (flags & 16 ? 0 : SWP_ACTIVATE) | (flags & 64 ? SWP_SHOW : 0) |
                     (flags & 128 ? SWP_HIDE : 0);
    HWND pm_after = after == 0 || after >= 0xFFFFFFFE ? HWND_TOP : after == 1 ? HWND_BOTTOM : after;
    BOOL result = WinSetWindowPos(handle, pm_after, new_x, bounds.yTop - new_y - new_height,
                                  new_width, new_height, pm_flags);
    if (result)
    {
        w->width = new_width;
        w->height = new_height;
        w->x = new_x;
        w->y = new_y;
        if (flags & 64)
            w->style |= 0x10000000;
        if (flags & 128)
            w->style &= ~0x10000000u;
    }
    return result;
}

BOOL MoveWindow(ULONG handle, LONG x, LONG y, LONG width, LONG height, BOOL repaint)
{
    BOOL result = SetWindowPos(handle, 0, x, y, width, height, 4 | 16 | (repaint ? 0 : 8));
    if (result && repaint)
        WinUpdateWindow(handle);
    return result;
}
BOOL ShowWindow(ULONG handle, LONG command)
{
    Open32Window *w = find_window(handle);
    if (!w)
    {
        open32SetError(1400);
        return FALSE;
    }
    BOOL previous = (w->style & 0x10000000) != 0;
    ULONG flags = command ? SWP_SHOW : SWP_HIDE;
    if (command == 2 || command == 6 || command == 7)
        flags |= SWP_MINIMIZE;
    else if (command == 3)
        flags |= SWP_MAXIMIZE;
    else if (command == 9)
        flags |= SWP_RESTORE;
    if (command == 1 || command == 3 || command == 5 || command == 9)
        flags |= SWP_ACTIVATE;
    if (WinSetWindowPos(handle, HWND_TOP, 0, 0, 0, 0, flags))
    {
        if (command)
            w->style |= 0x10000000;
        else
            w->style &= ~0x10000000u;
    }
    return previous;
}
BOOL UpdateWindow(ULONG handle)
{
    if (!find_window(handle))
    {
        open32SetError(1400);
        return FALSE;
    }
    return WinUpdateWindow(handle);
}

void open32WindowsCleanup(void)
{
    /* Module shutdown releases host windows without reentering guest procedures. */
    while (windows)
    {
        Open32Window *w = windows;
        if (!w->destroyed)
        {
            WinSubclassWindow(w->handle, WinDefWindowProc);
            WinDestroyWindow(w->handle);
        }
        windows = w->next;
        discard_window(w);
    }
    if (creating)
    {
        Open32Window *w = creating;
        if (w->handle && !w->destroyed)
        {
            WinSubclassWindow(w->handle, WinDefWindowProc);
            WinDestroyWindow(w->handle);
        }
        creating = NULL;
        discard_window(w);
    }
}
