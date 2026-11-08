/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame subclasses format SWP arrays, including extension controls and client rectangles. */
#include "os2/include/pmwin.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static PFNWP original;
static HWND extension, client;
static ULONG formatted, queries;
static RECTL last_client;

static MRESULT OS2API frame_proc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_QUERYFRAMECTLCOUNT)
    {
        queries++;
        return (MRESULT)((uintptr_t)original(window, message, first, second) + 1);
    }
    if (message == WM_FORMATFRAME)
    {
        ULONG count = (ULONG)(uintptr_t)original(window, message, first, second);
        SWP *positions = first;
        last_client = *(PRECTL)second;
        for (ULONG i = 0; i < count; i++)
            if (positions[i].hwnd == client)
                positions[i].cy -= 20;
        positions[count] = (SWP){SWP_MOVE | SWP_SIZE | SWP_SHOW,
                                 20,
                                 last_client.xRight - last_client.xLeft,
                                 last_client.yTop - 20,
                                 last_client.xLeft,
                                 HWND_TOP,
                                 extension,
                                 0,
                                 0};
        formatted++;
        /* Reentrant requests cannot recursively format the same frame. */
        WinSendMsg(window, WM_UPDATEFRAME, 0, 0);
        return (MRESULT)(uintptr_t)(count + 1);
    }
    return original(window, message, first, second);
}

int main(void)
{
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(queue);
    CHECK(WinRegisterClass(anchor, "FrameProtocolClient", WinDefWindowProc, 0, 0));
    ULONG flags = 1 | 2 | 8;
    HWND frame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "FrameProtocolClient",
                                    "Frame protocol", 0, 0, 0, &client);
    CHECK(frame && client);
    CHECK(WinSetWindowPos(frame, HWND_TOP, 0, 0, 300, 200, SWP_MOVE | SWP_SIZE));
    CHECK(WinCreateWindow(frame, (PSZ)(uintptr_t)WC_SCROLLBAR, NULL, WS_VISIBLE | 1, 0, 0, 1, 1,
                          frame, HWND_TOP, FID_VERTSCROLL, NULL, NULL));
    CHECK(WinCreateWindow(frame, (PSZ)(uintptr_t)WC_SCROLLBAR, NULL, WS_VISIBLE, 0, 0, 1, 1, frame,
                          HWND_TOP, FID_HORZSCROLL, NULL, NULL));
    extension = WinCreateWindow(frame, (PSZ)(uintptr_t)0xffff0001, "extension", WS_VISIBLE, 0, 0, 1,
                                1, frame, HWND_TOP, 0x9001, NULL, NULL);
    CHECK(extension);
    original = WinSubclassWindow(frame, frame_proc);
    CHECK(original);
    HWND title = WinWindowFromID(frame, FID_TITLEBAR);
    CLASSINFO title_class;
    CHECK(title && WinQueryClassInfo(anchor, (PSZ)(uintptr_t)WC_TITLEBAR, &title_class) &&
          title_class.cbWindowData >= 4);
    CHECK(WinSetWindowULong(title, 0, 0x12345678) && WinQueryWindowULong(title, 0) == 0x12345678);
    ULONG standard_count = (ULONG)(uintptr_t)original(frame, WM_QUERYFRAMECTLCOUNT, 0, 0);
    CHECK(standard_count == 5);
    CHECK((ULONG)(uintptr_t)WinSendMsg(frame, WM_QUERYFRAMECTLCOUNT, 0, 0) == standard_count + 1);
    SWP before, after, records[8];
    RECTL rect;
    CHECK(WinQueryWindowPos(client, &before));
    CHECK((ULONG)(uintptr_t)original(frame, WM_FORMATFRAME, records, &rect) == standard_count);
    CHECK(WinQueryWindowPos(client, &after) && before.cx == after.cx && before.cy == after.cy);
    CHECK(!original(frame, WM_FORMATFRAME, NULL, NULL));
    WinSendMsg(frame, WM_UPDATEFRAME, 0, 0);
    CHECK(formatted == 1 && queries >= 2);
    CHECK(WinQueryWindowPos(client, &after));
    CHECK(after.cx == last_client.xRight - last_client.xLeft &&
          after.cy == last_client.yTop - last_client.yBottom - 20);
    CHECK(WinQueryWindowPos(extension, &after) && after.cy == 20 &&
          after.y == last_client.yTop - 20);
    CHECK(WinSetWindowPos(frame, HWND_TOP, 0, 0, 400, 300, SWP_SIZE));
    CHECK(formatted == 2 && WinQueryWindowPos(client, &after) &&
          after.cy == last_client.yTop - last_client.yBottom - 20);
    CHECK(WinDestroyWindow(frame) && WinDestroyMsgQueue(queue) && WinTerminate(anchor));
    return 0;
}
