/* Public domain. No rights reserved. Wholly AI-authored. */
/* Region enumeration ordering, pages and child visible-region notification. */
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/ps/pmmerge/pmwin/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static unsigned notifications(HAB hab, HWND window, ULONG message)
{
    unsigned count = 0;
    QMSG event;
    while (WinPeekMsg(hab, &event, 0, 0, 0, 1))
    {
        if (event.msg == WM_PAINT)
            WinDispatchMsg(hab, &event);
        if (event.hwnd == window && event.msg == message)
            count++;
    }
    return count;
}

static void enumeration(HPS ps)
{
    RECTL source[4] = {{20, 0, 30, 10}, {0, 20, 10, 30}, {0, 0, 10, 10}, {20, 20, 30, 30}};
    ULONG region = GpiCreateRegion(ps, 4, source);
    CHECK(region);
    const unsigned orders[4][4] = {{1, 3, 2, 0}, {3, 1, 0, 2}, {2, 0, 1, 3}, {0, 2, 3, 1}};
    for (unsigned direction = 1; direction <= 4; direction++)
    {
        RECTL actual[4];
        RGNRECT control = {1, 4, 0, direction};
        CHECK(GpiQueryRegionRects(ps, region, NULL, &control, actual));
        CHECK(control.crcReturned == 4);
        for (unsigned i = 0; i < 4; i++)
            CHECK(!memcmp(&actual[i], &source[orders[direction - 1][i]], sizeof(RECTL)));
        control.ircStart = 2;
        control.crc = 2;
        CHECK(GpiQueryRegionRects(ps, region, NULL, &control, actual));
        CHECK(control.crcReturned == 2 && control.ircStart == 2);
        CHECK(!memcmp(&actual[0], &source[orders[direction - 1][1]], sizeof(RECTL)));
        CHECK(!memcmp(&actual[1], &source[orders[direction - 1][2]], sizeof(RECTL)));
        control.ircStart = 5;
        CHECK(GpiQueryRegionRects(ps, region, NULL, &control, actual));
        CHECK(control.crcReturned == 0);
    }
    RECTL bounds = {5, 5, 25, 25}, actual[4];
    RGNRECT control = {1, 4, 0, 1};
    CHECK(GpiQueryRegionRects(ps, region, &bounds, &control, actual));
    CHECK(control.crcReturned == 4);
    RECTL expected[4] = {{5, 20, 10, 25}, {20, 20, 25, 25}, {5, 5, 10, 10}, {20, 5, 25, 10}};
    CHECK(!memcmp(actual, expected, sizeof expected));
    control = (RGNRECT){0, 0, 999, 1};
    CHECK(GpiQueryRegionRects(ps, region, NULL, &control, NULL));
    CHECK(control.ircStart == 1 && control.crcReturned == 4);
    control = (RGNRECT){0, 0, 999, 1};
    CHECK(GpiQueryRegionRects(ps, region, &bounds, &control, NULL));
    CHECK(control.ircStart == 1 && control.crcReturned == 4 && control.crc == 2);
    bounds = (RECTL){1, 1, 2, 2};
    CHECK(GpiQueryRegionRects(ps, region, &bounds, &control, NULL));
    CHECK(control.crcReturned == 1 && control.crc == 3);
    bounds = (RECTL){12, 12, 15, 15};
    CHECK(GpiQueryRegionRects(ps, region, &bounds, &control, NULL));
    CHECK(control.crcReturned == 0 && control.crc == 1);
    control = (RGNRECT){0, 4, 0, 1};
    CHECK(!GpiQueryRegionRects(ps, region, NULL, &control, actual));
    control = (RGNRECT){1, 0, 0, 1};
    CHECK(!GpiQueryRegionRects(ps, region, NULL, &control, actual));
    control = (RGNRECT){1, 4, 0, 5};
    CHECK(!GpiQueryRegionRects(ps, region, NULL, &control, actual));
    CHECK(GpiDestroyRegion(ps, region));
}

static MRESULT OS2API page_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_ADJUSTWINDOWPOS && WinQueryWindowUShort(window, -1) == 9)
    {
        SWP *position = first;
        if (position->fl & SWP_SIZE)
            position->cx = position->cy = 0;
        return 0;
    }
    if (message == WM_PAINT)
    {
        RECTL rect;
        HPS ps = WinBeginPaint(window, 0, &rect);
        CHECK(ps);
        USHORT id = WinQueryWindowUShort(window, -1);
        CHECK(WinFillRect(ps, &rect, id == 1 ? CLR_RED : id == 3 ? CLR_GREEN : CLR_BLUE));
        CHECK(WinEndPaint(ps));
        return 0;
    }
    return WinDefWindowProc(window, message, first, second);
}

static void page_exposure(HAB hab)
{
    CHECK(WinRegisterClass(hab, "PageExposure", page_procedure, 0, 0));
    HWND parent = WinCreateWindow(HWND_DESKTOP, "PageExposure", "Pages", WS_VISIBLE, 0, 0, 80, 60,
                                  0, HWND_TOP, 1, NULL, NULL);
    HWND page = WinCreateWindow(parent, "PageExposure", "Page", WS_VISIBLE, 5, 5, 20, 20, parent,
                                HWND_TOP, 2, NULL, NULL);
    CHECK(parent && page);
    notifications(hab, page, WM_PAINT);
    HPS ps = WinGetPS(parent);
    uint32_t pixel;
    CHECK(ps && lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    HWND overlay = WinCreateWindow(parent, (PSZ)WC_STATIC, "", WS_VISIBLE | 8, 5, 5, 20, 20, parent,
                                   HWND_TOP, 4, NULL, NULL);
    CHECK(overlay);
    notifications(hab, parent, WM_PAINT);
    uint32_t framed[80 * 60], repainted[80 * 60];
    CHECK(lx_pm_raster_read(ps, 0, 0, 80, 60, framed));
    CHECK(WinInvalidateRect(page, NULL, FALSE));
    notifications(hab, parent, WM_PAINT);
    CHECK(lx_pm_raster_read(ps, 0, 0, 80, 60, repainted));
    CHECK(!memcmp(framed, repainted, sizeof framed));
    CHECK(WinDestroyWindow(overlay));
    notifications(hab, parent, WM_PAINT);
    /* Two disjoint parent rectangles produce a bounding update box spanning
       a clean child. The child's corresponding pixels must also be repainted,
       because the parent's conservative bounding fill covers that gap. */
    RECTL first_gap = {0, 0, 4, 4}, second_gap = {30, 30, 40, 40};
    CHECK(WinInvalidateRect(parent, &first_gap, TRUE));
    CHECK(WinInvalidateRect(parent, &second_gap, TRUE));
    notifications(hab, parent, WM_PAINT);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    /* Lotus explicitly sends WM_PAINT between updates. With no damage this
       must not clear the dialog and erase unchanged labels or timer text. */
    WinSendMsg(parent, WM_PAINT, 0, 0);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    RECTL corner = {0, 0, 4, 4};
    CHECK(WinInvalidateRect(parent, &corner, FALSE));
    WinSendMsg(parent, WM_PAINT, 0, 0);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    CHECK(WinInvalidateRect(parent, &corner, FALSE));
    HPS clipped = WinBeginPaint(parent, 0, NULL);
    POINTL start = {0, 0}, end = {20, 20};
    CHECK(clipped && GpiSetColor(clipped, CLR_GREEN) && GpiMove(clipped, &start) &&
          GpiLine(clipped, &end) && WinEndPaint(clipped));
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    /* A slow parent repaint must not expose its background before the child. */
    CHECK(WinInvalidateRect(parent, NULL, TRUE));
    QMSG repaint;
    CHECK(WinPeekMsg(hab, &repaint, parent, WM_PAINT, WM_PAINT, 1));
    WinDispatchMsg(hab, &repaint);
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    Window *root = NULL;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == parent)
            root = anchor->windows[i];
    CHECK(root);
    uint64_t presented = root->last_present;
    lx_host_sleep_ms(25);
    CHECK(WinPeekMsg(hab, &repaint, page, WM_PAINT, WM_PAINT, 1));
    CHECK(root->last_present == presented);
    WinDispatchMsg(hab, &repaint);
    notifications(hab, parent, WM_PAINT);
    CHECK(root->last_present != presented);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    /* Retained drawing still presents when the app deliberately defers paint. */
    CHECK(WinInvalidateRect(page, NULL, FALSE));
    CHECK(WinPeekMsg(hab, &repaint, page, WM_PAINT, WM_PAINT, 1));
    presented = root->last_present;
    lx_host_sleep_ms(25);
    CHECK(WinPeekMsg(hab, &repaint, page, WM_PAINT, WM_PAINT, 1));
    CHECK(root->last_present != presented);
    WinDispatchMsg(hab, &repaint);
    /* A resized-away child cannot receive WM_PAINT. Its retained damage must
       not hold the entire dialog black until a host exposure (Cmd-Tab). */
    HWND empty = WinCreateWindow(parent, "PageExposure", "Empty", WS_VISIBLE, 0, 0, 10, 10, parent,
                                 HWND_TOP, 9, NULL, NULL);
    CHECK(empty && WinInvalidateRect(empty, NULL, FALSE));
    CHECK(WinSetWindowPos(empty, 0, 0, 0, 10, 10, SWP_SIZE | SWP_NOREDRAW));
    Window *empty_window = NULL;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == empty)
            empty_window = anchor->windows[i];
    CHECK(empty_window && empty_window->paint_pending);
    CHECK(WinInvalidateRect(parent, NULL, TRUE));
    presented = root->last_present;
    lx_host_sleep_ms(20);
    notifications(hab, parent, WM_PAINT);
    CHECK(root->last_present != presented);
    CHECK(WinDestroyWindow(empty));
    CHECK(WinShowWindow(page, FALSE));
    notifications(hab, parent, WM_PAINT);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinShowWindow(page, TRUE));
    notifications(hab, page, WM_PAINT);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff);
    /* Word Pro switches Welcome pages with the ancestor's WS_VISIBLE style
       temporarily cleared. Newly shown controls retain damage while hidden;
       restoring QWL_STYLE must schedule that existing damage before a host
       activation/exposure arrives. */
    HWND second = WinCreateWindow(parent, "PageExposure", "Second", 0, 5, 5, 20, 20, parent,
                                  HWND_TOP, 3, NULL, NULL);
    CHECK(second);
    ULONG style = WinQueryWindowULong(parent, -2);
    for (unsigned pass = 0; pass < 2; pass++)
    {
        CHECK(WinSetWindowULong(parent, -2, style & ~WS_VISIBLE));
        CHECK(WinShowWindow(pass ? second : page, FALSE));
        CHECK(WinShowWindow(pass ? page : second, TRUE));
        CHECK(WinSetWindowULong(parent, -2, style));
        notifications(hab, parent, WM_PAINT);
        CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == (pass ? 0xffu : 0xff00u));
        uint32_t before[80 * 60], after[80 * 60];
        CHECK(lx_pm_raster_read(ps, 0, 0, 80, 60, before));
        LxHostEvent expose = {.type = LX_HOST_EVENT_EXPOSED, .window_data = root};
        CHECK(processHostEvent(anchor, &expose));
        notifications(hab, parent, WM_PAINT);
        CHECK(lx_pm_raster_read(ps, 0, 0, 80, 60, after));
        CHECK(!memcmp(before, after, sizeof before));
    }
    CHECK(WinDestroyWindow(second));
    LONG old_x = root->x, old_y = root->y;
    LxHostEvent moved = {.type = LX_HOST_EVENT_MOVED,
                         .window_data = root,
                         .x = old_x + 37,
                         .y = WinQuerySysValue(HWND_DESKTOP, 21) - old_y - root->h + 19};
    CHECK(processHostEvent(anchor, &moved));
    POINTL mapped = {0, 0};
    CHECK(WinMapWindowPoints(parent, HWND_DESKTOP, &mapped, 1));
    CHECK(mapped.x == old_x + 37 && mapped.y == old_y - 19);
    CHECK(!processHostEvent(anchor, &moved));
    CHECK(WinSetWindowPos(page, HWND_TOP, 40, 5, 20, 20, SWP_MOVE));
    notifications(hab, parent, WM_PAINT);
    CHECK(lx_pm_raster_read(ps, 10, 10, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 45, 10, 1, 1, &pixel) && pixel == 0xff);
    CHECK(WinDestroyWindow(page));
    notifications(hab, parent, WM_PAINT);
    CHECK(lx_pm_raster_read(ps, 45, 10, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinReleasePS(ps) && WinDestroyWindow(parent));
}

int main(void)
{
    HAB hab = WinInitialize(0);
    CHECK(hab);
    HMQ queue = WinCreateMsgQueue(hab, 0);
    CHECK(queue);
    page_exposure(hab);
    CHECK(WinRegisterClass(hab, "VisibleContract", WinDefWindowProc, 0, 0));
    HWND parent = WinCreateWindow(HWND_DESKTOP, "VisibleContract", "Visibility", WS_VISIBLE, 10, 10,
                                  40, 30, 0, HWND_TOP, 1, NULL, NULL);
    CHECK(parent);
    HWND child = WinCreateWindow(parent, "VisibleContract", "Child", WS_VISIBLE, 20, 10, 30, 25,
                                 parent, HWND_TOP, 2, NULL, NULL);
    CHECK(child);
    notifications(hab, child, WM_PAINT);
    CHECK(WinEnableWindowUpdate(parent, FALSE));
    CHECK(!WinIsWindowVisible(parent));
    CHECK(WinInvalidateRect(child, NULL, FALSE));
    CHECK(notifications(hab, child, WM_PAINT) == 0);
    RECTL damage;
    CHECK(WinQueryUpdateRect(child, &damage));
    CHECK(WinUpdateWindow(child) && WinQueryUpdateRect(child, &damage));
    CHECK(WinEnableWindowUpdate(parent, TRUE));
    CHECK(WinIsWindowVisible(parent));
    CHECK(notifications(hab, child, WM_PAINT) == 0);
    CHECK(WinShowWindow(parent, TRUE));
    CHECK(notifications(hab, child, WM_PAINT) == 1);
    CHECK(WinUpdateWindow(child) && !WinQueryUpdateRect(child, &damage));
    CHECK(!WinEnableWindowUpdate(0, TRUE));
    HPS ps = WinGetPS(child);
    CHECK(ps);
    enumeration(ps);
    ULONG region = GpiCreateRegion(ps, 0, NULL);
    CHECK(region);
    CHECK(WinSetVisibleRegionNotify(child, TRUE));
    CHECK(notifications(hab, child, WM_VRNENABLED) >= 1);
    CHECK(WinQueryVisibleRegion(child, region) == 2);
    RECTL actual, expected = {0, 0, 20, 20};
    CHECK(GpiQueryRegionBox(ps, region, &actual) == 2 &&
          !memcmp(&actual, &expected, sizeof actual));
    CHECK(WinSetWindowPos(parent, HWND_TOP, 0, 0, 60, 50, SWP_SIZE));
    CHECK(notifications(hab, child, WM_VRNENABLED) >= 1);
    CHECK(WinQueryVisibleRegion(child, region) == 2);
    expected = (RECTL){0, 0, 30, 25};
    CHECK(GpiQueryRegionBox(ps, region, &actual) == 2 &&
          !memcmp(&actual, &expected, sizeof actual));
    CHECK(WinShowWindow(parent, FALSE));
    CHECK(notifications(hab, child, WM_VRNDISABLED) >= 1);
    CHECK(WinQueryVisibleRegion(child, region) == 1);
    CHECK(GpiQueryRegionBox(ps, region, &actual) == 1);
    CHECK(WinShowWindow(parent, TRUE));
    CHECK(notifications(hab, child, WM_VRNENABLED) >= 1);
    CHECK(WinQueryVisibleRegion(child, region) == 2);
    CHECK(WinSetVisibleRegionNotify(child, FALSE));
    CHECK(WinShowWindow(parent, FALSE));
    CHECK(notifications(hab, child, WM_VRNDISABLED) == 0);
    CHECK(!WinSetVisibleRegionNotify(HWND_DESKTOP, TRUE));
    CHECK(!WinQueryVisibleRegion(0, region));
    CHECK(!WinQueryVisibleRegion(child, 0));
    CHECK(GpiDestroyRegion(ps, region));
    CHECK(WinReleasePS(ps));
    CHECK(WinDestroyWindow(parent));
    CHECK(WinDestroyMsgQueue(queue));
    CHECK(WinTerminate(hab));
    puts("visible region and enumeration contracts passed");
    return 0;
}
