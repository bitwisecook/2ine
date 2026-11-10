/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window-menu buttons keep title geometry and open the real command submenu. */
#include "os2/include/private/export.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/features.h"
#include <stdio.h>
#define CHECK(test)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(test))                                                                               \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static AnchorBlock *test_anchor(void)
{
    return lxHostThreadState()->anchor_block;
}

static Window *test_window(HWND handle)
{
    AnchorBlock *anchor = test_anchor();
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == handle)
            return anchor->windows[i];
    return NULL;
}

static HWND closed_client;

static MRESULT OS2API client_proc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_CLOSE)
    {
        closed_client = window;
        return 0;
    }
    return WinDefWindowProc(window, message, first, second);
}

static void dispatch_pending(HAB anchor)
{
    QMSG message;
    for (unsigned count = 0; count < 1000 && WinPeekMsg(anchor, &message, 0, 0, 0, PM_REMOVE);
         count++)
        WinDispatchMsg(anchor, &message);
}

static MPARAM point(LONG x, LONG y)
{
    return (MPARAM)(uintptr_t)((USHORT)x | (ULONG)(USHORT)y << 16);
}

int main(void)
{
    HAB anchor = WinInitialize(0);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(anchor && queue);
    CHECK(WinRegisterClass(anchor, "menu-parent", client_proc, 0, 0));
    CHECK(lxRuntimeWindowChrome() == LX_CHROME_BOTH);
    CHECK(lxRuntimeConfigureFeature("window_chrome", "invalid") == -1);
    CHECK(lxRuntimeWindowChrome() == LX_CHROME_BOTH);
    HWND parent = WinCreateWindow(HWND_DESKTOP, "menu-parent", "Menu test", WS_VISIBLE, 0, 0, 300,
                                  200, 0, HWND_TOP, 1, NULL, NULL);
    ULONG flags = FCF_TITLEBAR | FCF_SYSMENU | FCF_DLGBORDER;
    HWND client = 0;
    HWND frame = WinCreateStdWindow(parent, WS_VISIBLE, &flags, "menu-parent", "Child frame", 0, 0,
                                    0, &client);
    CHECK(parent && frame && client);
    CHECK(WinSetWindowPos(frame, HWND_TOP, 10, 10, 200, 140, SWP_MOVE | SWP_SIZE | SWP_SHOW));
    HWND system = WinWindowFromID(frame, FID_SYSMENU);
    Window *button = lx_pm_menu_window(system);
    CHECK(button &&
          (button->style & (MS_ACTIONBAR | MS_TITLEBUTTON)) == (MS_ACTIONBAR | MS_TITLEBUTTON) &&
          button->menu->count == 1);
    CHECK(button->w == button->h && button->h == WinQuerySysValue(HWND_DESKTOP, SV_CYTITLEBAR));
    WinSendMsg(system, WM_PAINT, 0, 0);
    RECTL rectangle = button->menu->items[0].rect;
    CHECK(rectangle.xLeft == 0 && rectangle.yBottom == 0 && rectangle.xRight == button->w &&
          rectangle.yTop == button->h);
    HPS space = WinGetPS(system);
    uint32_t center = 0;
    CHECK(space && lx_pm_raster_read(space, button->w / 2, button->h / 2, 1, 1, &center));
    CHECK(center == 0 && WinReleasePS(space));
    HWND popup = button->menu->items[0].item.hwndSubMenu;
    CHECK(popup && !WinIsWindowVisible(popup));
    WinSendMsg(system, WM_BUTTON1DOWN,
               (MPARAM)(uintptr_t)((unsigned)(button->w / 2) | (unsigned)(button->h / 2) << 16), 0);
    CHECK(WinIsWindowVisible(popup));
    WinSendMsg(popup, MM_ENDMENUMODE, 0, 0);
    CHECK(!WinIsWindowVisible(popup));
    /* Maximized document frames may merge their system button into an action bar. */
    HWND merged = WinCreateWindow(parent, (PSZ)WC_MENU, NULL, MS_ACTIONBAR | WS_VISIBLE, 0, 0, 280,
                                  24, parent, HWND_TOP, FID_MENU, NULL, NULL);
    MENUITEM item = {MIT_END, MIS_TEXT | MIS_SUBMENU, 0, SC_SYSMENU, popup, 0};
    CHECK(merged && (intptr_t)WinSendMsg(merged, MM_INSERTITEM, &item, "~System") != MIT_ERROR);
    item = (MENUITEM){MIT_END, MIS_TEXT, 0, 42, 0, 0};
    CHECK((intptr_t)WinSendMsg(merged, MM_INSERTITEM, &item, "~File") != MIT_ERROR);
    WinSendMsg(merged, WM_PAINT, 0, 0);
    Window *action = lx_pm_menu_window(merged);
    CHECK(action && action->menu->items[0].rect.xRight == action->h);
    CHECK(action->menu->items[1].rect.xLeft == action->h);
    WinSendMsg(merged, WM_BUTTON1DOWN, point(5, 5), 0);
    CHECK(WinIsWindowVisible(popup));
    Window *commands = lx_pm_menu_window(popup);
    CHECK(commands && commands->menu->active_parent == merged);
    PmMenuItem *close = &commands->menu->items[commands->menu->count - 1];
    CHECK(close->item.id == SC_CLOSE);
    WinSendMsg(popup, WM_BUTTON1DOWN, point(close->rect.xLeft + 2, close->rect.yBottom + 2), 0);
    dispatch_pending(anchor);
    CHECK(!WinIsWindowVisible(popup) && !action->menu->focus && closed_client == client);
    HWND generic =
        WinCreateWindow(parent, (PSZ)WC_MENU, NULL, MS_ACTIONBAR | MS_TITLEBUTTON | WS_VISIBLE, 0,
                        0, 280, 24, parent, HWND_TOP, 0x9001, NULL, NULL);
    item = (MENUITEM){MIT_END, MIS_TEXT, 0, 42, 0, 0};
    CHECK(generic && (intptr_t)WinSendMsg(generic, MM_INSERTITEM, &item, "A custom title button") !=
                         MIT_ERROR);
    WinSendMsg(generic, WM_PAINT, 0, 0);
    Window *custom = lx_pm_menu_window(generic);
    CHECK(custom && custom->menu->items[0].rect.xRight > custom->h);

    const char *modes[] = {"native", "os2", "both"};
    for (unsigned mode = 0; mode < 3; mode++)
    {
        CHECK(lxRuntimeConfigureFeature("window_chrome", modes[mode]) == 1);
        CHECK(lxRuntimeWindowChrome() == (int)mode);
        ULONG controls = FCF_TITLEBAR | FCF_SYSMENU | FCF_SIZEBORDER | FCF_MINMAX;
        HWND top_client = 0;
        HWND top = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &controls, "menu-parent",
                                      "Top frame", 0, 0, 0, &top_client);
        CHECK(top && top_client);
        CHECK(WinSetWindowPos(top, HWND_TOP, 20, 25, 240, 160, SWP_MOVE | SWP_SIZE | SWP_SHOW));
        HWND title = WinWindowFromID(top, FID_TITLEBAR);
        HWND sys = WinWindowFromID(top, FID_SYSMENU);
        HWND minmax = WinWindowFromID(top, FID_MINMAX);
        CHECK(title && sys && minmax);
        CHECK((ULONG)(uintptr_t)WinSendMsg(top, WM_QUERYFRAMECTLCOUNT, 0, 0) == 4);
        SWP records[4], before, after;
        RECTL client_rect;
        CHECK(WinQueryWindowPos(top_client, &before));
        CHECK((ULONG)(uintptr_t)WinSendMsg(top, WM_FORMATFRAME, records, &client_rect) == 4);
        CHECK(WinQueryWindowPos(top_client, &after) && before.cx == after.cx &&
              before.cy == after.cy);
        for (unsigned i = 0; i < 4; i++)
        {
            CHECK(WinQueryWindow(records[i].hwnd, QW_PARENT) == top);
            if (mode == LX_CHROME_NATIVE && records[i].hwnd != top_client)
                CHECK((records[i].fl & SWP_HIDE) && !(records[i].fl & SWP_SHOW));
            for (unsigned j = i + 1; j < 4; j++)
                CHECK(records[i].hwnd != records[j].hwnd);
        }
        CHECK(!!WinIsWindowVisible(title) == (mode != LX_CHROME_NATIVE));
        CHECK(!!WinIsWindowVisible(sys) == (mode != LX_CHROME_NATIVE));
        CHECK(!!WinIsWindowVisible(minmax) == (mode != LX_CHROME_NATIVE));
        CHECK(WinShowWindow(title, TRUE));
        CHECK(!!WinIsWindowVisible(title) == (mode != LX_CHROME_NATIVE));
        if (mode == LX_CHROME_NATIVE)
            CHECK(before.x == 0 && before.y == 0 && before.cx == 240 && before.cy == 160);
        else
        {
            CHECK(before.x > 0 && before.cy < 160);
            Window *buttons = lx_pm_menu_window(minmax);
            LONG button_width = WinQuerySysValue(HWND_DESKTOP, SV_CXMINMAXBUTTON);
            CHECK(buttons && buttons->menu->count == 2 && buttons->w == button_width * 2);
            WinSendMsg(minmax, WM_PAINT, 0, 0);
            WinSendMsg(minmax, WM_BUTTON1DOWN, point(button_width + 2, 2), 0);
            dispatch_pending(anchor);
            CHECK(WinQueryWindowULong(top, QWL_STYLE) & WS_MAXIMIZED);
            WinSendMsg(minmax, WM_BUTTON1DOWN, point(button_width + 2, 2), 0);
            dispatch_pending(anchor);
            CHECK(!(WinQueryWindowULong(top, QWL_STYLE) & WS_MAXIMIZED));
            CHECK(WinQueryWindowPos(top, &after) && after.x == 20 && after.y == 25 &&
                  after.cx == 240 && after.cy == 160);
            WinSendMsg(title, WM_BUTTON1DOWN, point(10, 10), 0);
            CHECK(WinQueryCapture(HWND_DESKTOP) == title);
            WinSendMsg(title, WM_MOUSEMOVE, point(17, 19), 0);
            WinSendMsg(title, WM_BUTTON1UP, point(10, 10), 0);
            CHECK(!WinQueryCapture(HWND_DESKTOP));
            CHECK(WinQueryWindowPos(top, &after) && after.x == 27 && after.y == 34);
            Window *top_window = test_window(top);
            Window *title_window = test_window(title);
            LxHostEvent event = {.type = LX_HOST_EVENT_BUTTON_DOWN,
                                 .window_data = top_window,
                                 .x = title_window->x + 10,
                                 .y = top_window->h - 1 - title_window->y - 10,
                                 .button = 1};
            CHECK(processHostEvent(test_anchor(), &event));
            dispatch_pending(anchor);
            CHECK(WinQueryCapture(HWND_DESKTOP) == title);
            /* Motion enters the client area but remains routed to the captured title bar. */
            event.type = LX_HOST_EVENT_MOTION;
            event.x += 7;
            event.y += 15;
            CHECK(processHostEvent(test_anchor(), &event));
            dispatch_pending(anchor);
            CHECK(WinQueryWindowPos(top, &after) && after.x == 34 && after.y == 19);
            event.type = LX_HOST_EVENT_BUTTON_UP;
            CHECK(processHostEvent(test_anchor(), &event));
            dispatch_pending(anchor);
            CHECK(!WinQueryCapture(HWND_DESKTOP));
        }
        WinSendMsg(top, WM_SYSCOMMAND, (MPARAM)(uintptr_t)SC_MINIMIZE, 0);
        CHECK(WinQueryWindowULong(top, QWL_STYLE) & WS_MINIMIZED);
        LxHostEvent restored = {.type = LX_HOST_EVENT_RESTORED, .window_data = test_window(top)};
        CHECK(processHostEvent(test_anchor(), &restored));
        CHECK(!(WinQueryWindowULong(top, QWL_STYLE) & (WS_MINIMIZED | WS_MAXIMIZED)));
        CHECK(!!WinIsWindowVisible(sys) == (mode != LX_CHROME_NATIVE));
        CHECK(WinSetParent(top, parent, TRUE));
        CHECK(WinIsWindowVisible(title) && WinIsWindowVisible(sys) && WinIsWindowVisible(minmax));
        WinSendMsg(top, WM_SYSCOMMAND, (MPARAM)(uintptr_t)SC_MINIMIZE, 0);
        CHECK(WinQueryWindowULong(top, QWL_STYLE) & WS_MINIMIZED);
        /* SV_DBLCLKTIME and the double-click rectangle decide the second press. */
        LxHostEvent double_click = {.type = LX_HOST_EVENT_BUTTON_DOWN,
                                    .window_data = test_window(parent),
                                    .x = test_window(top)->x + 2,
                                    .y = test_window(parent)->h - test_window(top)->y - 3,
                                    .button = 1,
                                    .clicks = 1};
        CHECK(processHostEvent(test_anchor(), &double_click));
        dispatch_pending(anchor);
        double_click.clicks = 2;
        CHECK(processHostEvent(test_anchor(), &double_click));
        dispatch_pending(anchor);
        CHECK(!(WinQueryWindowULong(top, QWL_STYLE) & WS_MINIMIZED) && WinIsWindowVisible(title));
        CHECK(WinSetParent(top, HWND_DESKTOP, TRUE));
        CHECK(!!WinIsWindowVisible(title) == (mode != LX_CHROME_NATIVE));
        CHECK(WinDestroyWindow(top));
    }
    ULONG fixed_flags = FCF_TITLEBAR | FCF_SYSMENU | FCF_DLGBORDER;
    HWND fixed = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &fixed_flags, "menu-parent",
                                    "Fixed dialog", 0, 0, 0, &client);
    CHECK(fixed && WinSetWindowPos(fixed, HWND_TOP, 0, 0, 240, 160, SWP_SIZE));
    BOOL native_resize_flags = lx_host_window_resizable(test_window(fixed)->heavy.window, FALSE);
    if (native_resize_flags)
        CHECK(!lx_host_window_is_resizable(test_window(fixed)->heavy.window));
    LxHostEvent resize = {.type = LX_HOST_EVENT_RESIZED,
                          .window_data = test_window(fixed),
                          .width = 561,
                          .height = 426};
    CHECK(!processHostEvent(test_anchor(), &resize));
    SWP unchanged;
    CHECK(WinQueryWindowPos(fixed, &unchanged) && unchanged.cx == 240 && unchanged.cy == 160);
    /* Fixed dialogs can still change size at the application's request. */
    CHECK(WinSetWindowPos(fixed, HWND_TOP, 0, 0, 260, 180, SWP_SIZE));
    ULONG fixed_style = WinQueryWindowULong(fixed, QWL_STYLE);
    CHECK(WinSetWindowULong(fixed, QWL_STYLE, fixed_style | FS_SIZEBORDER));
    if (native_resize_flags)
        CHECK(lx_host_window_is_resizable(test_window(fixed)->heavy.window));
    CHECK(processHostEvent(test_anchor(), &resize));
    CHECK(WinQueryWindowPos(fixed, &unchanged) && unchanged.cx == 561 && unchanged.cy == 426);
    CHECK(WinSetWindowULong(fixed, QWL_STYLE, fixed_style));
    if (native_resize_flags)
        CHECK(!lx_host_window_is_resizable(test_window(fixed)->heavy.window));
    CHECK(WinDestroyWindow(fixed));
    CHECK(WinDestroyWindow(parent));
    CHECK(WinDestroyMsgQueue(queue) && WinTerminate(anchor));
    puts("Frame chrome modes, menu ownership, controls, commands and dragging passed");
    return 0;
}
