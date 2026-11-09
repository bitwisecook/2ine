/* Public domain. No rights reserved. Wholly AI-authored. */
/* One queue-thread driver schedules Windows timer IDs without truncating them to PM USHORTs. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmp.h"
#include "host/window.h"

typedef struct Open32Timer
{
    struct Open32Timer *next;
    ULONG window, id, callback, interval;
    uint64 due;
} Open32Timer;

static _Thread_local Open32Timer *timers;
static _Thread_local ULONG driver, next_id;

static void tick(void)
{
    uint64 now = lx_host_video_ticks_ms();
    for (Open32Timer *timer = timers; timer; timer = timer->next)
        if (now >= timer->due)
        {
            if (open32PostTimer(timer->window, timer->id, timer->callback))
                timer->due = now + timer->interval;
        }
}

ULONG SetTimer(ULONG window, ULONG id, ULONG milliseconds, ULONG callback)
{
    TRACE_NATIVE("SetTimer(%u, %u, %u, %x)", (uint)window, (uint)id, (uint)milliseconds,
                 (uint)callback);
    if (!open32Presentation())
        return 0;
    if (window && !open32FindWindow(window) && !open32NativeDialogWindow(window))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    if (callback && !open32ValidProcedure(callback))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Open32Timer *timer;
    for (timer = timers; timer; timer = timer->next)
        if (timer->window == window && timer->id == id)
            break;
    if (!timer)
    {
        if (!window || !id)
        {
            do
            {
                if (!++next_id)
                    ++next_id;
                for (timer = timers; timer && (timer->window != window || timer->id != next_id);
                     timer = timer->next)
                {
                }
            } while (timer);
            id = next_id;
        }
        timer = calloc(1, sizeof *timer);
        if (!timer)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        if (!driver)
            driver = lx_pm_timer_create(HWND_OBJECT, 10, tick);
        if (!driver)
        {
            free(timer);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        timer->window = window;
        timer->id = id;
        timer->next = timers;
        timers = timer;
    }
    timer->callback = callback;
    timer->interval = milliseconds < 10 ? 10 : milliseconds > INT32_MAX ? INT32_MAX : milliseconds;
    timer->due = lx_host_video_ticks_ms() + timer->interval;
    return timer->id;
}

BOOL KillTimer(ULONG window, ULONG id)
{
    for (Open32Timer **link = &timers; *link; link = &(*link)->next)
        if ((*link)->window == window && (*link)->id == id)
        {
            Open32Timer *timer = *link;
            *link = timer->next;
            free(timer);
            if (!timers && driver)
            {
                lx_pm_timer_stop(driver);
                driver = 0;
            }
            return TRUE;
        }
    return FALSE;
}

void open32TimersWindowDestroyed(ULONG window)
{
    Open32Timer *timer = timers;
    while (timer)
    {
        Open32Timer *next = timer->next;
        if (timer->window == window)
            KillTimer(window, timer->id);
        timer = next;
    }
}

void open32TimersCleanup(void)
{
    while (timers)
        KillTimer(timers->window, timers->id);
}
