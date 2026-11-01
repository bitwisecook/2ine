/* Public domain. No rights reserved. Wholly AI-authored. */
/* Boolean regions are normalized into disjoint horizontal rectangles. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
typedef struct
{
    LONG x;
    int a, b;
} Edge;
static int long_compare(const void *left, const void *right)
{
    LONG a = *(const LONG *)left, b = *(const LONG *)right;
    return (a > b) - (a < b);
}
static int edge_compare(const void *left, const void *right)
{
    return long_compare(&((const Edge *)left)->x, &((const Edge *)right)->x);
}
BOOL regionBoolean(PmRegion *out, const PmRegion *a, const PmRegion *b, LONG mode)
{
    size_t total = a->count + (b ? b->count : 0);
    if (total > 8192)
        return FALSE;
    LONG *ys = malloc((total * 2 + 1) * sizeof *ys);
    Edge *edges = malloc((total * 2 + 1) * sizeof *edges);
    RECTL *rects = NULL;
    size_t count = 0, capacity = 0;
    if (!ys || !edges)
    {
        free(ys);
        free(edges);
        return FALSE;
    }
    size_t ny = 0;
    for (unsigned pass = 0; pass < 2; pass++)
    {
        const PmRegion *r = pass ? b : a;
        if (!r)
            continue;
        for (size_t i = 0; i < r->count; i++)
        {
            ys[ny++] = r->rects[i].yBottom;
            ys[ny++] = r->rects[i].yTop;
        }
    }
    qsort(ys, ny, sizeof *ys, long_compare);
    for (size_t band = 0; band + 1 < ny; band++)
    {
        LONG bottom = ys[band], top = ys[band + 1];
        if (bottom == top)
            continue;
        size_t n = 0;
        for (unsigned pass = 0; pass < 2; pass++)
        {
            const PmRegion *r = pass ? b : a;
            if (!r)
                continue;
            for (size_t i = 0; i < r->count; i++)
            {
                const RECTL *p = &r->rects[i];
                if (p->yBottom <= bottom && p->yTop >= top)
                {
                    edges[n++] = (Edge){p->xLeft, pass ? 0 : 1, pass ? 1 : 0};
                    edges[n++] = (Edge){p->xRight, pass ? 0 : -1, pass ? -1 : 0};
                }
            }
        }
        qsort(edges, n, sizeof *edges, edge_compare);
        int ca = 0, cb = 0;
        BOOL open = FALSE;
        LONG start = 0;
        for (size_t i = 0; i < n;)
        {
            LONG x = edges[i].x;
            while (i < n && edges[i].x == x)
            {
                ca += edges[i].a;
                cb += edges[i].b;
                i++;
            }
            BOOL inside = mode == 1   ? ca || cb
                          : mode == 2 ? ca > 0
                          : mode == 4 ? !!ca != !!cb
                          : mode == 6 ? ca && cb
                                      : ca && !cb;
            if (inside && !open)
            {
                start = x;
                open = TRUE;
            }
            if (!inside && open)
            {
                BOOL merged = FALSE;
                for (size_t j = count; j > 0; j--)
                    if (rects[j - 1].yTop == bottom && rects[j - 1].xLeft == start &&
                        rects[j - 1].xRight == x)
                    {
                        rects[j - 1].yTop = top;
                        merged = TRUE;
                        break;
                    }
                if (!merged)
                {
                    if (count == capacity)
                    {
                        size_t next = capacity ? capacity * 2 : 8;
                        if (next > 65536)
                            goto fail;
                        RECTL *p = realloc(rects, next * sizeof *p);
                        if (!p)
                            goto fail;
                        rects = p;
                        capacity = next;
                    }
                    rects[count++] = (RECTL){start, bottom, x, top};
                }
                open = FALSE;
            }
        }
    }
    free(ys);
    free(edges);
    free(out->rects);
    out->rects = rects;
    out->count = count;
    return TRUE;
fail:
    free(ys);
    free(edges);
    free(rects);
    return FALSE;
}
static void destroy_region(void *resource)
{
    PmRegion *region = resource;
    AnchorBlock *anchor = region->anchor;
    PmRegion **slot = &anchor->regions;
    while (*slot && *slot != region)
        slot = &(*slot)->next;
    if (*slot)
        *slot = region->next;
    free(region->rects);
    free(region);
}
static PmRegion *borrow_region(ULONG handle, LxHandleBorrow *borrow)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !lx_handle_borrow(handle, LX_HANDLE_PM_REGION, borrow))
        return NULL;
    PmRegion *r = borrow->resource;
    if (r->anchor != anchor)
    {
        lx_handle_release(borrow);
        return NULL;
    }
    return r;
}
static LONG region_type(const PmRegion *r)
{
    return !r->count ? 1 : r->count == 1 ? 2 : 3;
}
ULONG lx_pm_region_create(HPS ps, LONG count, const RECTL *rectangles)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !lx_pm_valid_ps(ps) || count < 0 || count > 4096 || (count && !rectangles))
        return 0;
    PmRegion source = {0}, *region = calloc(1, sizeof *region);
    if (!region)
        return 0;
    source.rects = malloc(((size_t)count + 1) * sizeof *source.rects);
    if (!source.rects)
    {
        free(region);
        return 0;
    }
    for (LONG i = 0; i < count; i++)
        if (!WinIsRectEmpty(0, (PRECTL)&rectangles[i]))
            source.rects[source.count++] = rectangles[i];
    BOOL ok = regionBoolean(region, &source, NULL, 2);
    free(source.rects);
    if (!ok)
    {
        free(region);
        return 0;
    }
    region->anchor = anchor;
    region->handle = lx_handle_create(LX_HANDLE_PM_REGION, region, destroy_region);
    if (!region->handle)
    {
        free(region->rects);
        free(region);
        return 0;
    }
    region->next = anchor->regions;
    anchor->regions = region;
    return region->handle;
}
BOOL lx_pm_region_destroy(HPS ps, ULONG handle)
{
    if (!lx_pm_valid_ps(ps))
        return FALSE;
    LxHandleBorrow borrow = {0};
    PmRegion *region = borrow_region(handle, &borrow);
    BOOL result = region && !region->selected &&
                  lx_handle_close_guarded(handle, LX_HANDLE_PM_REGION, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}
LONG lx_pm_region_set(HPS ps, ULONG handle, LONG count, const RECTL *rects)
{
    if (!lx_pm_valid_ps(ps) || count < 0 || count > 4096 || (count && !rects))
        return 0;
    LxHandleBorrow borrow = {0};
    PmRegion *region = borrow_region(handle, &borrow);
    PmRegion source = {0};
    LONG result = 0;
    source.rects = malloc(((size_t)count + 1) * sizeof *source.rects);
    if (region && source.rects)
    {
        for (LONG i = 0; i < count; i++)
            if (!WinIsRectEmpty(0, (PRECTL)&rects[i]))
                source.rects[source.count++] = rects[i];
        if (regionBoolean(region, &source, NULL, 2))
            result = region_type(region);
    }
    free(source.rects);
    lx_handle_release(&borrow);
    return result;
}
LONG lx_pm_region_combine(HPS ps, ULONG destination, ULONG first, ULONG second, LONG mode)
{
    if (!lx_pm_valid_ps(ps) || (mode != 1 && mode != 2 && mode != 4 && mode != 6 && mode != 7))
        return 0;
    LxHandleBorrow d = {0}, a = {0}, b = {0};
    PmRegion *dst = borrow_region(destination, &d), *left = borrow_region(first, &a),
             *right = mode == 2 ? NULL : borrow_region(second, &b);
    LONG result = dst && left && (mode == 2 || right) && regionBoolean(dst, left, right, mode)
                      ? region_type(dst)
                      : 0;
    lx_handle_release(&b);
    lx_handle_release(&a);
    lx_handle_release(&d);
    return result;
}
LONG lx_pm_region_box(HPS ps, ULONG handle, RECTL *rect)
{
    if (!lx_pm_valid_ps(ps) || !rect)
        return 0;
    LxHandleBorrow borrow = {0};
    PmRegion *region = borrow_region(handle, &borrow);
    LONG result = 0;
    if (region)
    {
        *rect = (RECTL){0};
        for (size_t i = 0; i < region->count; i++)
            WinUnionRect(0, rect, rect, &region->rects[i]);
        result = region_type(region);
    }
    lx_handle_release(&borrow);
    return result;
}
LONG lx_pm_region_offset(HPS ps, ULONG handle, const POINTL *point)
{
    if (!lx_pm_valid_ps(ps) || !point)
        return 0;
    LxHandleBorrow borrow = {0};
    PmRegion *region = borrow_region(handle, &borrow);
    LONG result = 0;
    if (region)
    {
        for (size_t i = 0; i < region->count; i++)
            WinOffsetRect(0, &region->rects[i], point->x, point->y);
        result = region_type(region);
    }
    lx_handle_release(&borrow);
    return result;
}
LONG lx_pm_set_clip(HPS handle, ULONG region, ULONG *previous)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return 0;
    LxHandleBorrow next = {0};
    PmRegion *value = region ? borrow_region(region, &next) : NULL;
    if (region && !value)
        return 0;
    PmRegion *old = ps->clip_lease.resource;
    if (previous)
        *previous = old ? old->handle : 0;
    if (value)
        value->selected++;
    if (old)
        old->selected--;
    lx_handle_release(&ps->clip_lease);
    ps->clip_lease = next;
    return value ? region_type(value) : 2;
}
LONG lx_pm_rect_visible(HPS handle, const RECTL *rect)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect)
        return 0;
    PmRegion *clip = ps->clip_lease.resource;
    RECTL bounds = ps->window ? (RECTL){0, 0, ps->window->w, ps->window->h} : ps->viewport;
    if (ps->bitmap_lease.resource)
    {
        PmBitmap *b = ps->bitmap_lease.resource;
        bounds = (RECTL){0, 0, (LONG)b->width, (LONG)b->height};
    }
    RECTL target;
    if (!WinIntersectRect(0, &target, (PRECTL)rect, &bounds))
        return 1;
    uint64_t area = 0;
    if (clip)
        for (size_t i = 0; i < clip->count; i++)
        {
            RECTL r;
            if (WinIntersectRect(0, &r, &target, &clip->rects[i]))
                area += (uint64_t)((int64_t)r.xRight - r.xLeft) * ((int64_t)r.yTop - r.yBottom);
        }
    else
        area = (uint64_t)((int64_t)target.xRight - target.xLeft) *
               ((int64_t)target.yTop - target.yBottom);
    if (!area)
        return 1;
    uint64_t full =
        (uint64_t)((int64_t)rect->xRight - rect->xLeft) * ((int64_t)rect->yTop - rect->yBottom);
    return area == full ? 3 : 2;
}
void lx_pm_region_cleanup(void *context)
{
    AnchorBlock *anchor = context;
    while (anchor->regions)
        lx_handle_close(anchor->regions->handle, LX_HANDLE_PM_REGION);
}

BOOL lx_pm_clip_point(HPS handle, LONG x, LONG y)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return FALSE;
    PmRegion *clip = ps->clip_lease.resource;
    if (!clip)
        return TRUE;
    POINTL point = {x, y};
    for (size_t i = 0; i < clip->count; i++)
        if (WinPtInRect(0, &clip->rects[i], &point))
            return TRUE;
    return FALSE;
}
