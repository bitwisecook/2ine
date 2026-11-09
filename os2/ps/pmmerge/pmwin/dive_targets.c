/* Public domain. No rights reserved. Wholly AI-authored. */
/* DIVE workers submit copies. The PM owner alone draws host window surfaces. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "host/sync.h"

typedef struct PmDiveTarget
{
    struct PmDiveTarget *next;
    AnchorBlock *anchor;
    Window *window;
    LONG x, y;
    unsigned width, height;
    ULONG count;
    RECTL *rectangles;
    uint32_t *pixels;
} PmDiveTarget;

static PmDiveTarget *targets;
static LxHostMutex *target_mutex;
static LxHostOnce target_once = LX_HOST_ONCE_INIT;

static void initialize_targets(void)
{
    target_mutex = lx_host_mutex_create(0, 0);
}

static void clear_frame(PmDiveTarget *target)
{
    free(target->pixels);
    free(target->rectangles);
    target->pixels = NULL;
    target->rectangles = NULL;
}

static BOOL write_pixels(HPS ps, LONG x, LONG y, unsigned width, unsigned height,
                         const uint32_t *pixels)
{
    BOOL partial = FALSE;
    for (size_t i = 0; i < (size_t)width * height; i++)
        partial = partial || (pixels[i] & 0x01000000u);
    if (!partial)
        return lx_pm_raster_write(ps, x, y, width, height, pixels);
    for (unsigned row = 0; row < height; row++)
    {
        const uint32_t *line = pixels + (size_t)row * width;
        unsigned column = 0;
        while (column < width)
        {
            while (column < width && (line[column] & 0x01000000u))
                column++;
            unsigned start = column;
            while (column < width && !(line[column] & 0x01000000u))
                column++;
            if (column > start &&
                !lx_pm_raster_write(ps, x + start, y + row, column - start, 1, line + start))
                return FALSE;
        }
    }
    return TRUE;
}

void *lx_pm_dive_target(LONG x, LONG y, unsigned width, unsigned height)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !width || !height)
        return NULL;
    Window *best = NULL;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *window = anchor->windows[i];
        if (!window || window->destroying || !window->hwnd || !window->heavy.window ||
            !(window->style & WS_VISIBLE))
            continue;
        POINTL point = {0, 0};
        if (!WinMapWindowPoints(window->hwnd, HWND_DESKTOP, &point, 1))
            continue;
        if (x < point.x || y < point.y || (int64_t)x + width > (int64_t)point.x + window->w ||
            (int64_t)y + height > (int64_t)point.y + window->h)
            continue;
        if (!best || (int64_t)window->w * window->h < (int64_t)best->w * best->h)
            best = window;
    }
    if (!best)
        return NULL;
    lx_host_once(&target_once, initialize_targets);
    PmDiveTarget *target = target_mutex ? calloc(1, sizeof *target) : NULL;
    if (!target)
        return NULL;
    target->anchor = anchor;
    target->window = best;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    target->next = targets;
    targets = target;
    lx_host_mutex_unlock(target_mutex);
    return target;
}

void lx_pm_dive_release(void *context)
{
    if (!context || !target_mutex)
        return;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    PmDiveTarget *target = context;
    for (PmDiveTarget **link = &targets; *link; link = &(*link)->next)
        if (*link == target)
        {
            *link = target->next;
            clear_frame(target);
            free(target);
            break;
        }
    lx_host_mutex_unlock(target_mutex);
}

BOOL lx_pm_dive_blit(void *context, LONG x, LONG y, unsigned width, unsigned height,
                     const uint32_t *pixels, ULONG count, const RECTL *rectangles)
{
    if (!context || !target_mutex || !pixels || !width || !height || width > 16384 ||
        height > 16384 || (uint64_t)width * height > 16777216 ||
        (count != 0xffffffffUL && count > 65536) || (count && count != 0xffffffffUL && !rectangles))
        return FALSE;
    uint32_t *copy = malloc((size_t)width * height * 4);
    RECTL *clips = count && count != 0xffffffffUL ? malloc((size_t)count * sizeof *clips) : NULL;
    if (!copy || (count && count != 0xffffffffUL && !clips))
    {
        free(copy);
        free(clips);
        return FALSE;
    }
    memcpy(copy, pixels, (size_t)width * height * 4);
    if (clips)
        memcpy(clips, rectangles, (size_t)count * sizeof *clips);
    /* Apply visibility before coalescing partial submissions. Unchanged pixels
       cannot erase an older pending update, including one with different clips. */
    if (count != 0xffffffffUL)
    {
        size_t stride = (size_t)width + 1;
        int32_t *coverage = calloc(stride * (height + 1), sizeof *coverage);
        if (!coverage)
        {
            free(copy);
            free(clips);
            return FALSE;
        }
        /* A rectangular difference image bounds clipping to O(rects + pixels),
           including heavily overlapping visible-region lists. */
        for (ULONG i = 0; i < count; i++)
        {
            RECTL bounds = {0, 0, (LONG)width, (LONG)height}, rect;
            if (!WinIntersectRect(0, &rect, &bounds, &clips[i]))
                continue;
            coverage[(size_t)rect.yBottom * stride + rect.xLeft]++;
            coverage[(size_t)rect.yBottom * stride + rect.xRight]--;
            coverage[(size_t)rect.yTop * stride + rect.xLeft]--;
            coverage[(size_t)rect.yTop * stride + rect.xRight]++;
        }
        for (unsigned y = 0; y < height; y++)
        {
            int32_t running = 0;
            for (unsigned x = 0; x < width; x++)
            {
                running += coverage[(size_t)y * stride + x];
                int32_t visible = running + (y ? coverage[(size_t)(y - 1) * stride + x] : 0);
                coverage[(size_t)y * stride + x] = visible;
                if (!visible)
                    copy[(size_t)y * width + x] = 0x01000000u;
            }
        }
        free(coverage);
    }
    free(clips);
    clips = NULL;
    count = 0xffffffffUL;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    PmDiveTarget *target = context;
    BOOL ok = target->window != NULL;
    BOOL wake = ok && target->pixels == NULL;
    if (ok)
    {
        if (target->pixels && target->x == x && target->y == y && target->width == width &&
            target->height == height)
            for (size_t i = 0; i < (size_t)width * height; i++)
                if (copy[i] & 0x01000000u)
                    copy[i] = target->pixels[i];
        clear_frame(target);
        target->pixels = copy;
        target->rectangles = clips;
        target->x = x;
        target->y = y;
        target->width = width;
        target->height = height;
        target->count = count;
    }
    else
    {
        free(copy);
        free(clips);
    }
    lx_host_mutex_unlock(target_mutex);
    if (wake)
    {
        LxHostEvent event = {0};
        event.type = LX_HOST_EVENT_OTHER;
        lx_host_video_push(&event);
    }
    return ok;
}

BOOL lx_pm_dive_read(void *context, LONG x, LONG y, unsigned width, unsigned height,
                     uint32_t *pixels)
{
    if (!context || !target_mutex)
        return FALSE;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    PmDiveTarget *target = context;
    BOOL ok = target->window && target->anchor == getAnchorBlockNoHAB();
    POINTL origin = {0, 0};
    HPS ps = ok && WinMapWindowPoints(target->window->hwnd, HWND_DESKTOP, &origin, 1)
                 ? WinGetPS(target->window->hwnd)
                 : 0;
    ok = ps && lx_pm_raster_read(ps, x - origin.x, y - origin.y, width, height, pixels);
    if (ps)
        WinReleasePS(ps);
    lx_host_mutex_unlock(target_mutex);
    return ok;
}

BOOL lx_pm_dive_flush(void *context)
{
    if (!context || !target_mutex)
        return FALSE;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    PmDiveTarget *target = context;
    AnchorBlock *anchor = target->anchor;
    BOOL ok = anchor && anchor == getAnchorBlockNoHAB() && target->window;
    lx_host_mutex_unlock(target_mutex);
    if (ok)
        lx_pm_dive_pump(anchor);
    return ok;
}

void lx_pm_dive_pump(void *context)
{
    if (!target_mutex)
        return;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    for (PmDiveTarget *target = targets; target; target = target->next)
    {
        if (target->anchor != context || !target->window || !target->pixels)
            continue;
        Window *window = target->window;
        POINTL origin = {0, 0};
        WinMapWindowPoints(window->hwnd, HWND_DESKTOP, &origin, 1);
        HPS ps = WinGetPS(window->hwnd);
        if (ps)
        {
            if (target->count == 0xffffffffUL)
                write_pixels(ps, target->x - origin.x, target->y - origin.y, target->width,
                             target->height, target->pixels);
            else
            {
                uint32_t *part = malloc((size_t)target->width * target->height * 4);
                for (ULONG i = 0; part && i < target->count; i++)
                {
                    RECTL bounds = {0, 0, (LONG)target->width, (LONG)target->height}, clip;
                    if (!WinIntersectRect(0, &clip, &bounds, &target->rectangles[i]))
                        continue;
                    unsigned w = clip.xRight - clip.xLeft, h = clip.yTop - clip.yBottom;
                    for (unsigned row = 0; row < h; row++)
                        memcpy(part + (size_t)row * w,
                               target->pixels + (size_t)(clip.yBottom + row) * target->width +
                                   clip.xLeft,
                               (size_t)w * 4);
                    write_pixels(ps, target->x - origin.x + clip.xLeft,
                                 target->y - origin.y + clip.yBottom, w, h, part);
                }
                free(part);
            }
            WinReleasePS(ps);
        }
        clear_frame(target);
    }
    lx_host_mutex_unlock(target_mutex);
}

void lx_pm_dive_forget(void *window)
{
    if (!target_mutex)
        return;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    for (PmDiveTarget *target = targets; target; target = target->next)
        if (target->window == window)
        {
            target->window = NULL;
            clear_frame(target);
        }
    lx_host_mutex_unlock(target_mutex);
}

void lx_pm_dive_cleanup(void *anchor)
{
    if (!target_mutex)
        return;
    lx_host_mutex_lock(target_mutex, UINT32_MAX);
    for (PmDiveTarget *target = targets; target; target = target->next)
        if (target->anchor == anchor)
        {
            target->anchor = NULL;
            target->window = NULL;
            clear_frame(target);
        }
    lx_host_mutex_unlock(target_mutex);
}
