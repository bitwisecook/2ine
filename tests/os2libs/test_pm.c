/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame/client lifetime, queue filtering, retained drawing and timer contracts. */
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi.h"
#include "guest/handles.h"
#include "os2libs/pmwin/graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
static unsigned creates, sizes, changes;
static unsigned hook_calls;
static BOOL OS2API input_hook(HAB hab, PQMSG message, ULONG flags)
{
    CHECK(flags == 1);
    if (message->msg == WM_USER + 100)
    {
        hook_calls++;
        return TRUE;
    }
    if (message->msg == WM_USER + 101)
    {
        hook_calls++;
        message->msg++;
        message->mp1 = (MPARAM)(uintptr_t)1234;
        CHECK(WinReleaseHook(hab, 1, 1, (PFN)input_hook, 0));
    }
    return FALSE;
}
static void hook_contracts(HAB hab, HWND window)
{
    QMSG message;
    while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
    {
    }
    CHECK(WinSetHook(hab, 1, 1, (PFN)input_hook, 0));
    CHECK(WinPostMsg(window, WM_USER + 100, 0, 0));
    CHECK(WinPostMsg(window, WM_USER + 101, 0, 0));
    CHECK(WinGetMsg(hab, &message, 0, 0, 0));
    CHECK(hook_calls == 2 && message.msg == WM_USER + 102 && (uintptr_t)message.mp1 == 1234);
    CHECK(!WinReleaseHook(hab, 1, 1, (PFN)input_hook, 0));
}
static MRESULT OS2API procedure(HWND hwnd, ULONG message, MPARAM a, MPARAM b)
{
    if (message == WM_CREATE)
    {
        CREATESTRUCT *creation = b;
        CHECK(creation && creation->pCtlData == a);
        CHECK(creation->hwndParent == WinQueryWindow(hwnd, 5));
        CHECK(WinWindowFromID(creation->hwndParent, creation->id) == hwnd);
        creates++;
    }
    if (message == WM_PRESPARAMCHANGED)
        changes++;
    if (message == WM_SIZE)
        sizes++;
    return WinDefWindowProc(hwnd, message, a, b);
}
static void accelerator_contracts(HAB hab, HWND frame, HWND client)
{
    const uint8_t table[] = {1, 0, 181, 1, 17, 0, 'S', 0, 42, 0};
    ULONG handle = WinCreateAccelTable(hab, (PVOID)table);
    CHECK(handle && WinSetAccelTable(hab, handle, frame));
    CHECK(WinQueryAccelTable(hab, frame) == handle && !WinDestroyAccelTable(handle));
    uint8_t copy[10];
    CHECK(WinCopyAccelTable(handle, copy, sizeof copy) == sizeof copy &&
          !memcmp(copy, table, sizeof table));
    QMSG message = {client, WM_CHAR, (MPARAM)(uintptr_t)0x10001, (MPARAM)(uintptr_t)'s', 0,
                    {0, 0}, 0};
    CHECK(!WinTranslateAccel(hab, frame, 0, &message) && message.hwnd == client &&
          message.msg == WM_CHAR);
    message.mp1 = (MPARAM)(uintptr_t)0x10011;
    CHECK(WinTranslateAccel(hab, frame, 0, &message));
    CHECK(message.hwnd == frame && message.msg == WM_COMMAND && (uintptr_t)message.mp1 == 42 &&
          (uintptr_t)message.mp2 == 3);
    CHECK(WinSetAccelTable(hab, 0, frame) && WinDestroyAccelTable(handle));
    CHECK(!WinQueryAccelTable(hab, frame) && !WinDestroyAccelTable(handle));
}
static void menu_contracts(HWND parent)
{
    const uint8_t empty[] = {10, 0, 0, 0, 181, 1, 0, 0, 0, 0};
    HWND menu = WinCreateMenu(parent, (PVOID)empty);
    CHECK(menu);
    MENUITEM item = {-1, 1, 0, 42, 0, 123};
    CHECK((intptr_t)WinSendMsg(menu, 0x180, &item, "~Item") == 0);
    item.iPosition = 99;
    item.afAttribute = 0x4000;
    item.hItem = 456;
    CHECK(WinSendMsg(menu, 0x183, 0, &item));
    MENUITEM actual;
    CHECK(WinSendMsg(menu, 0x182, (MPARAM)(uintptr_t)42, &actual));
    CHECK(actual.iPosition == 0 && actual.afAttribute == 0x4000 && actual.hItem == 456);
    item.id = 43;
    CHECK(!WinSendMsg(menu, 0x183, 0, &item));
    CHECK(WinDestroyWindow(menu));
}
static void list_contracts(HWND parent)
{
    HWND combo = WinCreateWindow(parent, (PSZ)WC_COMBOBOX, "", 4, 0, 0, 180, 120, parent, HWND_TOP,
                                 901, NULL, NULL);
    CHECK(combo);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0004,
                                     "Lotus Word Pro") == -1);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x161, (MPARAM)(intptr_t)-1, "Lotus Word Pro") == 0);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x161, (MPARAM)(intptr_t)-1, "Plain text") == 1);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0004,
                                     "Lotus Word Pro") == 0);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0x00010006, "Lotus") == 0);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0005, "text") == 1);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0004,
                                     "lotus word pro") == -1);
    CHECK(WinSendMsg(combo, 0x164, 0, (MPARAM)(uintptr_t)TRUE));
    char text[32];
    CHECK(WinQueryWindowText(combo, sizeof text, text) == 14 && !strcmp(text, "Lotus Word Pro"));
    CHECK(WinDestroyWindow(combo));
}
static void graphics_contracts(HAB hab, HWND window)
{
    CHECK(sizeof(FATTRS) == 56);
    HDC window_dc = WinQueryWindowDC(window);
    CHECK(window_dc == WinOpenWindowDC(window));
    CHECK(!WinQueryWindowDC(0));
    HDC memory = DevOpenDC(hab, 8, "*", 0, NULL, window_dc);
    CHECK(memory && !DevOpenDC(hab, 8, "*", 0, NULL, 0xdeadbeef));
    SIZEL size = {32, 24};
    HPS ps = GpiCreatePS(hab, memory, &size, 0x5008);
    CHECK(ps && GpiQueryDevice(ps) == memory && DevCloseDC(memory) == (ULONG)-1);
    CHECK(GpiSetCp(ps, 437) && GpiQueryCp(ps) == 437 && !GpiSetCp(ps, 932));
    FATTRS attrs = {0};
    attrs.usRecordLength = sizeof attrs;
    strcpy(attrs.szFacename, "Courier");
    CHECK(GpiCreateLogFont(ps, NULL, 5, &attrs) == 2 && GpiSetCharSet(ps, 5));
    FONTMETRICS metrics;
    CHECK(GpiQueryFontMetrics(ps, sizeof metrics, &metrics) &&
          !strcmp(metrics.szFamilyname, "Courier"));
    SIZEF box = {24 * 65536, 24 * 65536};
    CHECK(GpiSetCharBox(ps, &box) && GpiQueryFontMetrics(ps, sizeof metrics, &metrics) &&
          metrics.lEmHeight == 24);
    LONG widths[2];
    CHECK(GpiQueryWidthTable(ps, 'W', 1, widths) && GpiQueryWidthTable(ps, 'i', 1, widths + 1) &&
          widths[0] == widths[1]);
    CHECK(GpiQueryNumberSetIds(ps) == 1 && !GpiSetCharSet(ps, 6));
    LONG saved = GpiSavePS(ps);
    CHECK(saved > 0 && GpiSetBackMix(ps, 2) && GpiQueryBackMix(ps) == 2);
    CHECK(GpiSetCharSet(ps, 0) && GpiRestorePS(ps, saved) && GpiQueryCharSet(ps) == 5 &&
          GpiQueryBackMix(ps) == 5);
    CHECK(!GpiRestorePS(ps, saved));
    uint8_t header[64] = {64};
    header[4] = 32;
    header[8] = 24;
    header[12] = 1;
    header[14] = 24;
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(bitmap && GpiSetBitmap(ps, bitmap) == 0 && !GpiDeleteBitmap(bitmap));
    RECTL fill = {0, 0, 32, 24};
    CHECK(WinFillRect(ps, &fill, CLR_RED));
    uint32_t pixel;
    CHECK(lx_pm_raster_read(ps, 3, 4, 1, 1, &pixel) && pixel == 0xff0000);
    RECTL outer = {0, 0, 16, 16}, hole = {4, 4, 12, 12};
    ULONG a = GpiCreateRegion(ps, 1, &outer), b = GpiCreateRegion(ps, 1, &hole),
          clipped = GpiCreateRegion(ps, 0, NULL);
    CHECK(a && b && clipped && GpiCombineRegion(ps, clipped, a, b, 7) == 3);
    RECTL bound;
    CHECK(GpiQueryRegionBox(ps, clipped, &bound) == 3 && WinEqualRect(hab, &bound, &outer));
    ULONG previous = 99;
    CHECK(GpiSetClipRegion(ps, clipped, &previous) == 3 && previous == 0 &&
          !GpiDestroyRegion(ps, clipped));
    CHECK(GpiRectVisible(ps, &hole) == 1 && GpiRectVisible(ps, &outer) == 2);
    CHECK(WinFillRect(ps, &outer, CLR_BLUE));
    CHECK(lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel == 0x0000ff);
    CHECK(lx_pm_raster_read(ps, 8, 8, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(GpiSetClipRegion(ps, 0, &previous) == 2 && previous == clipped);
    CHECK(GpiDestroyRegion(ps, clipped) && GpiDestroyRegion(ps, a) && GpiDestroyRegion(ps, b));
    CHECK(WinFillRect(ps, &outer, CLR_RED));

    CHECK(GpiSetBitmapId(ps, bitmap, 9) && !GpiSetBitmapId(ps, bitmap, 9));
    CHECK(GpiQueryBitmapHandle(ps, 9) == bitmap && GpiQueryNumberSetIds(ps) == 2);
    LONG types[2], ids[2];
    char names[16];
    CHECK(GpiQuerySetIds(ps, 2, types, names, ids) && ids[0] == 5 && types[0] == 6 && ids[1] == 9 &&
          types[1] == 7);
    uint8_t queried[64] = {64};
    CHECK(GpiQueryBitmapInfoHeader(bitmap, queried) && queried[4] == 32 && queried[8] == 24 &&
          queried[14] == 24);
    memset(queried, 0xa5, sizeof queried);
    CHECK(GpiQueryBitmapParameters(bitmap, queried) && queried[0] == 12 && queried[4] == 32 &&
          queried[12] == 0xa5);
    HPS target = WinGetPS(window);
    POINTL points[3] = {{0, 0}, {32, 24}, {0, 0}};
    CHECK(GpiBitBlt(target, ps, 3, points, 0xcc, 2));
    CHECK(lx_pm_raster_read(target, 3, 4, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinReleasePS(target));
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteSetId(ps, 9) && GpiDeleteBitmap(bitmap));
    CHECK(!GpiDeleteBitmap(bitmap) && GpiDestroyPS(ps) && DevCloseDC(memory) == 0);
    CHECK(!DevQueryCaps(memory, 0, 1, widths));
}
int main(void)
{
    CHECK(sizeof(SWP) == 36 && sizeof(POINTL) == 8 && sizeof(RECTL) == 16 && sizeof(SIZEL) == 8);
    HAB hab = WinInitialize(0);
    CHECK(hab);
    HMQ hmq = WinCreateMsgQueue(hab, 0);
    CHECK(hmq);
    CHECK(WinRegisterClass(0, "test", procedure, CS_SIZEREDRAW, 8));
    ULONG flags = 0;
    HWND client;
    HWND frame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "test", "PM contracts", 0, 0,
                                    0, &client);
    CHECK(frame && client && creates == 1);
    CHECK(WinWindowFromID(frame, FID_CLIENT) == client);
    CHECK(WinSetWindowPtr(frame, 0, (void *)(uintptr_t)0x12345678));
    CHECK((uintptr_t)WinQueryWindowPtr(frame, 0) == 0x12345678);
    CHECK(!WinWindowFromID(frame, FID_MENU));
    CHECK(WinSetWindowULong(client, 0, 0x12345678) && WinQueryWindowULong(client, 0) == 0x12345678);
    CHECK(WinSetWindowPtr(client, 4, (void *)(uintptr_t)0xfedcba98) &&
          (uintptr_t)WinQueryWindowPtr(client, 4) == 0xfedcba98);
    CHECK(!WinSetWindowULong(client, 5, 42) && !WinSetWindowULong(client, -1, 42));
    CHECK(WinQueryWindowULong(client, -4) == hmq);
    CHECK(WinSubclassWindow(client, WinDefWindowProc) == procedure);
    CHECK(WinSubclassWindow(client, procedure) == WinDefWindowProc);
    CHECK(WinQueryWindowULong(client, -3) == (ULONG)(uintptr_t)procedure);
    CHECK(WinSetWindowPtr(client, -3, (PVOID)(uintptr_t)0x12345678));
    CHECK((uintptr_t)WinQueryWindowPtr(client, -3) == 0x12345678);
    CHECK(WinSubclassWindow(client, procedure) == (PFNWP)(uintptr_t)0x12345678);
    CHECK(WinQuerySysColor(HWND_DESKTOP, -20, 0) == 0xffffff);
    CHECK(!WinQuerySysColor(HWND_DESKTOP, -51, 0));
    CHECK((WinGetLastError(hab) & 0xffff) == PMERR_PARAMETER_OUT_OF_RANGE);
    RECTL desktop;
    CHECK(WinQueryWindowRect(HWND_DESKTOP, &desktop));
    CHECK(WinQuerySysValue(HWND_DESKTOP, 20) == desktop.xRight);
    CHECK(WinQuerySysValue(HWND_DESKTOP, 21) == desktop.yTop);
    HPS screen = WinGetScreenPS(HWND_DESKTOP);
    CHECK(screen && sizeof(FONTMETRICS) == 228);
    LONG requested = 0;
    CHECK(GpiQueryFonts(screen, 1, "Helv", &requested, 228, NULL) == 4 && requested == 0);
    FONTMETRICS metrics[2];
    requested = 2;
    CHECK(GpiQueryFonts(screen, 1, "Helv", &requested, 228, metrics) == 2 && requested == 2);
    CHECK(!strcmp(metrics[0].szFamilyname, "Helv") && metrics[0].lMatch == 1);
    CHECK(metrics[0].lMaxAscender > 0 && metrics[0].lAveCharWidth > 0);
    CHECK(!(metrics[0].fsSelection & 0x20) && (metrics[1].fsSelection & 0x20));
    requested = 0;
    CHECK(!GpiQueryFonts(screen, 1, "Missing face", &requested, 228, NULL));
    CHECK(GpiQueryFontMetrics(screen, 228, metrics) && metrics[0].lMatch == 1);
    LONG capabilities[2];
    CHECK(DevQueryCaps(HWND_DESKTOP, 8, 2, capabilities) && capabilities[0] == 3780 &&
          capabilities[1] == 3780);
    CHECK(!DevQueryCaps(HWND_DESKTOP, 41, 2, capabilities));
    CHECK(GpiQueryColorIndex(screen, 0, 0xff0000) == CLR_RED);
    CHECK(GpiQueryColorIndex(screen, 0, 0x0100fe) == CLR_BLUE);
    CHECK(GpiQueryRGBColor(screen, 0, CLR_RED) == 0xff0000);
    CHECK(GpiQueryRGBColor(screen, 1, CLR_RED) == -255);
    CHECK(WinReleasePS(screen) && !WinReleasePS(screen));
    CHECK(WinSetWindowPos(frame, HWND_TOP, 10, 10, 64, 48, SWP_MOVE | SWP_SIZE | SWP_SHOW));
    graphics_contracts(hab, client);
    accelerator_contracts(hab, frame, client);
    menu_contracts(frame);
    list_contracts(frame);
    hook_contracts(hab, client);
    SWP position;
    RECTL rectangle;
    CHECK(WinQueryWindowPos(client, &position) && position.cx == 64 && position.cy == 48);
    CHECK(WinQueryWindowRect(client, &rectangle) && rectangle.xRight == 64 && sizes == 1);
    char classname[16];
    CHECK(WinQueryClassName(client, sizeof classname, classname) == 4 &&
          !strcmp(classname, "test"));
    CHECK(WinQueryAnchorBlock(client) == hab && WinQueryWindow(client, 5) == frame);
    CHECK(WinQueryWindow(client, 4) == frame);
    HENUM enumeration = WinBeginEnumWindows(frame);
    CHECK(enumeration);
    HWND transient =
        WinCreateWindow(frame, "test", NULL, 0, 0, 0, 1, 1, frame, HWND_TOP, 900, NULL, NULL);
    CHECK(transient && WinGetNextWindow(enumeration) == client);
    CHECK(!WinGetNextWindow(enumeration) && WinGetNextWindow(enumeration) == client);
    CHECK(WinEndEnumWindows(enumeration) && !WinEndEnumWindows(enumeration));
    CHECK(WinQueryWindow(transient, 0) == client && WinQueryWindow(client, 1) == transient);
    CHECK(WinEnableWindow(transient, FALSE) && !WinIsWindowEnabled(transient));
    CHECK(WinEnableWindow(transient, TRUE) && WinIsWindowEnabled(transient));
    CHECK(WinPostMsg(transient, WM_USER + 90, 0, 0) && WinDestroyWindow(transient));
    CHECK(WinPostMsg(client, WM_USER + 90, (MPARAM)(uintptr_t)3, 0));
    QMSG retired;
    CHECK(WinGetMsg(hab, &retired, 0, WM_USER + 90, WM_USER + 90) && retired.hwnd == client);
    ULONG color = CLR_RED, found = 0, result = 0;
    unsigned before = changes;
    CHECK(WinSetPresParam(frame, 2, 4, &color) && changes == before + 1);
    CHECK(WinQueryPresParam(client, 2, 0, &found, 4, &result, 2) == 4 && found == 2 &&
          result == 0xff0000);
    CHECK(!WinQueryPresParam(client, 2, 0, &found, 4, &result, 1));
    CHECK(WinRemovePresParam(frame, 2) && !WinRemovePresParam(frame, 2));
    CHECK(WinShowWindow(frame, FALSE) && !WinIsWindowVisible(frame));
    CHECK(WinShowWindow(frame, TRUE) && WinIsWindowVisible(frame));
    HDC dc = WinOpenWindowDC(client);
    SIZEL size = {64, 48};
    HPS ps = GpiCreatePS(hab, dc, &size, PU_PELS | GPIA_ASSOC);
    CHECK(ps && GpiSetColor(ps, CLR_RED) && GpiSetDrawingMode(ps, DM_DRAW));
    char font_name[] = "18.Courier Bold";
    CHECK(WinSetPresParam(frame, 15, sizeof font_name, font_name));
    CHECK(GpiQueryFontMetrics(ps, 228, metrics));
    CHECK(metrics[0].lEmHeight == 24 && (metrics[0].fsType & 1) && (metrics[0].fsSelection & 0x20));
    POINTL text_box[5], narrow_box[5];
    CHECK(GpiQueryTextBox(ps, 2, "WW", 5, text_box) && GpiQueryTextBox(ps, 2, "ii", 5, narrow_box));
    CHECK(text_box[4].x == narrow_box[4].x && text_box[0].y > 0 && text_box[1].y <= 0);
    CHECK(WinRemovePresParam(frame, 15));
    CHECK(GpiQueryTextBox(ps, 2, "WW", 5, text_box) && GpiQueryTextBox(ps, 2, "ii", 5, narrow_box));
    CHECK(text_box[4].x > narrow_box[4].x);

    POINTL point = {0, 0};
    CHECK(GpiMove(ps, &point));
    point = (POINTL){63, 47};
    CHECK(GpiLine(ps, &point));
    CHECK(GpiSetPel(ps, &point));
    CHECK(!GpiSetDrawingMode(ps, 99));
    HPS paint = WinBeginPaint(client, 0, &rectangle);
    CHECK(paint && paint != ps);
    CHECK(WinFillRect(paint, &rectangle, CLR_BLACK) && WinEndPaint(paint));
    CHECK(GpiMove(ps, &point));
    /* Growing the registry must retain window and presentation-space identities. */
    for (unsigned i = 0; i < 40; i++)
        CHECK(
            WinCreateWindow(frame, "test", NULL, 0, 0, 0, 1, 1, 0, HWND_TOP, 100 + i, NULL, NULL));
    CHECK(WinQueryWindowRect(client, &rectangle) && GpiMove(ps, &point));
    CHECK(WinPostMsg(client, WM_USER, (MPARAM)(uintptr_t)1, 0));
    CHECK(WinPostMsg(client, WM_USER + 1, (MPARAM)(uintptr_t)2, 0));
    QMSG q;
    CHECK(WinGetMsg(hab, &q, client, WM_USER + 1, WM_USER + 1) && (uintptr_t)q.mp1 == 2);
    CHECK(WinPostMsg(client, WM_USER + 2, (MPARAM)(uintptr_t)3, 0));
    CHECK(WinGetMsg(hab, &q, client, WM_USER, WM_USER) && (uintptr_t)q.mp1 == 1);
    CHECK(WinGetMsg(hab, &q, client, WM_USER + 2, WM_USER + 2) && (uintptr_t)q.mp1 == 3);
    ULONG timer = WinStartTimer(hab, client, 0, 5);
    CHECK(timer);
    CHECK(WinGetMsg(hab, &q, client, WM_TIMER, WM_TIMER) && (uintptr_t)q.mp1 == timer);
    CHECK(WinStopTimer(hab, client, timer) && !WinStopTimer(hab, client, timer));
    CHECK(WinDestroyWindow(frame));
    CHECK(GpiQueryDevice(ps) == 0 && GpiMove(ps, &point) && GpiDestroyPS(ps));
    HENUM abandoned = WinBeginEnumWindows(HWND_DESKTOP);
    CHECK(abandoned);
    CHECK(WinDestroyMsgQueue(hmq) && WinTerminate(hab));
    LxHandleBorrow borrow = {0};
    CHECK(!lx_handle_borrow(abandoned, LX_HANDLE_PM_ENUM, &borrow));
    puts("PM frame, stable handles, retained drawing, queue and timer contracts passed");
    return 0;
}
