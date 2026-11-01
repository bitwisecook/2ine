/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window posts cross thread ownership without touching a foreign queue directly. */
#include "os2libs/pmwin.h"
#include "hal/host_thread.h"
#include <stdint.h>
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
static unsigned activated;
static MRESULT OS2API procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_ACTIVATE && (uintptr_t)first)
        activated++;
    return WinDefWindowProc(window, message, first, second);
}
static void *post(void *argument)
{
    HWND window = (HWND)(uintptr_t)argument;
    for (unsigned i = 0; i < 1000; i++)
        CHECK(WinPostMsg(window, WM_USER + 1, (MPARAM)(uintptr_t)i, 0));
    return NULL;
}
static void *stale(void *argument)
{
    CHECK(!WinPostMsg((HWND)(uintptr_t)argument, WM_USER + 1, 0, 0));
    return NULL;
}
int main(void)
{
    HAB hab = WinInitialize(0);
    CHECK(hab && WinCreateMsgQueue(hab, 0));
    CHECK(WinRegisterClass(hab, "ThreadClient", procedure, 0, 0));
    ULONG flags = 0;
    HWND client;
    HWND frame =
        WinCreateStdWindow(HWND_DESKTOP, 0, &flags, "ThreadClient", "posts", 0, 0, 0, &client);
    CHECK(frame && client);
    CHECK(WinSetWindowPos(frame, HWND_TOP, 10, 10, 100, 100,
                          SWP_MOVE | SWP_SIZE | SWP_SHOW | SWP_ACTIVATE));
    CHECK(WinQueryFocus(HWND_DESKTOP) == client && activated);
    LxHostThread *thread = lx_host_thread_create(post, (void *)(uintptr_t)client, NULL);
    CHECK(thread && !lx_host_thread_join(thread, NULL));
    QMSG message;
    unsigned count = 0;
    while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
        if (message.msg == WM_USER + 1)
        {
            CHECK(message.hwnd == client && (uintptr_t)message.mp1 == count);
            count++;
        }
    CHECK(count == 1000);
    HWND old_client = client;
    CHECK(WinDestroyWindow(frame));
    thread = lx_host_thread_create(stale, (void *)(uintptr_t)old_client, NULL);
    CHECK(thread && !lx_host_thread_join(thread, NULL));
    frame = WinCreateStdWindow(HWND_DESKTOP, 0, &flags, "ThreadClient", "new", 0, 0, 0, &client);
    CHECK(frame && client && client != old_client);
    CHECK(!WinPostMsg(old_client, WM_USER + 1, 0, 0));
    CHECK(WinDestroyWindow(frame));
    CHECK(WinTerminate(hab));
    puts("PM client activation and cross-thread message lifetime passed");
    return 0;
}
