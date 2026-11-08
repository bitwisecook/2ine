/* Public domain. No rights reserved. Wholly AI-authored. */
/* A latest-frame mailbox transfers ownership without copying decoded pixels.
   Only the owning PM thread touches renderer resources or window geometry. */
#include "os2/ps/pmwin/internal.h"
#include "os2/ps/pmwin/graphics.h"
#include "os2/ps/pmwin/video_frames.h"
#include "host/sync.h"

typedef struct VideoTarget
{
    struct VideoTarget *next;
    Window *window;
    AnchorBlock *anchor;
    LxHostVideoFrame frame;
    RECTL destination;
    void *owner;
    void (*release)(void *);
    BOOL failed;
} VideoTarget;

static VideoTarget *targets;
static LxHostMutex *lock;
static LxHostOnce once = LX_HOST_ONCE_INIT;

static void initialize(void)
{
    lock = lx_host_mutex_create(0, 0);
}

static void clear(VideoTarget *target)
{
    if (target->release)
        target->release(target->owner);
    target->owner = NULL;
    target->release = NULL;
}

void *lx_pm_video_target(HWND handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || window->destroying || !window->heavy.window)
        return NULL;
    lx_host_once(&once, initialize);
    VideoTarget *target = lock ? calloc(1, sizeof *target) : NULL;
    if (!target)
        return NULL;
    target->window = window;
    target->anchor = anchor;
    lx_host_mutex_lock(lock, UINT32_MAX);
    target->next = targets;
    targets = target;
    lx_host_mutex_unlock(lock);
    return target;
}

BOOL lx_pm_video_submit(void *context, const LxHostVideoFrame *frame, const RECTL *destination,
                        void *owner, void (*release)(void *))
{
    if (!context || !lock || !frame || !destination || !release ||
        destination->xRight <= destination->xLeft || destination->yTop <= destination->yBottom ||
        (int64_t)destination->xRight - destination->xLeft > INT32_MAX ||
        (int64_t)destination->yTop - destination->yBottom > INT32_MAX)
    {
        if (release)
            release(owner);
        return FALSE;
    }
    lx_host_mutex_lock(lock, UINT32_MAX);
    VideoTarget *target = context;
    clear(target);
    BOOL valid = target->window != NULL && !target->failed;
    if (valid)
    {
        target->frame = *frame;
        target->destination = *destination;
        target->owner = owner;
        target->release = release;
    }
    lx_host_mutex_unlock(lock);
    if (!valid)
        release(owner);
    else
    {
        LxHostEvent event = {.type = LX_HOST_EVENT_OTHER};
        lx_host_video_push(&event);
    }
    return valid;
}

BOOL lx_pm_video_ready(void *context)
{
    if (!context || !lock)
        return FALSE;
    lx_host_mutex_lock(lock, UINT32_MAX);
    VideoTarget *target = context;
    BOOL ready = target->window && !target->failed;
    lx_host_mutex_unlock(lock);
    return ready;
}

void lx_pm_video_release(void *context)
{
    if (!context || !lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (VideoTarget **link = &targets; *link; link = &(*link)->next)
        if (*link == context)
        {
            VideoTarget *target = *link;
            *link = target->next;
            clear(target);
            free(target);
            break;
        }
    lx_host_mutex_unlock(lock);
}

void lx_pm_video_pump(void *context)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (VideoTarget *target = targets; target; target = target->next)
    {
        if (target->anchor != context || !target->window || !target->release)
            continue;
        Window *window = target->window;
        HPS ps = WinGetPS(window->hwnd);
        PmRegion visible = {0};
        if (ps && lx_pm_window_clip(ps, NULL, &visible))
        {
            LxHostRect origin;
            calcHeavyweightRect(window, &origin);
            RECTL rect = target->destination;
            int64_t x = (int64_t)origin.x + rect.xLeft;
            int64_t y = (int64_t)origin.y + origin.h - rect.yTop;
            if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
                target->failed = TRUE;
            LxHostRect destination = {(int)x, (int)y, rect.xRight - rect.xLeft,
                                      rect.yTop - rect.yBottom};
            LxHostRect *clips = visible.count ? malloc(visible.count * sizeof *clips) : NULL;
            if (clips)
            {
                for (size_t i = 0; i < visible.count; i++)
                {
                    RECTL part = visible.rects[i];
                    clips[i] = (LxHostRect){origin.x + part.xLeft, origin.y + origin.h - part.yTop,
                                            part.xRight - part.xLeft, part.yTop - part.yBottom};
                }
                if (!target->failed &&
                    lx_host_window_video_frame(window->heavy.window, &target->frame, &destination,
                                               clips, visible.count))
                    presentWindow(target->anchor, window);
                else
                    target->failed = TRUE;
                free(clips);
            }
            else if (visible.count)
                target->failed = TRUE;
        }
        else
            target->failed = TRUE;
        free(visible.rects);
        if (ps)
            WinReleasePS(ps);
        clear(target);
    }
    lx_host_mutex_unlock(lock);
}

void lx_pm_video_forget(void *window)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (VideoTarget *target = targets; target; target = target->next)
        if (target->window == window)
        {
            clear(target);
            target->window = NULL;
        }
    lx_host_mutex_unlock(lock);
}

void lx_pm_video_cleanup(void *anchor)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (VideoTarget *target = targets; target; target = target->next)
        if (target->anchor == anchor)
        {
            clear(target);
            target->anchor = NULL;
            target->window = NULL;
        }
    lx_host_mutex_unlock(lock);
}

void lx_pm_video_pump_current(void)
{
    lx_pm_video_pump(getAnchorBlockNoHAB());
}
