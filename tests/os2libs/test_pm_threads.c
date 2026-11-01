/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window posts cross thread ownership without touching a foreign queue directly. */
#include "os2libs/pmwin.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "hal/host_thread.h"
#include "hal/host_sync.h"
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
typedef struct HostDelivery
{
    LxHostMonitor *monitor;
    Window *window;
    BOOL ready;
    BOOL sent;
} HostDelivery;
static void *receive_host_event(void *argument)
{
    HostDelivery *delivery = argument;
    HAB hab = WinInitialize(0);
    HMQ queue = WinCreateMsgQueue(hab, 0);
    HWND window = WinCreateWindow(HWND_DESKTOP, (PSZ)WC_FRAME, "event owner", 0, 0, 0, 32, 32, 0,
                                  HWND_TOP, 0, NULL, NULL);
    CHECK(hab && queue && window);
    lx_host_monitor_lock(delivery->monitor);
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == window)
            delivery->window = anchor->windows[i];
    CHECK(delivery->window);
    delivery->ready = TRUE;
    lx_host_monitor_wake_all(delivery->monitor);
    while (!delivery->sent)
        CHECK(lx_host_monitor_wait(delivery->monitor, 5000) == LX_HOST_LOCK_OK);
    lx_host_monitor_unlock(delivery->monitor);
    QMSG message;
    CHECK(WinGetMsg(hab, &message, window, WM_CLOSE, WM_CLOSE));
    CHECK(message.hwnd == window && message.msg == WM_CLOSE);
    CHECK(WinDestroyWindow(window) && WinDestroyMsgQueue(queue) && WinTerminate(hab));
    lxHostThreadCleanup();
    CHECK(!lxHostThreadState()->anchor_block);
    return NULL;
}
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
    HostDelivery delivery = {0};
    delivery.monitor = lx_host_monitor_create();
    CHECK(delivery.monitor);
    thread = lx_host_thread_create(receive_host_event, &delivery, NULL);
    CHECK(thread);
    lx_host_monitor_lock(delivery.monitor);
    while (!delivery.ready)
        CHECK(lx_host_monitor_wait(delivery.monitor, 5000) == LX_HOST_LOCK_OK);
    LxHostEvent event = {.type = LX_HOST_EVENT_CLOSE, .window_data = delivery.window};
    CHECK(lx_pm_host_event(&event));
    CHECK(!WinPeekMsg(hab, &message, 0, WM_CLOSE, WM_CLOSE, 1));
    delivery.sent = TRUE;
    lx_host_monitor_wake_all(delivery.monitor);
    lx_host_monitor_unlock(delivery.monitor);
    CHECK(!lx_host_thread_join(thread, NULL));
    lx_host_monitor_destroy(delivery.monitor);
    HPS space = WinGetPS(client);
    RECTL rectangle = {0, 0, 10, 10};
    CHECK(space && WinFillRect(space, &rectangle, CLR_RED));
    WinPeekMsg(hab, &message, 0, 0, 0, 0);
    CHECK(WinReleasePS(space));
    CHECK(WinDestroyWindow(frame));
    CHECK(WinTerminate(hab));
    puts("PM client activation and cross-thread message lifetime passed");
    return 0;
}
