/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame restore geometry uses the public QWS_*RESTORE words. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/menus.h"
#include "os2libs/pmwin/controls.h"
#include "runtime/runtime_features.h"

enum
{
    FRAME_XRESTORE = 0x0c,
    FRAME_YRESTORE = 0x0e,
    FRAME_CXRESTORE = 0x10,
    FRAME_CYRESTORE = 0x12
};
BOOL prepareFrameState(Window *window, SWP *position)
{
    ULONG state = position->fl & (SWP_MINIMIZE | SWP_MAXIMIZE | SWP_RESTORE);
    if (!state)
        return TRUE;
    if ((state & (state - 1)) || strcmp(window->window_class_name, "#1") || window->data_len < 0x14)
        return FALSE;
    if (sendWindowPos(window, WM_MINMAXFRAME, position))
    {
        position->fl &= ~(SWP_SIZE | SWP_MOVE);
        position->x = window->x;
        position->y = window->y;
        position->cx = window->w;
        position->cy = window->h;
        return TRUE;
    }
    if (state == SWP_MAXIMIZE || state == SWP_MINIMIZE)
    {
        if (!(window->style & (WS_MAXIMIZED | WS_MINIMIZED)))
        {
            WinSetWindowUShort(window->hwnd, FRAME_XRESTORE, window->w ? window->x : position->x);
            WinSetWindowUShort(window->hwnd, FRAME_YRESTORE, window->h ? window->y : position->y);
            WinSetWindowUShort(window->hwnd, FRAME_CXRESTORE, window->w ? window->w : position->cx);
            WinSetWindowUShort(window->hwnd, FRAME_CYRESTORE, window->h ? window->h : position->cy);
        }
        if (window->parent == &desktop_window &&
            ((window->style & WS_MINIMIZED) || state == SWP_MINIMIZE))
            lx_host_window_minimize(window->heavy.window, state == SWP_MINIMIZE);
        if (state == SWP_MAXIMIZE)
        {
            position->x = position->y = 0;
            position->cx = window->parent->w;
            position->cy = window->parent->h;
            position->fl |= SWP_MOVE | SWP_SIZE;
        }
        else if (window->parent != &desktop_window)
        {
            position->x = (SHORT)WinQueryWindowUShort(window->hwnd, 0x14); /* QWS_XMINIMIZE */
            position->y = (SHORT)WinQueryWindowUShort(window->hwnd, 0x16); /* QWS_YMINIMIZE */
            position->cx = WinQuerySysValue(HWND_DESKTOP, 38);             /* SV_CXICON */
            position->cy = WinQuerySysValue(HWND_DESKTOP, 39);             /* SV_CYICON */
            position->fl |= SWP_MOVE | SWP_SIZE;
        }
        window->style = (window->style & ~(WS_MINIMIZED | WS_MAXIMIZED)) |
                        (state == SWP_MINIMIZE ? WS_MINIMIZED : WS_MAXIMIZED);
    }
    else if (window->style & (WS_MAXIMIZED | WS_MINIMIZED))
    {
        if (window->parent == &desktop_window && (window->style & WS_MINIMIZED))
            lx_host_window_minimize(window->heavy.window, FALSE);
        if (!(position->fl & SWP_MOVE))
        {
            position->x = (SHORT)WinQueryWindowUShort(window->hwnd, FRAME_XRESTORE);
            position->y = (SHORT)WinQueryWindowUShort(window->hwnd, FRAME_YRESTORE);
        }
        if (!(position->fl & SWP_SIZE))
        {
            position->cx = WinQueryWindowUShort(window->hwnd, FRAME_CXRESTORE);
            position->cy = WinQueryWindowUShort(window->hwnd, FRAME_CYRESTORE);
        }
        position->fl |= SWP_MOVE | SWP_SIZE;
        window->style &= ~(WS_MAXIMIZED | WS_MINIMIZED);
    }
    return position->cx > 0 && position->cy > 0;
}

/* Borders are measured in device pels, using the same published metrics for
   queries, frame/client conversion, formatting and painting. */
enum
{
    FRAME_TITLE = 0x1,
    FRAME_SYSTEM = 0x2,
    FRAME_MENU = 0x4,
    FRAME_SIZE = 0x8,
    FRAME_MIN = 0x10,
    FRAME_MAX = 0x20,
    FRAME_VERTICAL = 0x40,
    FRAME_HORIZONTAL = 0x80,
    FRAME_DIALOG = 0x100,
    FRAME_THIN = 0x200,
    STYLE_DIALOG = 0x80,
    STYLE_THIN = 0x100,
    STYLE_SIZE = 0x800
};
BOOL isFrameWindow(const Window *window)
{
    return window && (window->frame_flags ||
                      (window->window_class_name && !strcmp(window->window_class_name, "#1")));
}
BOOL frameGuestChrome(const Window *window)
{
    return window &&
           (window->parent != &desktop_window || lxRuntimeWindowChrome() != LX_CHROME_NATIVE);
}
BOOL frameControlSuppressed(const Window *window)
{
    return window && isFrameWindow(window->parent) &&
           ((window->parent->style & WS_MINIMIZED) ||
            (!frameGuestChrome(window->parent) &&
             (window->id == FID_TITLEBAR || window->id == FID_SYSMENU ||
              window->id == FID_MINMAX)));
}
BOOL frameResizable(const Window *window)
{
    return isFrameWindow(window) && (window->style & STYLE_SIZE) != 0;
}
void applyHostWindowChrome(Window *window)
{
    if (window && window->parent == &desktop_window)
    {
        if (!strcmp(window->window_class_name, "#4"))
        {
            lx_host_window_border(window->heavy.window, FALSE);
            lx_host_window_resizable(window->heavy.window, FALSE);
        }
        else if (isFrameWindow(window))
        {
            lx_host_window_border(window->heavy.window, lxRuntimeWindowChrome() != LX_CHROME_OS2);
            lx_host_window_resizable(window->heavy.window, frameResizable(window));
        }
    }
}
POINTL frameBorderSize(const Window *window)
{
    POINTL border = {0, 0};
    if (!isFrameWindow(window) || !frameGuestChrome(window))
        return border;
    LONG metric = -1;
    if ((window->frame_flags & FRAME_SIZE) || (window->style & STYLE_SIZE))
        metric = 4; /* SV_CXSIZEBORDER / SV_CYSIZEBORDER */
    else if ((window->frame_flags & FRAME_DIALOG) || (window->style & STYLE_DIALOG))
        metric = 28; /* SV_CXDLGFRAME / SV_CYDLGFRAME */
    else if ((window->frame_flags & FRAME_THIN) || (window->style & STYLE_THIN))
        metric = 26; /* SV_CXBORDER / SV_CYBORDER */
    if (metric >= 0)
    {
        border.x = WinQuerySysValue(HWND_DESKTOP, metric);
        border.y = WinQuerySysValue(HWND_DESKTOP, metric + 1);
    }
    return border;
}
static LONG frame_title_height(Window *window)
{
    return frameGuestChrome(window) && !(window->style & WS_MINIMIZED) &&
                   WinWindowFromID(window->hwnd, FID_TITLEBAR)
               ? WinQuerySysValue(HWND_DESKTOP, 30)
               : 0;
}
BOOL frameClientRect(Window *window, PRECTL rect, BOOL client)
{
    if (!window || !rect)
        return FALSE;
    POINTL border = frameBorderSize(window);
    LONG top = border.y + frame_title_height(window) +
               (WinWindowFromID(window->hwnd, FID_MENU) ? lx_pm_display_scale(24) : 0);
    LONG right =
        border.x +
        (WinWindowFromID(window->hwnd, FID_VERTSCROLL) ? WinQuerySysValue(HWND_DESKTOP, 22) : 0);
    LONG bottom =
        border.y +
        (WinWindowFromID(window->hwnd, FID_HORZSCROLL) ? WinQuerySysValue(HWND_DESKTOP, 25) : 0);
    RECTL output = *rect;
    int64_t left = (int64_t)output.xLeft + (client ? border.x : -border.x);
    int64_t low = (int64_t)output.yBottom + (client ? bottom : -bottom);
    int64_t high = (int64_t)output.yTop + (client ? -top : top);
    int64_t far = (int64_t)output.xRight + (client ? -right : right);
    if (left < INT32_MIN || left > INT32_MAX || low < INT32_MIN || low > INT32_MAX ||
        high < INT32_MIN || high > INT32_MAX || far < INT32_MIN || far > INT32_MAX || left >= far ||
        low >= high)
        return FALSE;
    *rect = (RECTL){(LONG)left, (LONG)low, (LONG)far, (LONG)high};
    return TRUE;
}
static const ULONG frame_controls[] = {FID_TITLEBAR,   FID_SYSMENU,    FID_MINMAX, FID_MENU,
                                       FID_VERTSCROLL, FID_HORZSCROLL, FID_CLIENT};
ULONG frameControlCount(Window *window)
{
    ULONG count = 0;
    for (size_t i = 0; i < sizeof frame_controls / sizeof frame_controls[0]; i++)
        count += WinWindowFromID(window->hwnd, frame_controls[i]) != 0;
    return count;
}
static void frame_position(SWP *positions, ULONG *count, HWND child, LONG x, LONG y, LONG width,
                           LONG height)
{
    if (child)
    {
        Window *control = getWindowFromHWND(getAnchorBlockNoHAB(), child);
        BOOL visible =
            control && (control->frame_chrome_hidden ? control->frame_chrome_visible
                                                     : (control->style & WS_VISIBLE) != 0);
        ULONG visibility = visible ? SWP_SHOW : SWP_HIDE;
        if (frameControlSuppressed(control))
            visibility = SWP_HIDE;
        positions[(*count)++] = (SWP){SWP_MOVE | SWP_SIZE | SWP_NOADJUST | visibility,
                                      height > 0 ? height : 1,
                                      width > 0 ? width : 1,
                                      y,
                                      x,
                                      HWND_TOP,
                                      child,
                                      0,
                                      0};
    }
}
ULONG frameFormatPositions(Window *window, MPARAM output, MPARAM client_rect)
{
    if (!output || !isFrameWindow(window))
        return 0;
    SWP positions[sizeof frame_controls / sizeof frame_controls[0]];
    ULONG count = 0;
    POINTL border = frameBorderSize(window);
    LONG title = frame_title_height(window), top = window->h - border.y;
    LONG width = window->w - border.x * 2;
    HWND titlebar = WinWindowFromID(window->hwnd, FID_TITLEBAR);
    HWND system = WinWindowFromID(window->hwnd, FID_SYSMENU);
    HWND buttons = WinWindowFromID(window->hwnd, FID_MINMAX);
    LONG system_width = system && title ? title : 0;
    LONG button_width =
        buttons && title
            ? WinQuerySysValue(HWND_DESKTOP, 33) *
                  (!!(window->frame_flags & FRAME_MIN) + !!(window->frame_flags & FRAME_MAX))
            : 0;
    if (title)
    {
        top -= title;
    }
    frame_position(positions, &count, titlebar, border.x + system_width, top,
                   width - system_width - button_width, title);
    frame_position(positions, &count, system, border.x, top, system_width, title);
    frame_position(positions, &count, buttons, window->w - border.x - button_width, top,
                   button_width, title);
    HWND menu = WinWindowFromID(window->hwnd, FID_MENU);
    if (menu)
    {
        top -= lx_pm_display_scale(24);
        frame_position(positions, &count, menu, border.x, top, width, lx_pm_display_scale(24));
    }
    RECTL client = {0, 0, window->w, window->h};
    if (!frameClientRect(window, &client, TRUE))
        client = (RECTL){border.x, border.y, border.x + 1, border.y + 1};
    frame_position(positions, &count, WinWindowFromID(window->hwnd, FID_VERTSCROLL), client.xRight,
                   client.yBottom, window->w - border.x - client.xRight,
                   client.yTop - client.yBottom);
    frame_position(positions, &count, WinWindowFromID(window->hwnd, FID_HORZSCROLL), client.xLeft,
                   border.y, client.xRight - client.xLeft, client.yBottom - border.y);
    frame_position(positions, &count, WinWindowFromID(window->hwnd, FID_CLIENT), client.xLeft,
                   client.yBottom, client.xRight - client.xLeft, client.yTop - client.yBottom);
    if ((client_rect && !controlWrite(client_rect, &client, sizeof client)) ||
        !controlWrite(output, positions, count * sizeof *positions))
        return 0;
    return count;
}
void formatFrame(Window *window)
{
    if (!window || !isFrameWindow(window) || window->formatting_frame)
        return;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    HWND handle = window->hwnd;
    window->formatting_frame = TRUE;
    ULONG capacity = (ULONG)(uintptr_t)sendMessage(window, WM_QUERYFRAMECTLCOUNT, 0, 0);
    if (getWindowFromHWND(anchor, handle) != window)
        return;
    if (!capacity || capacity > 4096)
    {
        window->formatting_frame = FALSE;
        return;
    }
    size_t bytes = capacity * sizeof(SWP) + sizeof(RECTL);
    LxGuestMapping *mapping = NULL;
    unsigned char *storage;
    uintptr_t address;
    if (TWOINE_PORTABLE_LOADER && GLoaderState.execution)
    {
        mapping = lx_execution_map(GLoaderState.execution, 0, (bytes + 4095) & ~(size_t)4095, 3, 0);
        storage = mapping ? lx_execution_mapping_host(mapping) : NULL;
        address = mapping ? lx_execution_mapping_address(mapping) : 0;
    }
    else
    {
        storage = calloc(1, bytes);
        address = (uintptr_t)storage;
    }
    if (storage)
    {
        memset(storage, 0, bytes);
        ULONG count = (ULONG)(uintptr_t)sendMessage(window, WM_FORMATFRAME, (MPARAM)address,
                                                    (MPARAM)(address + capacity * sizeof(SWP)));
        if (count <= capacity && getWindowFromHWND(anchor, handle) == window)
            for (ULONG i = 0; i < count; i++)
            {
                SWP position;
                memcpy(&position, storage + i * sizeof position, sizeof position);
                Window *child = getWindowFromHWND(anchor, position.hwnd);
                if (child && child->parent == window)
                    WinSetWindowPos(position.hwnd, position.hwndInsertBehind, position.x,
                                    position.y, position.cx, position.cy, position.fl);
                if (getWindowFromHWND(anchor, handle) != window)
                    break;
            }
    }
    if (mapping)
        lx_execution_unmap(GLoaderState.execution, mapping);
    else
        free(storage);
    if (getWindowFromHWND(anchor, handle) == window)
        window->formatting_frame = FALSE;
}
void paintFrameBorder(Window *window, HPS space)
{
    if ((window->style & WS_MINIMIZED) && window->parent != &desktop_window)
    {
        RECTL rect = {0, 0, window->w, window->h};
        WinFillRect(space, &rect, CLR_PALEGRAY);
        WinDrawText(space, -1, window->text ? window->text : "", &rect, CLR_BLACK, CLR_PALEGRAY,
                    0x500); /* DT_CENTER | DT_VCENTER */
    }
    POINTL border = frameBorderSize(window);
    for (LONG edge = 0; edge < border.x || edge < border.y; edge++)
    {
        LONG left = edge, right = window->w - edge;
        LONG bottom = edge, top = window->h - edge;
        if (right <= left || top <= bottom)
            break;
        LONG light = edge == 0 ? CLR_DARKGRAY : CLR_WHITE;
        LONG dark = edge == 0 ? CLR_DARKGRAY : CLR_BLACK;
        RECTL strip;
        if (edge < border.x)
        {
            strip = (RECTL){left, bottom, left + 1, top};
            WinFillRect(space, &strip, light);
            strip = (RECTL){right - 1, bottom, right, top};
            WinFillRect(space, &strip, dark);
        }
        if (edge < border.y)
        {
            strip = (RECTL){left, top - 1, right, top};
            WinFillRect(space, &strip, light);
            strip = (RECTL){left, bottom, right, bottom + 1};
            WinFillRect(space, &strip, dark);
        }
    }
}
static BOOL startFrameTracking(Window *source, Window *frame, ULONG command, POINTL point)
{
    if (!isFrameWindow(frame) || !frame->parent || (frame->style & (WS_MAXIMIZED | WS_MINIMIZED)))
        return FALSE;
    if (WinQueryCapture(HWND_DESKTOP) && WinQueryCapture(HWND_DESKTOP) != source->hwnd)
        return FALSE;
    if (!WinMapWindowPoints(source->hwnd, frame->parent->hwnd, &point, 1) ||
        !WinSetCapture(HWND_DESKTOP, source->hwnd))
        return FALSE;
    source->frame_tracking = command;
    source->frame_track_start = point;
    source->frame_track_rect =
        (RECTL){frame->x, frame->y, frame->x + frame->w, frame->y + frame->h};
    return TRUE;
}
BOOL frameTrackMessage(Window *window, ULONG message, MPARAM first)
{
    if (!window->frame_tracking)
        return FALSE;
    if (WinQueryCapture(HWND_DESKTOP) != window->hwnd)
    {
        window->frame_tracking = 0;
        return FALSE;
    }
    Window *frame = isFrameWindow(window) ? window : window->parent;
    if (!isFrameWindow(frame))
        return FALSE;
    if (message == WM_BUTTON1UP)
    {
        window->frame_tracking = 0;
        WinSetCapture(HWND_DESKTOP, 0);
        return TRUE;
    }
    if (message != WM_MOUSEMOVE)
        return FALSE;
    ULONG packed = (ULONG)(uintptr_t)first;
    POINTL point = {(SHORT)packed, (SHORT)(packed >> 16)};
    if (!WinMapWindowPoints(window->hwnd, frame->parent->hwnd, &point, 1))
        return TRUE;
    LONG dx = point.x - window->frame_track_start.x, dy = point.y - window->frame_track_start.y;
    RECTL original = window->frame_track_rect;
    if (window->frame_tracking == 0x8001) /* SC_MOVE */
        WinSetWindowPos(frame->hwnd, HWND_TOP, original.xLeft + dx, original.yBottom + dy, 0, 0,
                        SWP_MOVE);
    else
    {
        LONG width = original.xRight - original.xLeft + dx;
        LONG height = original.yTop - original.yBottom - dy;
        POINTL border = frameBorderSize(frame);
        LONG minimum_width = frame_title_height(frame) * 4 + border.x * 2 + 1;
        LONG minimum_height = frame_title_height(frame) + border.y * 2 + 1;
        if (width >= minimum_width && height >= minimum_height)
            WinSetWindowPos(frame->hwnd, HWND_TOP, original.xLeft, original.yBottom + dy, width,
                            height, SWP_SIZE | SWP_MOVE);
    }
    return TRUE;
}
MRESULT frameSystemCommand(Window *window, USHORT command)
{
    if (!isFrameWindow(window))
        return 0;
    ULONG flags = command == 0x8002   ? SWP_MINIMIZE
                  : command == 0x8003 ? SWP_MAXIMIZE
                  : command == 0x8008 ? SWP_RESTORE
                                      : 0;
    if (flags)
    {
        WinSetWindowPos(window->hwnd, HWND_TOP, 0, 0, 0, 0, flags);
        WinInvalidateRect(window->hwnd, NULL, TRUE);
    }
    else if (command == 0x8004) /* SC_CLOSE preserves application WM_CLOSE handling. */
        WinPostMsg(window->hwnd, WM_CLOSE, 0, 0);
    else if (command == 0x8001 || (command == 0x8000 && (window->frame_flags & FRAME_SIZE)))
    {
        POINTL point;
        if (WinQueryPointerPos(HWND_DESKTOP, &point) &&
            WinMapWindowPoints(HWND_DESKTOP, window->hwnd, &point, 1))
            startFrameTracking(window, window, command, point);
    }
    return 0;
}
MRESULT OS2API frameControlProcedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return 0;
    if (frameTrackMessage(window, message, first))
        return 0;
    if (window->id == FID_TITLEBAR && isFrameWindow(window->parent))
    {
        if (message == WM_BUTTON1DOWN && !frameControlSuppressed(window))
        {
            ULONG point = (ULONG)(uintptr_t)first;
            startFrameTracking(window, window->parent, 0x8001,
                               (POINTL){(SHORT)point, (SHORT)(point >> 16)});
            return 0;
        }
        if (message == WM_BUTTON1DBLCLK && (window->parent->frame_flags & FRAME_MAX))
            return frameSystemCommand(window->parent,
                                      (window->parent->style & WS_MAXIMIZED) ? 0x8008 : 0x8003);
    }
    if (message == WM_PAINT)
    {
        RECTL rect;
        HPS space = WinBeginPaint(handle, 0, &rect);
        if (space)
        {
            BOOL active = anchor->active == topLevelWindow(anchor, handle);
            if(window->parent && window->parent->flash && ((currentSystemTicks()/500)&1)) active=!active;
            LONG background = active ? -23 : -22; /* SYSCLR_ACTIVE/INACTIVETITLE */
            LONG foreground = active ? -28 : -29; /* SYSCLR_ACTIVE/INACTIVETITLETEXT */
            WinFillRect(space, &rect, background);
            RECTL text = {3, 0, window->w - 3, window->h};
            WinDrawText(space, -1, window->text ? window->text : "", &text, foreground, background,
                        0x400);
            WinEndPaint(space);
        }
        return 0;
    }
    return WinDefWindowProc(handle, message, first, second);
}
BOOL WinCreateFrameControls(HWND handle, PFRAMECDATA data, PSZ title)
{
    TRACE_NATIVE("WinCreateFrameControls(%u, %p, '%s')", handle, data, title);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || !data || data->cb != sizeof *data)
        return FALSE;
    window->frame_flags |= data->flCreateFlags;
    if (data->flCreateFlags & FRAME_SIZE)
        window->style |= STYLE_SIZE;
    if (data->flCreateFlags & FRAME_DIALOG)
        window->style |= STYLE_DIALOG;
    if (data->flCreateFlags & FRAME_THIN)
        window->style |= STYLE_THIN;
    if (title && (!window->text || strcmp(window->text, title)))
        if (!WinSetWindowText(handle, title))
            return FALSE;
    if ((data->flCreateFlags & FRAME_TITLE) && !WinWindowFromID(handle, FID_TITLEBAR))
        if (!WinCreateWindow(handle, (PSZ)WC_TITLEBAR, title ? title : window->text, WS_VISIBLE, 0,
                             0, 1, 1, handle, HWND_TOP, FID_TITLEBAR, NULL, NULL))
            return FALSE;
    if ((data->flCreateFlags & FRAME_SYSTEM) && !WinWindowFromID(handle, FID_SYSMENU) &&
        !createSystemMenu(window, window->frame_flags))
        return FALSE;
    if ((data->flCreateFlags & (FRAME_MIN | FRAME_MAX)) && !WinWindowFromID(handle, FID_MINMAX) &&
        !createMinMaxMenu(window, window->frame_flags))
        return FALSE;
    HMODULE resources = data->hmodResources;
    LxHandleBorrow resource_lease = {0};
    if (resources)
    {
        /* FRAMECDATA is shared with USE16 resources and carries only a USHORT.
           Expand that alias before querying this process's module registry. */
        for (LxModule *module = GLoaderState.loaded_modules; module; module = module->next)
            if (!module->load_failed && (USHORT)module->handle == resources &&
                lx_handle_borrow(module->handle, LX_HANDLE_MODULE, &resource_lease))
            {
                resources = module->handle;
                break;
            }
        if (!resource_lease.resource && GLoaderState.main_module &&
            (USHORT)GLoaderState.main_module->handle == resources &&
            lx_handle_borrow(GLoaderState.main_module->handle, LX_HANDLE_MODULE, &resource_lease))
            resources = GLoaderState.main_module->handle;
    }
    if ((data->flCreateFlags & FRAME_MENU) && !WinWindowFromID(handle, FID_MENU) &&
        !WinLoadMenu(handle, resources, data->idResources))
    {
        lx_handle_release(&resource_lease);
        return FALSE;
    }
    if (data->flCreateFlags & 0x8000) /* FCF_ACCELTABLE */
    {
        ULONG table = WinLoadAccelTable(anchor->hab, resources, data->idResources);
        if (table && !WinSetAccelTable(anchor->hab, table, handle))
        {
            lx_handle_release(&resource_lease);
            return FALSE;
        }
    }
    applyHostWindowChrome(window);
    formatFrame(window);
    lx_handle_release(&resource_lease);
    return TRUE;
}
