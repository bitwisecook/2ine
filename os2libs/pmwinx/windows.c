/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 custom windows translate their lifecycle callbacks through PM. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwin/open32_post.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include <stdio.h>
static _Thread_local Open32Window *windows, *creating;
Open32Window *open32FindWindow(HWND handle)
{
    for (Open32Window *w = windows; w; w = w->next)
        if (w->handle == handle)
            return w;
    for (Open32Window *w = creating; w; w = w->creating_previous)
        if (w->handle == handle)
            return w;
    return NULL;
}
ULONG open32WindowCallback(Open32Window *w, ULONG message, ULONG first, ULONG second)
{
    return lx_execution_callback32_4(GLoaderState.execution, w->procedure, w->handle, message,
                                     first, second);
}
static MRESULT OS2API window_procedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    if (creating && !creating->handle)
    {
        creating->handle = handle;
        lx_pm_mark_open32_window(handle);
    }
    Open32Window *w = open32FindWindow(handle);
    if (!w)
        return WinDefWindowProc(handle, message, first, second);
    ULONG queued_result;
    if (open32QueuedCallback(handle, message, &queued_result))
        return (MRESULT)(uintptr_t)queued_result;
    switch (message)
    {
        case WM_CREATE:
        {
            ULONG record = lx_execution_mapping_address(w->creation);
            if (!open32WindowCallback(w, 0x81, 0, record))
                return (MRESULT)(uintptr_t)1;
            return (MRESULT)(uintptr_t)(open32WindowCallback(w, 1, 0, record) == UINT32_MAX);
        }
        case WM_DESTROY:
            open32CaretWindowDestroyed(handle);
            if (w->system_menu)
            {
                ULONG menu = w->system_menu;
                w->system_menu = 0;
                DestroyMenu(menu);
            }
            w->destroyed = 1;
            open32WindowCallback(w, 2, 0, 0);
            open32WindowCallback(w, 0x82, 0, 0);
            w->destroyed = 1;
            open32QueueWindowDestroyed(handle);
            return NULL;
        case WM_SIZE:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 5, 0, (ULONG)(uintptr_t)second);
        case WM_PAINT:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 15, 0, 0);
        case WM_CLOSE:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 16, 0, 0);
        case WM_SHOW:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 24, (ULONG)(uintptr_t)first, 0);
        case WM_ENABLE:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 10, (ULONG)(uintptr_t)first, 0);
        case WM_ACTIVATE:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 6, first ? 1 : 0,
                                                            (ULONG)(uintptr_t)second);
        case WM_SETFOCUS:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, second ? 7 : 8,
                                                            (ULONG)(uintptr_t)first, 0);
        case WM_CHAR:
            return (MRESULT)(uintptr_t)open32KeyboardMessage(w->handle, (ULONG)(uintptr_t)first,
                                                             (ULONG)(uintptr_t)second);
        case WM_SYSCOMMAND:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 0x112,
                                                            (ULONG)(uintptr_t)first & 0xFFFF, 0);
        case WM_COMMAND:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 0x111,
                                                            (ULONG)(uintptr_t)first & 0xFFFF, 0);
        case WM_INITMENU:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 0x117, (ULONG)(uintptr_t)second, 0);
        case WM_TIMER:
            return (MRESULT)(uintptr_t)open32WindowCallback(w, 0x113, (ULONG)(uintptr_t)first, 0);
        case WM_MOUSEMOVE:
        case WM_BUTTON1DOWN:
        case WM_BUTTON1UP:
        case WM_BUTTON1DBLCLK:
        case WM_BUTTON2DOWN:
        case WM_BUTTON2UP:
        case WM_BUTTON2DBLCLK:
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
        case WM_BUTTON3DBLCLK:
        {
            QMSG native = {.hwnd = handle, .msg = message, .mp1 = first, .mp2 = second};
            ULONG translated, a, b;
            if (open32PmMessage(&native, &translated, &a, &b))
                return (MRESULT)(uintptr_t)open32WindowCallback(w, translated, a, b);
            return 0;
        }
        default:
            if (message >= WM_USER)
                return (MRESULT)(uintptr_t)open32WindowCallback(w, message - WM_USER + 0x400,
                                                                (ULONG)(uintptr_t)first,
                                                                (ULONG)(uintptr_t)second);
            return WinDefWindowProc(handle, message, first, second);
    }
}
static _Thread_local LxGuestMapping *pm_procedure_mapping;
static uint32 pm_window_bridge(uint8 *a)
{
    return (uint32)(uintptr_t)window_procedure(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
}
static PFNWP pm_window_procedure(void)
{
    if (!pm_procedure_mapping)
    {
        LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
        const LxExecutionApi32 api = {pm_window_bridge, 16, 0, LX_ABI_STACK32};
        if (!mapping)
            return NULL;
        if (!lx_execution_emit_bridge32(GLoaderState.execution, lx_execution_mapping_host(mapping),
                                        4096, &api) ||
            !lx_execution_protect(GLoaderState.execution, mapping,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GLoaderState.execution, mapping);
            return NULL;
        }
        pm_procedure_mapping = mapping;
    }
    return (PFNWP)(uintptr_t)lx_execution_mapping_address(pm_procedure_mapping);
}
static void discard_window(Open32Window *w)
{
    open32PropertiesCleanup(w);
    open32TimersWindowDestroyed(w->handle);
    open32ClipboardWindowDestroyed(w->handle);
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
    PFNWP native_procedure = pm_window_procedure();
    if (!native_procedure || !WinRegisterClass(hab, pm_class, native_procedure, CS_SIZEREDRAW, 0))
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
    for (unsigned i = 0; i < 3; i++)
        w->scroll[i].maximum = 100;
    w->scroll[0].visible = (style & 0x00100000) != 0;
    w->scroll[1].visible = (style & 0x00200000) != 0;
    w->style = style;
    w->erase_pending = TRUE;
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
    w->creating_previous = previous;
    creating = w;
    RECTL bounds;
    HWND pm_parent = (style & 0x40000000u) ? parent : HWND_DESKTOP;
    if (!pm_parent)
    {
        creating = previous;
        discard_window(w);
        open32SetError(1400);
        return 0;
    }
    if (!WinQueryWindowRect(pm_parent, &bounds))
    {
        creating = previous;
        discard_window(w);
        open32SetError(1400);
        return 0;
    }
    ULONG pm_style =
        ((style & 0x10000000) ? WS_VISIBLE : 0) | ((style & 0x08000000) ? WS_DISABLED : 0) |
        ((style & 0x02000000) ? WS_CLIPCHILDREN : 0) | ((style & 0x04000000) ? WS_CLIPSIBLINGS : 0);
    HWND handle = WinCreateWindow(pm_parent, pm_class, w->title, pm_style, w->x,
                                  bounds.yTop - w->y - w->height, w->width, w->height, parent,
                                  HWND_TOP, (style & 0x40000000u) ? menu : 0, NULL, NULL);
    creating = previous;
    w->creating_previous = NULL;
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
    Open32Window *w = open32FindWindow(handle);
    if (!w)
        return FALSE;
    if (!w->erase_pending)
        return TRUE;
    w->erase_pending = FALSE;
    return open32WindowCallback(w, 0x14, dc, 0) != 0;
}
LONG DefWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    if (message == 0x81 || message == 0x86)
        return 1;
    if (message == 1 || message == 0x82)
        return 0;
    if (message == 16)
        return DestroyWindow(handle) ? 0 : -1;
    if (message == 12)
    {
        Open32Window *w = open32FindWindow(handle);
        char *text =
            second ? lx_execution_copy_string(GLoaderState.execution, second, 65536) : strdup("");
        if (!w || !text)
        {
            free(text);
            open32SetError(87);
            return 0;
        }
        if (!WinSetWindowText(handle, text))
        {
            free(text);
            return 0;
        }
        free(w->title);
        w->title = text;
        return 1;
    }
    if (message == 13 || message == 14)
    {
        Open32Window *w = open32FindWindow(handle);
        if (!w)
        {
            open32SetError(1400);
            return 0;
        }
        size_t length = strlen(w->title);
        if (message == 14)
            return (LONG)length;
        if (!first)
            return 0;
        if (length >= first)
            length = first - 1;
        LxGuestBorrow output = {0};
        if (!lx_execution_borrow(GLoaderState.execution, second, length + 1, LX_GUEST_WRITE,
                                 &output))
        {
            open32SetError(87);
            return 0;
        }
        memcpy(output.host, w->title, length);
        ((char *)output.host)[length] = 0;
        lx_execution_release(&output);
        return (LONG)length;
    }
    if (message == 0x14)
    {
        Open32Window *w = open32FindWindow(handle);
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
    TRACE_NATIVE("GetWindowLong(%u, %d)", (uint)handle, (int)index);
    Open32Window *w = open32FindWindow(handle);
    if (!w)
    {
        if (open32NativeDialogWindow(handle))
            return open32NativeWindowLong(handle, index, 0, FALSE);
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
    TRACE_NATIVE("SetWindowLong(%u, %d, %x)", (uint)handle, (int)index, (uint)value);
    Open32Window *w = open32FindWindow(handle);
    if (!w)
    {
        if (open32NativeDialogWindow(handle))
            return open32NativeWindowLong(handle, index, value, TRUE);
        open32SetError(1400);
        return 0;
    }
    if (index == -4 && !open32ValidProcedure((ULONG)value))
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
    Open32Window *w = open32FindWindow(handle);
    SWP old = {0};
    RECTL parent_bounds = {0};
    BOOL native = !w && open32NativeDialogWindow(handle) && WinQueryWindowPos(handle, &old) &&
                  WinQueryWindowRect(WinQueryWindow(handle, 5), &parent_bounds);
    if ((!w && !native) || ((flags & 1) == 0 && (width < 0 || height < 0)))
    {
        open32SetError(w || native ? 87 : 1400);
        return FALSE;
    }
    LONG new_width = flags & 1 ? (w ? w->width : old.cx) : width;
    LONG new_height = flags & 1 ? (w ? w->height : old.cy) : height;
    LONG new_x = flags & 2 ? (w ? w->x : old.x) : x;
    LONG new_y = flags & 2 ? (w ? w->y : parent_bounds.yTop - old.y - old.cy) : y;
    RECTL bounds;
    if (!WinQueryWindowRect(WinQueryWindow(handle, 5), &bounds))
        return FALSE;
    ULONG pm_flags = (flags & 1 ? 0 : SWP_SIZE) | (flags & 2 ? 0 : SWP_MOVE) |
                     (flags & 4 ? 0 : SWP_ZORDER) | (flags & 8 ? SWP_NOREDRAW : 0) |
                     (flags & 16 ? 0 : SWP_ACTIVATE) | (flags & 64 ? SWP_SHOW : 0) |
                     (flags & 128 ? SWP_HIDE : 0);
    HWND pm_after = after == 0 || after >= 0xFFFFFFFE ? HWND_TOP : after == 1 ? HWND_BOTTOM : after;
    BOOL result = WinSetWindowPos(handle, pm_after, new_x, bounds.yTop - new_y - new_height,
                                  new_width, new_height, pm_flags);
    w = open32FindWindow(handle);
    if (result && w)
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
    Open32Window *w = open32FindWindow(handle);
    if (!w)
    {
        if (open32NativeDialogWindow(handle) &&
            (command == 0 || command == 1 || command == 5 || command == 8))
        {
            BOOL previous = WinIsWindowVisible(handle);
            WinShowWindow(handle, command != 0);
            return previous;
        }
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
    if (!open32FindWindow(handle))
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
    if (pm_procedure_mapping)
    {
        lx_execution_unmap(GLoaderState.execution, pm_procedure_mapping);
        pm_procedure_mapping = NULL;
    }
}

LONG SendMessage(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    TRACE_NATIVE("SendMessage(%u, %x, %x, %x)", (uint)handle, (uint)message, (uint)first,
                 (uint)second);
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        if (open32NativeDialogWindow(handle))
            return open32NativeControlMessage(handle, message, first, second, FALSE);
        open32SetError(1400);
        return 0;
    }
    return (LONG)open32WindowCallback(window, message, first, second);
}
LONG CallWindowProc(ULONG procedure, ULONG handle, ULONG message, ULONG first, ULONG second)
{
    if (!open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return 0;
    }
    return (LONG)lx_execution_callback32_4(GLoaderState.execution, procedure, handle, message,
                                           first, second);
}

BOOL SetWindowText(ULONG handle, PSZ text)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window && !open32NativeDialogWindow(handle))
    {
        open32SetError(1400);
        return FALSE;
    }
    size_t size = text ? strlen(text) + 1 : 1;
    if (size > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    LxGuestMapping *mapping =
        lx_execution_map(GLoaderState.execution, 0, (size + 4095) & ~(size_t)4095, 3, 0);
    if (!mapping)
    {
        open32SetError(8);
        return FALSE;
    }
    memcpy(lx_execution_mapping_host(mapping), text ? text : "", size);
    ULONG address = lx_execution_mapping_address(mapping);
    BOOL result = window ? open32WindowCallback(window, 12, 0, address) != 0
                         : open32NativeControlMessage(handle, 12, 0, address, FALSE) != 0;
    lx_execution_unmap(GLoaderState.execution, mapping);
    return result;
}
LONG GetWindowTextLength(ULONG handle)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        if (open32NativeDialogWindow(handle))
            return open32NativeControlMessage(handle, 14, 0, 0, FALSE);
        open32SetError(1400);
        return 0;
    }
    return (LONG)open32WindowCallback(window, 14, 0, 0);
}
LONG GetWindowText(ULONG handle, PCHAR output, LONG capacity)
{
    Open32Window *window = open32FindWindow(handle);
    BOOL native = !window && open32NativeDialogWindow(handle);
    if ((!window && !native) || !output || capacity <= 0)
    {
        open32SetError(window || native ? 87 : 1400);
        return 0;
    }
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, output, (uint32)capacity, LX_GUEST_WRITE,
                              &address))
    {
        open32SetError(87);
        return 0;
    }
    return window ? (LONG)open32WindowCallback(window, 13, (ULONG)capacity, address)
                  : open32NativeControlMessage(handle, 13, (ULONG)capacity, address, FALSE);
}

ULONG GetWindow(ULONG handle, ULONG command)
{
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), handle))
    {
        open32SetError(1400);
        return 0;
    }
    ULONG result, direction;
    switch (command)
    {
        case 0:
        case 1:
        {
            ULONG parent = WinQueryWindow(handle, 5);
            result = parent ? WinQueryWindow(parent, command ? 3 : 2) : 0;
            direction = command ? 1 : 0;
            break;
        }
        case 2:
        case 3:
            direction = command - 2;
            result = WinQueryWindow(handle, (LONG)direction);
            break;
        case 4:
        {
            Open32Window *window = open32FindWindow(handle);
            if (window && (window->style & 0x40000000u))
                return 0;
            result = WinQueryWindow(handle, 4);
            return result == HWND_DESKTOP ? 0 : result;
        }
        case 5:
            result = WinQueryWindow(handle, 2);
            direction = 0;
            break;
        default:
            open32SetError(87);
            return 0;
    }
    /* Native menus and frame decorations are outside the Windows child list. */
    while (result && !open32FindWindow(result))
        result = WinQueryWindow(result, (LONG)direction);
    return result;
}
