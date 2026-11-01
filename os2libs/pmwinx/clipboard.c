/* Public domain. No rights reserved. Wholly AI-authored. */
/* Clipboard viewers form a checked notification chain within this worker. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
typedef struct Viewer
{
    ULONG window;
    struct Viewer *next;
} Viewer;
static Viewer *viewers;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
ULONG SetClipboardViewer(ULONG window)
{
    if (!open32FindWindow(window))
    {
        open32SetError(1400);
        return 0;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    for (Viewer *viewer = viewers; viewer; viewer = viewer->next)
        if (viewer->window == window)
        {
            ULONG next = viewer->next ? viewer->next->window : 0;
            lx_host_monitor_unlock(monitor);
            return next;
        }
    Viewer *viewer = calloc(1, sizeof *viewer);
    if (!viewer)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(8);
        return 0;
    }
    ULONG previous = viewers ? viewers->window : 0;
    viewer->window = window;
    viewer->next = viewers;
    viewers = viewer;
    lx_host_monitor_unlock(monitor);
    SendMessage(window, 0x308, 0, 0);
    return previous;
}
ULONG GetClipboardViewer(void)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    ULONG window = viewers ? viewers->window : 0;
    lx_host_monitor_unlock(monitor);
    return window;
}
BOOL ChangeClipboardChain(ULONG window, ULONG next)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Viewer **link = &viewers;
    while (*link && (*link)->window != window)
        link = &(*link)->next;
    if (!*link)
    {
        lx_host_monitor_unlock(monitor);
        return FALSE;
    }
    Viewer *removed = *link;
    *link = removed->next;
    ULONG head = viewers ? viewers->window : 0;
    free(removed);
    lx_host_monitor_unlock(monitor);
    if (head && head != window)
        SendMessage(head, 0x30D, window, next);
    return TRUE;
}
void open32ClipboardWindowDestroyed(ULONG window)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Viewer **link = &viewers;
    while (*link && (*link)->window != window)
        link = &(*link)->next;
    if (*link)
    {
        Viewer *removed = *link;
        *link = removed->next;
        free(removed);
    }
    lx_host_monitor_unlock(monitor);
}
void open32ClipboardCleanup(void)
{
    while (viewers)
    {
        Viewer *viewer = viewers;
        viewers = viewer->next;
        free(viewer);
    }
}
