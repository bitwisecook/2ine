/* Public domain. No rights reserved. Wholly AI-authored. */
/* Foreign posts are staged under a lock and consumed by the owning PM thread. */
#include "os2libs/pmwin/internal.h"
#include "hal/host_sync.h"
typedef struct Target
{
    struct Target *next;
    HWND window;
    AnchorBlock *anchor;
} Target;
typedef struct Pending
{
    struct Pending *next;
    AnchorBlock *anchor;
    QMSG message;
} Pending;
static LxHostOnce initialized = LX_HOST_ONCE_INIT;
static LxHostMutex *lock;
static Target *targets;
static Pending *pending, *tail;
static uint32 next_window = FIRST_HWND_VALUE - 1;
static void initialize(void)
{
    lock = lx_host_mutex_create(0, 0);
}
HWND registerWindowMessages(AnchorBlock *anchor)
{
    lx_host_once(&initialized, initialize);
    Target *target = lock ? calloc(1, sizeof *target) : NULL;
    if (!target)
        return 0;
    target->window = lx_host_atomic_increment(&next_window);
    if (target->window < FIRST_HWND_VALUE || target->window >= 0xffff0000u)
    {
        free(target);
        return 0;
    }
    target->anchor = anchor;
    lx_host_mutex_lock(lock, UINT32_MAX);
    target->next = targets;
    targets = target;
    lx_host_mutex_unlock(lock);
    return target->window;
}
void unregisterWindowMessages(HWND window)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target **link = &targets; *link; link = &(*link)->next)
        if ((*link)->window == window)
        {
            Target *target = *link;
            *link = target->next;
            free(target);
            break;
        }
    Pending **link = &pending;
    tail = NULL;
    while (*link)
    {
        Pending *item = *link;
        if (item->message.hwnd == window)
        {
            *link = item->next;
            free(item);
        }
        else
        {
            tail = item;
            link = &item->next;
        }
    }
    lx_host_mutex_unlock(lock);
}
BOOL postForeignMessage(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (!lock || !window)
        return FALSE;
    Pending *item = calloc(1, sizeof *item);
    if (!item)
        return FALSE;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target; target = target->next)
        if (target->window == window)
        {
            item->anchor = target->anchor;
            break;
        }
    if (!item->anchor)
    {
        lx_host_mutex_unlock(lock);
        free(item);
        return FALSE;
    }
    item->message = (QMSG){window, message, first, second, currentSystemTicks(), {0, 0}, 0};
    if (tail)
        tail->next = item;
    else
        pending = item;
    tail = item;
    lx_host_mutex_unlock(lock);
    LxHostEvent wake = {0};
    lx_host_video_push(&wake);
    return TRUE;
}
void drainForeignMessages(AnchorBlock *anchor)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    Pending **link = &pending;
    tail = NULL;
    while (*link)
    {
        Pending *item = *link;
        if (item->anchor == anchor)
        {
            *link = item->next;
            postMessage(anchor, &item->message);
            free(item);
        }
        else
        {
            tail = item;
            link = &item->next;
        }
    }
    lx_host_mutex_unlock(lock);
}
