/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM timers are dispatched on the owning message-queue thread. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/timers.h"
ULONG WinStartTimer(HAB hab, HWND hwnd, ULONG id, ULONG milliseconds)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !getWindowFromHWND(anchor, hwnd) || !milliseconds || id > 0xffff)
        return 0;
    if (!id)
    {
        for (id = 1; id < 0x8000; id++)
        {
            PmTimer *t;
            for (t = anchor->timers; t && !(t->hwnd == hwnd && t->id == id); t = t->next)
            {
            }
            if (!t)
                break;
        }
        if (id == 0x8000)
            return 0;
    }
    PmTimer *timer;
    for (timer = anchor->timers; timer; timer = timer->next)
        if (timer->id == id && timer->hwnd == hwnd)
            break;
    if (!timer)
    {
        timer = calloc(1, sizeof *timer);
        if (!timer)
            return 0;
        timer->next = anchor->timers;
        anchor->timers = timer;
    }
    timer->id = id;
    timer->hwnd = hwnd;
    timer->interval = milliseconds;
    timer->due = lx_host_video_ticks_ms() + milliseconds;
    return id;
}
BOOL WinStopTimer(HAB hab, HWND hwnd, ULONG id)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || id > 0xFFFF)
        return FALSE;
    for (PmTimer **link = &anchor->timers; *link; link = &(*link)->next)
    {
        PmTimer *timer = *link;
        if (timer->hwnd == hwnd && timer->id == id)
        {
            *link = timer->next;
            free(timer);
            return TRUE;
        }
    }
    return FALSE;
}
int pumpTimers(AnchorBlock *anchor)
{
    uint64_t now = lx_host_video_ticks_ms();
    int wait = -1;
    for (PmTimer *timer = anchor->timers; timer; timer = timer->next)
    {
        if (timer->due <= now && timer->callback)
        {
            timer->due = now + timer->interval;
            timer->callback();
        }
        else if (timer->due <= now)
        {
            int pending = 0;
            for (MessageQueueItem *m = anchor->message_queue.head; m; m = m->next)
                if (m->qmsg.hwnd == timer->hwnd && m->qmsg.msg == WM_TIMER &&
                    (uintptr_t)m->qmsg.mp1 == timer->id)
                    pending = 1;
            if (!pending)
                postTimestampedMsg(anchor, timer->hwnd, WM_TIMER, (MPARAM)(uintptr_t)timer->id, 0,
                                   (ULONG)now);
            timer->due = now + timer->interval;
        }
        uint64_t delay = timer->due - now;
        int interval = delay > INT32_MAX ? INT32_MAX : (int)delay;
        if (wait < 0 || interval < wait)
            wait = interval;
    }
    return wait;
}

ULONG lx_pm_timer_create(HWND owner, ULONG interval, void (*callback)(void))
{
    static _Thread_local ULONG next = 0x10000;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !getWindowFromHWND(anchor, owner) || !interval || !callback || !next)
        return 0;
    PmTimer *timer = calloc(1, sizeof *timer);
    if (!timer)
        return 0;
    timer->id = next++;
    timer->hwnd = owner;
    timer->interval = interval;
    timer->callback = callback;
    timer->due = lx_host_video_ticks_ms() + interval;
    timer->next = anchor->timers;
    anchor->timers = timer;
    return timer->id;
}
BOOL lx_pm_timer_stop(ULONG id)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || id < 0x10000)
        return FALSE;
    for (PmTimer **slot = &anchor->timers; *slot; slot = &(*slot)->next)
        if ((*slot)->id == id && (*slot)->callback)
        {
            PmTimer *timer = *slot;
            *slot = timer->next;
            free(timer);
            return TRUE;
        }
    return FALSE;
}
