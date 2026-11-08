/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 custom windows translate their lifecycle callbacks through PM. */
#include "guest/launch_record.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwin/open32_post.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmwin/internal.h"
#include <stdio.h>
static _Thread_local Open32Window *windows, *creating;
static _Thread_local LxGuestMapping *pm_procedure_mapping, *pm_frame_mapping;

static void resize_children(Open32Window *parent, LONG delta)
{
    /* PM child origins follow the bottom edge; Windows child origins follow
       the top edge. Preserve Windows positions before delivering WM_SIZE. */
    if (!delta)
        return;
    for (Open32Window *child = windows; child; child = child->next)
    {
        SWP position;
        if (child->parent == parent->handle && WinQueryWindow(child->handle, 5) == parent->client &&
            WinQueryWindowPos(child->handle, &position))
        {
            int64_t y = (int64_t)position.y + delta;
            if (y >= INT32_MIN && y <= INT32_MAX)
                WinSetWindowPos(child->handle, 0, position.x, (LONG)y, 0, 0,
                                SWP_MOVE | SWP_NOADJUST | SWP_NOREDRAW);
        }
    }
}

Open32Window *open32FindWindow(HWND handle)
{
    if (!handle)
        return NULL;
    for (Open32Window *w = windows; w; w = w->next)
        if (w->handle == handle || w->client == handle)
            return w;
    for (Open32Window *w = creating; w; w = w->creating_previous)
        if (w->handle == handle || w->client == handle)
            return w;
    return NULL;
}

ULONG open32ClientWindow(ULONG handle)
{
    Open32Window *window = open32FindWindow(handle);
    return window && window->client ? window->client : handle;
}

ULONG open32PublicWindow(ULONG handle)
{
    Open32Window *window = open32FindWindow(handle);
    return window ? window->handle : handle;
}

/* WM_SETCURSOR runs before each uncaptured mouse move. An unhandled message selects the
   class cursor; a class without one leaves the current cursor in place. */
static void update_cursor(Open32Window *w, HWND handle)
{
    if (WinQueryCapture(HWND_DESKTOP))
        return;
    const ULONG client_hit = 1, mouse_move = 0x200;
    if (!open32WindowCallback(w, 0x20, open32PublicWindow(handle), client_hit | mouse_move << 16) &&
        w->class && w->class->cursor)
        SetCursor(w->class->cursor);
}

ULONG open32WindowCallback(Open32Window *w, ULONG message, ULONG first, ULONG second)
{
    return lx_execution_callback32_4(GLoaderState.execution, w->procedure, w->handle, message,
                                     first, second);
}

static MRESULT OS2API window_procedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    if (creating && !creating->client)
    {
        creating->client = handle;
        if (!creating->handle)
            creating->handle = handle;
        lx_pm_mark_open32_window(handle);
    }
    Open32Window *w = open32FindWindow(handle);
    if (!w)
        return WinDefWindowProc(handle, message, first, second);
    ULONG queued_result;
    if (open32QueuedCallback(handle, message, &queued_result))
        return (MRESULT)(uintptr_t)queued_result;
    if (message == WM_MOUSEMOVE)
        update_cursor(w, handle);
    switch (message)
    {
        case WM_RENDERFMT:
        case WM_RENDERALLFMTS:
        case WM_DESTROYCLIPBOARD:
        case WM_DRAWCLIPBOARD:
            return (MRESULT)(uintptr_t)open32WindowCallback(
                w,
                message == WM_RENDERFMT          ? 0x305
                : message == WM_RENDERALLFMTS    ? 0x306
                : message == WM_DESTROYCLIPBOARD ? 0x307
                                                 : 0x308,
                (ULONG)(uintptr_t)first, (ULONG)(uintptr_t)second);
        case WM_CREATE:
        {
            ULONG record = lx_execution_mapping_address(w->creation);
            if (!open32WindowCallback(w, 0x81, 0, record))
                return (MRESULT)(uintptr_t)1;
            return (MRESULT)(uintptr_t)(open32WindowCallback(w, 1, 0, record) == UINT32_MAX);
        }
        case WM_DESTROY:
            open32CaretWindowDestroyed(w->handle);
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
            open32QueueWindowDestroyed(w->handle);
            return NULL;
        case WM_SIZE:
            /* A resized host surface starts blank, so everything on it is erased again. */
            w->erase_pending = TRUE;
            for (Open32Window *child = windows; child; child = child->next)
                if (WinIsChild(child->client ? child->client : child->handle, handle))
                    child->erase_pending = TRUE;
            resize_children(w, (LONG)((uintptr_t)second >> 16 & 0xFFFF) -
                                   (LONG)((uintptr_t)first >> 16 & 0xFFFF));
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
        {
            QMSG native = {.hwnd = handle, .msg = message, .mp1 = first, .mp2 = second};
            ULONG translated, a, b;
            open32PmMessage(&native, &translated, &a, &b);
            return (MRESULT)(uintptr_t)open32WindowCallback(w, translated, a, b);
        }
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
        case 0x32e: /* DM_DRAGOVER */
        case 0x32f: /* DM_DROP */
            return (MRESULT)(uintptr_t)open32DragMessage(handle, (w->extended_style & 0x10) != 0,
                                                         message, (ULONG)(uintptr_t)first,
                                                         (ULONG)(uintptr_t)second);
        default:
            if (message >= WM_USER)
                return (MRESULT)(uintptr_t)open32WindowCallback(w, message - WM_USER + 0x400,
                                                                (ULONG)(uintptr_t)first,
                                                                (ULONG)(uintptr_t)second);
            return WinDefWindowProc(handle, message, first, second);
    }
}

static uint32 pm_window_bridge(uint8 *a)
{
    return (uint32)(uintptr_t)window_procedure(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
}

static uint32 pm_frame_bridge(uint8 *a)
{
    /* Open32 system menus carry Windows SC_* IDs. PM's frame procedure only
       recognises its own IDs, so let the application's client handle these. */
    ULONG handle = lx_guest_load_u32(a), message = lx_guest_load_u32(a + 4);
    if (message == WM_SYSCOMMAND && (lx_guest_load_u32(a + 8) & 0xF000) == 0xF000)
    {
        HWND client = WinWindowFromID(handle, FID_CLIENT);
        if (client)
            return (uint32)(uintptr_t)WinSendMsg(client, message,
                                                 (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8),
                                                 (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
    }
    return (uint32)(uintptr_t)WinDefWindowProc(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(a + 8),
                                               (MPARAM)(uintptr_t)lx_guest_load_u32(a + 12));
}

static PFNWP pm_window_procedure(BOOL frame)
{
    LxGuestMapping **slot = frame ? &pm_frame_mapping : &pm_procedure_mapping;
    if (!*slot)
    {
        LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
        const LxExecutionApi32 api = {frame ? pm_frame_bridge : pm_window_bridge, 16, 0,
                                      LX_ABI_STACK32};
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
        *slot = mapping;
    }
    return (PFNWP)(uintptr_t)lx_execution_mapping_address(*slot);
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

/* A superclass runs its own procedure over a PM control, and sees creation first. */
static void superclass_control(ULONG control, Open32Class *class, PSZ title, ULONG style, LONG x,
                               LONG y, LONG width, LONG height, ULONG parent, ULONG menu,
                               ULONG instance, ULONG parameter, ULONG extended_style)
{
    open32SetNativeClassName(control, class->name);
    open32NativeWindowLong(control, -4, (LONG) class->procedure, TRUE);
    size_t title_bytes = strlen(title ? title : "") + 1;
    LxGuestMapping *creation = lx_execution_map(GLoaderState.execution, 0,
                                                (title_bytes + 48 + 4095) & ~(size_t)4095, 3, 0);
    if (!creation)
        return;
    uint32 address = lx_execution_mapping_address(creation);
    uint8 *record = lx_execution_mapping_host(creation);
    uint32 fields[] = {parameter,
                       instance,
                       menu,
                       parent,
                       (uint32)height,
                       (uint32)width,
                       (uint32)y,
                       (uint32)x,
                       style,
                       address + 48,
                       lx_execution_mapping_address(class->names),
                       extended_style};
    for (unsigned i = 0; i < 12; i++)
        lx_guest_store_u32(record + 4 * i, fields[i]);
    memcpy(record + 48, title ? title : "", title_bytes);
    SendMessage(control, 0x81, 0, address);
    SendMessage(control, 1, 0, address);
    lx_execution_unmap(GLoaderState.execution, creation);
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
    Open32Class *control_class = class->builtin == OPEN32_BUILTIN_CONTROL ? class : class->alias;
    if (control_class && control_class->builtin == OPEN32_BUILTIN_CONTROL)
    {
        ULONG control = 0;
        BOOL created = open32Presentation() &&
                       open32CreateNativeControl(control_class->name, title, style, x, y, width,
                                                 height, parent, menu, &control);
        if (created && control && control_class != class)
            superclass_control(control, class, title, style, x, y, width, height, parent, menu,
                               instance, parameter, extended_style);
        open32ClassRelease(class);
        if (!created)
            open32SetError(1407);
        return control;
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
    PFNWP native_procedure = pm_window_procedure(FALSE);
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
    BOOL overlapped = !(style & 0xc0000000u);
    w->width = width == INT32_MIN ? (overlapped ? 640 : 0) : width;
    /* CW_USEDEFAULT width selects both dimensions; the supplied height is ignored. */
    w->height = width == INT32_MIN ? (overlapped ? 480 : 0) : height == INT32_MIN ? 480 : height;
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
    HWND pm_parent = (style & 0x40000000u) ? open32ClientWindow(parent) : HWND_DESKTOP;
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
    BOOL framed = !(style & 0x40000000u) && (style & 0x00c00000u) == 0x00c00000u;
    if (framed)
    {
        /* Frame controls also set the PM sizing style used by the host window.
           Retain the Windows sizing and min/max capabilities when making the
           native frame, rather than leaving every captioned window fixed. */
        ULONG controls = ((style & 0x00080000u) ? 0x2 : 0) |  /* WS_SYSMENU */
                         ((style & 0x00040000u) ? 0x8 : 0) |  /* WS_THICKFRAME */
                         ((style & 0x00020000u) ? 0x10 : 0) | /* WS_MINIMIZEBOX */
                         ((style & 0x00010000u) ? 0x20 : 0);  /* WS_MAXIMIZEBOX */
        FRAMECDATA frame = {sizeof frame, controls, 0, 0};
        w->handle = WinCreateWindow(pm_parent, (PSZ)WC_FRAME, w->title, pm_style, w->x,
                                    bounds.yTop - w->y - w->height, w->width, w->height,
                                    parent ? parent : HWND_DESKTOP, HWND_TOP, 0, &frame, NULL);
        if (!w->handle)
        {
            creating = previous;
            discard_window(w);
            open32SetError(8);
            return 0;
        }
        PFNWP frame_procedure = pm_window_procedure(TRUE);
        if (!frame_procedure || !WinSubclassWindow(w->handle, frame_procedure))
        {
            WinDestroyWindow(w->handle);
            creating = previous;
            discard_window(w);
            open32SetError(8);
            return 0;
        }
        pm_parent = w->handle;
    }
    HWND handle =
        WinCreateWindow(pm_parent, pm_class, w->title, framed ? pm_style | WS_VISIBLE : pm_style,
                        framed ? 0 : w->x, framed ? 0 : bounds.yTop - w->y - w->height, w->width,
                        w->height, framed || (style & 0x40000000u) ? pm_parent : parent, HWND_TOP,
                        framed                  ? FID_CLIENT
                        : (style & 0x40000000u) ? menu
                                                : 0,
                        NULL, NULL);
    creating = previous;
    w->creating_previous = NULL;
    lx_execution_unmap(GLoaderState.execution, w->creation);
    w->creation = NULL;
    if (!handle)
    {
        if (framed)
            WinDestroyWindow(w->handle);
        discard_window(w);
        open32SetError(1407);
        return 0;
    }
    if (!framed)
        w->handle = handle;
    w->client = handle;
    w->next = windows;
    windows = w;
    /* A frame created visible never passes through a show request of its own. */
    if (framed && WinIsWindowVisible(w->handle) && lx_launch_presentation_pending())
        WinSetWindowPos(w->handle, HWND_TOP, 0, 0, 0, 0, SWP_SHOW);
    ULONG result = w->handle;
    if (framed)
        WinSendMsg(result, WM_UPDATEFRAME, 0, 0);
    if (!(style & 0x40000000u) && menu && !SetMenu(result, menu))
    {
        DestroyWindow(result);
        return 0;
    }
    return result;
}

BOOL open32EraseBackground(ULONG handle, ULONG dc)
{
    Open32Window *w = open32FindWindow(handle);
    if (!w)
    {
        /* A dialog procedure that paints for itself still expects an erased background. */
        RECTL area;
        if (!open32NativeDialogWindow(handle) || WinQueryWindow(handle, 5) != HWND_DESKTOP ||
            !WinQueryWindowRect(handle, &area))
            return FALSE;
        return WinFillRect(dc, &area, -34); /* SYSCLR_DIALOGBACKGROUND */
    }
    if (!w->erase_pending)
        return TRUE;
    w->erase_pending = FALSE;
    return open32WindowCallback(w, 0x14, dc, 0) != 0;
}

LONG DefWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    if (message == 0x81 || message == 0x86)
        return 1;
    if (message == 0x14) /* WM_ERASEBKGND: paint the class background. */
    {
        Open32Window *window = open32FindWindow(handle);
        LONG client[4];
        ULONG brush = window && window->class ? window->class->brush : 0;
        if (!brush || !GetClientRect(handle, client))
            return 0;
        return FillRect(first, client, brush) != 0;
    }
    if (message == 0x20) /* WM_SETCURSOR: the parent may choose for its children. */
    {
        ULONG parent = GetParent(handle);
        return parent ? SendMessage(parent, message, first, second) : 0;
    }
    if (message == 1 || message == 0x82)
        return 0;
    if (message == 16)
        return DestroyWindow(handle) ? 0 : -1;
    if (message == 0x112)
    {
        switch (first & 0xFFF0)
        {
            case 0xF020:
                ShowWindow(handle, 6);
                return 0;
            case 0xF030:
                ShowWindow(handle, 3);
                return 0;
            case 0xF060:
                return SendMessage(handle, 16, 0, 0);
            case 0xF120:
                ShowWindow(handle, 9);
                return 0;
            default:
                return 0;
        }
    }
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
        if (!w || !w->class->brush || !WinQueryWindowRect(open32ClientWindow(handle), &rect))
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
    if (!*link && open32NativeDialogWindow(handle))
    {
        open32TimersWindowDestroyed(handle);
        return WinDestroyWindow(handle);
    }
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

/* Records live on their owning thread, so guest procedures run there. Any other thread
   reaches a window through PM's process-wide window registry and a queued call. */
typedef struct ForeignSend
{
    ULONG handle, message, first, second;
} ForeignSend;

static uintptr_t foreign_send(void *context)
{
    const ForeignSend *send = context;
    return (ULONG)SendMessage(send->handle, send->message, send->first, send->second);
}

typedef struct ForeignLong
{
    ULONG handle;
    LONG index;
} ForeignLong;

static uintptr_t foreign_long(void *context)
{
    const ForeignLong *query = context;
    return (ULONG)GetWindowLong(query->handle, query->index);
}

LONG GetWindowLong(ULONG handle, LONG index)
{
    TRACE_NATIVE("GetWindowLong(%u, %d)", (uint)handle, (int)index);
    Open32Window *w = open32FindWindow(handle);
    if (!w)
    {
        if (open32NativeDialogWindow(handle))
            return open32NativeWindowLong(handle, index, 0, FALSE);
        ForeignLong query = {handle, index};
        uintptr_t value = 0;
        if (lx_pm_window_call(handle, foreign_long, &query, sizeof query, &value))
            return (LONG)(ULONG)value;
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
    if (!WinQueryWindowPos(handle, &old) ||
        !WinQueryWindowRect(WinQueryWindow(handle, 5), &parent_bounds))
        return FALSE;
    LONG new_width = flags & 1 ? old.cx : width;
    LONG new_height = flags & 1 ? old.cy : height;
    LONG new_x = flags & 2 ? old.x : x;
    LONG new_y = flags & 2 ? parent_bounds.yTop - old.y - old.cy : y;
    RECTL bounds;
    if (!WinQueryWindowRect(WinQueryWindow(handle, 5), &bounds))
        return FALSE;
    /* Windows keeps the top edge when resizing with SWP_NOMOVE; PM keeps
       the bottom edge. A height change therefore also moves the PM origin. */
    BOOL move = !(flags & 2) || new_height != old.cy;
    ULONG pm_flags = (flags & 1 ? 0 : SWP_SIZE) | (move ? SWP_MOVE : 0) |
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
    if (open32ShowMdiChild(w, command))
        return previous;
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
    return WinUpdateWindow(open32ClientWindow(handle));
}

void open32WindowsCleanup(void)
{
    /* Module shutdown releases host windows without reentering guest procedures. */
    while (windows)
    {
        Open32Window *w = windows;
        if (!w->destroyed)
        {
            WinSubclassWindow(w->client, WinDefWindowProc);
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
            WinSubclassWindow(w->client, WinDefWindowProc);
            WinDestroyWindow(w->handle);
        }
        creating = NULL;
        discard_window(w);
    }
    if (pm_frame_mapping)
    {
        lx_execution_unmap(GLoaderState.execution, pm_frame_mapping);
        pm_frame_mapping = NULL;
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
        ForeignSend send = {handle, message, first, second};
        uintptr_t result = 0;
        if (lx_pm_window_call(handle, foreign_send, &send, sizeof send, &result))
            return (LONG)(ULONG)result;
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
        return SendMessage(handle, 14, 0, 0);
    }
    return (LONG)open32WindowCallback(window, 14, 0, 0);
}

LONG GetWindowText(ULONG handle, PCHAR output, LONG capacity)
{
    Open32Window *window = open32FindWindow(handle);
    BOOL native = !window && open32NativeDialogWindow(handle);
    if (!window && !native && output && capacity > 0)
    {
        uint32 foreign;
        if (lx_execution_address(GLoaderState.execution, output, (uint32)capacity, LX_GUEST_WRITE,
                                 &foreign))
        {
            ForeignSend send = {handle, 13, (ULONG)capacity, foreign};
            uintptr_t length = 0;
            if (lx_pm_window_call(handle, foreign_send, &send, sizeof send, &length))
                return (LONG)(ULONG)length;
        }
    }
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
            result = WinQueryWindow(open32ClientWindow(handle), 2);
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
