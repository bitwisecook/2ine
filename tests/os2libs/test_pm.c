/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame/client lifetime, queue filtering, retained drawing and timer contracts. */
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi.h"
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
    CHECK(!GpiMove(ps, &point));
    CHECK(WinDestroyMsgQueue(hmq) && WinTerminate(hab));
    puts("PM frame, stable handles, retained drawing, queue and timer contracts passed");
    return 0;
}
