/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame/client lifetime, queue filtering, retained drawing and timer contracts. */
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned creates, sizes;
static MRESULT OS2API procedure(HWND hwnd, ULONG message, MPARAM a, MPARAM b)
{
    if (message == WM_CREATE)
        creates++;
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
    CHECK(screen);
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
    HDC dc = WinOpenWindowDC(client);
    SIZEL size = {64, 48};
    HPS ps = GpiCreatePS(hab, dc, &size, PU_PELS | GPIA_ASSOC);
    CHECK(ps && GpiSetColor(ps, CLR_RED) && GpiSetDrawingMode(ps, DM_DRAW));
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
