/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows regions retain top-left coordinates; selected clips are copied into GPI. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include <math.h>
#include "os2libs/pmwinx/windows.h"
static void free_region(void *data)
{
    PmRegion *region = data;
    free(region->rects);
    free(region);
}
static LONG region_type(const PmRegion *region)
{
    return !region->count ? 1 : region->count == 1 ? 2 : 3;
}
static BOOL rectangle(PmRegion *region, LONG left, LONG top, LONG right, LONG bottom)
{
    const LONG minimum = -(1L << 26), maximum = (1L << 26) - 1;
    if (left < minimum || left > maximum || top < minimum || top > maximum || right < minimum ||
        right > maximum || bottom < minimum || bottom > maximum)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL rect = {left < right ? left : right, top < bottom ? top : bottom,
                  left < right ? right : left, top < bottom ? bottom : top};
    PmRegion source = {0};
    source.rects = &rect;
    source.count = left != right && top != bottom;
    if (!regionBoolean(region, &source, NULL, 2))
    {
        open32SetError(8);
        return FALSE;
    }
    return TRUE;
}
ULONG CreateRectRgn(LONG left, LONG top, LONG right, LONG bottom)
{
    PmRegion *region = calloc(1, sizeof *region);
    if (!region)
    {
        open32SetError(8);
        return 0;
    }
    if (!rectangle(region, left, top, right, bottom))
    {
        free_region(region);
        return 0;
    }
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_REGION, region, free_region);
    if (!handle)
    {
        free_region(region);
        open32SetError(8);
    }
    return handle;
}
ULONG CreateRectRgnIndirect(PVOID input)
{
    if (!input)
    {
        open32SetError(87);
        return 0;
    }
    return CreateRectRgn(
        (LONG)lx_guest_load_u32(input), (LONG)lx_guest_load_u32((uint8 *)input + 4),
        (LONG)lx_guest_load_u32((uint8 *)input + 8), (LONG)lx_guest_load_u32((uint8 *)input + 12));
}
BOOL SetRectRgn(ULONG handle, LONG left, LONG top, LONG right, LONG bottom)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    BOOL result = rectangle(borrow.resource, left, top, right, bottom);
    lx_handle_release(&borrow);
    return result;
}
LONG CombineRgn(ULONG destination, ULONG first, ULONG second, LONG mode)
{
    static const LONG modes[] = {0, 6, 1, 4, 7, 2};
    if (mode < 1 || mode > 5)
    {
        open32SetError(87);
        return 0;
    }
    LxHandleBorrow out = {0}, a = {0}, b = {0};
    LONG result = 0;
    if (lx_handle_borrow(destination, LX_HANDLE_OPEN32_REGION, &out) &&
        lx_handle_borrow(first, LX_HANDLE_OPEN32_REGION, &a) &&
        (mode == 5 || lx_handle_borrow(second, LX_HANDLE_OPEN32_REGION, &b)))
    {
        if (regionBoolean(out.resource, a.resource, b.resource, modes[mode]))
            result = region_type(out.resource);
        else
            open32SetError(8);
    }
    else
        open32SetError(6);
    lx_handle_release(&b);
    lx_handle_release(&a);
    lx_handle_release(&out);
    return result;
}
LONG GetRgnBox(ULONG handle, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!output || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(87);
        return 0;
    }
    PmRegion *region = borrow.resource;
    RECTL box = {0};
    for (size_t i = 0; i < region->count; i++)
    {
        const RECTL *r = &region->rects[i];
        if (!i)
            box = *r;
        else
        {
            if (r->xLeft < box.xLeft)
                box.xLeft = r->xLeft;
            if (r->yBottom < box.yBottom)
                box.yBottom = r->yBottom;
            if (r->xRight > box.xRight)
                box.xRight = r->xRight;
            if (r->yTop > box.yTop)
                box.yTop = r->yTop;
        }
    }
    lx_guest_store_u32(output, (ULONG)box.xLeft);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)box.yBottom);
    lx_guest_store_u32((uint8 *)output + 8, (ULONG)box.xRight);
    lx_guest_store_u32((uint8 *)output + 12, (ULONG)box.yTop);
    LONG result = region_type(region);
    lx_handle_release(&borrow);
    return result;
}
LONG SelectClipRgn(ULONG handle, ULONG region_handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return 0;
    }
    ULONG selected = 0;
    LxHandleBorrow borrow = {0};
    if (region_handle)
    {
        if (!lx_handle_borrow(region_handle, LX_HANDLE_OPEN32_REGION, &borrow))
        {
            open32SetError(6);
            return 0;
        }
        PmRegion *region = borrow.resource;
        RECTL *rects = calloc(region->count ? region->count : 1, sizeof *rects);
        if (!rects)
        {
            lx_handle_release(&borrow);
            open32SetError(8);
            return 0;
        }
        LONG height = open32DCHeight(handle);
        BOOL valid = TRUE;
        for (size_t i = 0; i < region->count; i++)
        {
            const RECTL *r = &region->rects[i];
            int64_t lower = (int64_t)height - r->yTop, upper = (int64_t)height - r->yBottom;
            if (lower < INT32_MIN || upper > INT32_MAX)
            {
                valid = FALSE;
                break;
            }
            rects[i] = (RECTL){r->xLeft, (LONG)lower, r->xRight, (LONG)upper};
        }
        if (valid)
            selected = GpiCreateRegion(handle, (LONG)region->count, rects);
        free(rects);
        lx_handle_release(&borrow);
        if (!selected)
        {
            open32SetError(valid ? 8 : 87);
            return 0;
        }
    }
    LONG result = GpiSetClipRegion(handle, selected, NULL);
    if (!result)
    {
        if (selected)
            GpiDestroyRegion(handle, selected);
        open32SetError(87);
        return 0;
    }
    ULONG previous = dc->clip_region;
    dc->clip_region = selected;
    if (previous)
        GpiDestroyRegion(handle, previous);
    open32MetafileClip(handle, region_handle);
    return result;
}
BOOL open32DeleteRegion(ULONG handle)
{
    return lx_handle_close(handle, LX_HANDLE_OPEN32_REGION);
}

static LONG clip_rectangle(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, LONG mode)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return 0;
    }
    const LONG recorded[] = {left, top, right, bottom};
    open32MetafileWords(handle, mode == 6 ? 0x416 : 0x415, 4, recorded);
    if (!open32MapPoint(dc, &left, &top, TRUE) || !open32MapPoint(dc, &right, &bottom, TRUE))
        return 0;
    if (left > right)
    {
        LONG swap = left;
        left = right;
        right = swap;
    }
    if (top > bottom)
    {
        LONG swap = top;
        top = bottom;
        bottom = swap;
    }
    LONG height = open32DCHeight(handle);
    int64_t lower = (int64_t)height - bottom, upper = (int64_t)height - top;
    if (lower < INT32_MIN || lower > INT32_MAX || upper < INT32_MIN || upper > INT32_MAX)
    {
        open32SetError(87);
        return 0;
    }
    RECTL area = {left, (LONG)lower, right, (LONG)upper};
    RECTL bounds = {0, 0, open32DCWidth(handle), height};
    BOOL empty = left >= right || top >= bottom;
    ULONG rectangle = GpiCreateRegion(handle, empty ? 0 : 1, empty ? NULL : &area);
    ULONG base = dc->clip_region ? dc->clip_region : GpiCreateRegion(handle, 1, &bounds);
    ULONG selected = GpiCreateRegion(handle, 0, NULL);
    LONG result = rectangle && base && selected
                      ? GpiCombineRegion(handle, selected, base, rectangle, mode)
                      : 0;
    if (result)
        result = GpiSetClipRegion(handle, selected, NULL);
    if (rectangle)
        GpiDestroyRegion(handle, rectangle);
    if (base && base != dc->clip_region)
        GpiDestroyRegion(handle, base);
    if (!result)
    {
        if (selected)
            GpiDestroyRegion(handle, selected);
        open32SetError(8);
        return 0;
    }
    ULONG previous = dc->clip_region;
    dc->clip_region = selected;
    if (previous)
        GpiDestroyRegion(handle, previous);
    return result;
}
LONG IntersectClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
{
    return clip_rectangle(dc, left, top, right, bottom, 6); /* CRGN_AND */
}
LONG ExcludeClipRect(ULONG dc, LONG left, LONG top, LONG right, LONG bottom)
{
    return clip_rectangle(dc, left, top, right, bottom, 7); /* CRGN_DIFF */
}

LONG GetClipBox(ULONG handle, PVOID output)
{
    if (!open32FindDC(handle) || !output)
    {
        open32SetError(output ? 6 : 87);
        return 0;
    }
    RECTL rectangle;
    LONG result = lx_pm_visible_box(handle, &rectangle);
    if (!result)
    {
        open32SetError(87);
        return 0;
    }
    if (result == 1)
        memset(output, 0, 16);
    else
    {
        LONG height = open32DCHeight(handle);
        lx_guest_store_u32(output, (ULONG)rectangle.xLeft);
        lx_guest_store_u32((uint8 *)output + 4, (ULONG)(height - rectangle.yTop));
        lx_guest_store_u32((uint8 *)output + 8, (ULONG)rectangle.xRight);
        lx_guest_store_u32((uint8 *)output + 12, (ULONG)(height - rectangle.yBottom));
    }
    return result;
}

/* Raster regions use pixel centres and half-open edges, the same convention as
   PMWINX's GPI regions. Canonicalize once after collecting scanline spans. */
static ULONG adopt_region(PmRegion *region)
{
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_REGION, region, free_region);
    if (!handle)
    {
        free_region(region);
        open32SetError(8);
    }
    return handle;
}
static ULONG round_region(LONG l, LONG t, LONG r, LONG b, LONG ew, LONG eh)
{
    if (l > r)
    {
        LONG v = l;
        l = r;
        r = v;
    }
    if (t > b)
    {
        LONG v = t;
        t = b;
        b = v;
    }
    int64_t w = (int64_t)r - l, h = (int64_t)b - t;
    if (w > 65536 || h > 65536 || l < -(1L << 26) || r >= (1L << 26) || t < -(1L << 26) ||
        b >= (1L << 26))
    {
        open32SetError(87);
        return 0;
    }
    if (ew < 0)
        ew = ew == INT32_MIN ? INT32_MAX : -ew;
    if (eh < 0)
        eh = eh == INT32_MIN ? INT32_MAX : -eh;
    if (ew > w)
        ew = (LONG)w;
    if (eh > h)
        eh = (LONG)h;
    if (!ew || !eh)
        return CreateRectRgn(l, t, r, b);
    PmRegion *region = calloc(1, sizeof *region);
    RECTL *spans = h ? calloc((size_t)h, sizeof *spans) : NULL;
    if (!region || (h && !spans))
    {
        free(region);
        free(spans);
        open32SetError(8);
        return 0;
    }
    size_t n = 0;
    double rx = ew / 2.0, ry = eh / 2.0;
    for (LONG y = t; y < b; y++)
    {
        double cy = y < t + ry ? t + ry : b - ry;
        double dy = (y + 0.5 - cy) / ry;
        double inset =
            (y + 0.5 >= t + ry && y + 0.5 <= b - ry) ? 0 : rx * (1 - sqrt(fmax(0, 1 - dy * dy)));
        LONG left = (LONG)ceil(l + inset - 0.5), right = (LONG)ceil(r - inset - 0.5);
        if (left < right)
            spans[n++] = (RECTL){left, y, right, y + 1};
    }
    PmRegion raw = {0};
    raw.rects = spans;
    raw.count = n;
    BOOL ok = regionBoolean(region, &raw, NULL, 2);
    free(spans);
    if (!ok)
    {
        free_region(region);
        open32SetError(8);
        return 0;
    }
    return adopt_region(region);
}
ULONG CreateRoundRectRgn(LONG l, LONG t, LONG r, LONG b, LONG ew, LONG eh)
{
    return round_region(l, t, r, b, ew, eh);
}
ULONG CreateEllipticRgn(LONG l, LONG t, LONG r, LONG b)
{
    int64_t w = llabs((int64_t)r - l), h = llabs((int64_t)b - t);
    if (w > INT32_MAX || h > INT32_MAX)
    {
        open32SetError(87);
        return 0;
    }
    return round_region(l, t, r, b, (LONG)w, (LONG)h);
}
ULONG CreateEllipticRgnIndirect(PVOID p)
{
    if (!p)
    {
        open32SetError(87);
        return 0;
    }
    return CreateEllipticRgn((LONG)lx_guest_load_u32(p), (LONG)lx_guest_load_u32((uint8 *)p + 4),
                             (LONG)lx_guest_load_u32((uint8 *)p + 8),
                             (LONG)lx_guest_load_u32((uint8 *)p + 12));
}
typedef struct RegionCrossing
{
    double x;
    LONG winding;
} RegionCrossing;
static int crossing_order(const void *a, const void *b)
{
    double x = ((const RegionCrossing *)a)->x, y = ((const RegionCrossing *)b)->x;
    return x < y ? -1 : x > y;
}
ULONG CreatePolyPolygonRgn(PVOID points, PVOID counts, LONG polygons, LONG mode)
{
    if (!points || !counts || polygons < 1 || polygons > 65536 || (mode != 1 && mode != 2))
    {
        open32SetError(87);
        return 0;
    }
    size_t total = 0;
    LONG top = INT32_MAX, bottom = INT32_MIN;
    for (LONG i = 0; i < polygons; i++)
    {
        LONG n = (LONG)lx_guest_load_u32((uint8 *)counts + i * 4);
        if (n < 0 || n > 65536 - total)
        {
            open32SetError(87);
            return 0;
        }
        total += (size_t)n;
    }
    for (size_t i = 0; i < total; i++)
    {
        LONG x = (LONG)lx_guest_load_u32((uint8 *)points + i * 8),
             y = (LONG)lx_guest_load_u32((uint8 *)points + i * 8 + 4);
        if (x < -(1L << 26) || x >= (1L << 26) || y < -(1L << 26) || y >= (1L << 26))
        {
            open32SetError(87);
            return 0;
        }
        if (y < top)
            top = y;
        if (y > bottom)
            bottom = y;
    }
    if (!total)
        return CreateRectRgn(0, 0, 0, 0);
    if ((int64_t)bottom - top > 65536 || (uint64)(bottom - top) * total > 67108864)
    {
        open32SetError(8);
        return 0;
    }
    RegionCrossing *edges = calloc(total ? total : 1, sizeof *edges);
    PmRegion *region = calloc(1, sizeof *region);
    RECTL *spans = NULL;
    size_t used = 0, capacity = 0;
    BOOL ok = edges && region;
    for (LONG y = top; ok && y < bottom; y++)
    {
        size_t offset = 0, n = 0;
        for (LONG p = 0; p < polygons; p++)
        {
            LONG count = (LONG)lx_guest_load_u32((uint8 *)counts + p * 4);
            for (LONG i = 0; i < count; i++)
            {
                const uint8 *a = (uint8 *)points + (offset + i) * 8,
                            *b = (uint8 *)points + (offset + (i + 1) % count) * 8;
                LONG ax = (LONG)lx_guest_load_u32(a), ay = (LONG)lx_guest_load_u32(a + 4),
                     bx = (LONG)lx_guest_load_u32(b), by = (LONG)lx_guest_load_u32(b + 4);
                double scan = y + 0.5;
                if ((ay <= scan && by > scan) || (by <= scan && ay > scan))
                    edges[n++] = (RegionCrossing){
                        ax + (scan - ay) * ((double)bx - ax) / ((double)by - ay), by > ay ? 1 : -1};
            }
            offset += (size_t)count;
        }
        qsort(edges, n, sizeof *edges, crossing_order);
        LONG winding = 0;
        BOOL inside = FALSE;
        double left = 0;
        for (size_t i = 0; i < n;)
        {
            double x = edges[i].x;
            LONG delta = 0;
            size_t j = i;
            while (j < n && edges[j].x == x)
                delta += edges[j++].winding;
            BOOL prior = inside;
            if (mode == 1)
                inside ^= ((j - i) & 1) != 0;
            else
            {
                winding += delta;
                inside = winding != 0;
            }
            if (!prior && inside)
                left = x;
            if (prior && !inside)
            {
                LONG l = (LONG)ceil(left - 0.5), r = (LONG)ceil(x - 0.5);
                if (l < r)
                {
                    if (used == capacity)
                    {
                        size_t next = capacity ? capacity * 2 : 256;
                        if (next > 16777216)
                        {
                            ok = FALSE;
                            break;
                        }
                        RECTL *more = realloc(spans, next * sizeof *more);
                        if (!more)
                        {
                            ok = FALSE;
                            break;
                        }
                        spans = more;
                        capacity = next;
                    }
                    spans[used++] = (RECTL){l, y, r, y + 1};
                }
            }
            i = j;
        }
    }
    free(edges);
    if (ok)
    {
        PmRegion raw = {0};
        raw.rects = spans;
        raw.count = used;
        ok = regionBoolean(region, &raw, NULL, 2);
    }
    free(spans);
    if (!ok)
    {
        if (region)
            free_region(region);
        open32SetError(8);
        return 0;
    }
    return adopt_region(region);
}
ULONG CreatePolygonRgn(PVOID points, LONG count, LONG mode)
{
    if (count < 2)
    {
        open32SetError(87);
        return 0;
    }
    uint8 n[4];
    lx_guest_store_u32(n, (ULONG)count);
    return CreatePolyPolygonRgn(points, n, 1, mode);
}
BOOL EqualRgn(ULONG first, ULONG second)
{
    LxHandleBorrow a = {0}, b = {0};
    BOOL same = FALSE;
    if (lx_handle_borrow(first, LX_HANDLE_OPEN32_REGION, &a) &&
        lx_handle_borrow(second, LX_HANDLE_OPEN32_REGION, &b))
    {
        PmRegion difference = {0};
        if (regionBoolean(&difference, a.resource, b.resource, 4))
            same = difference.count == 0;
        else
            open32SetError(8);
        free(difference.rects);
    }
    else
        open32SetError(6);
    lx_handle_release(&b);
    lx_handle_release(&a);
    return same;
}
LONG OffsetRgn(ULONG h, LONG dx, LONG dy)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    PmRegion *r = borrow.resource;
    BOOL valid = TRUE;
    for (size_t i = 0; i < r->count; i++)
    {
        RECTL b = r->rects[i];
        int64_t l = (int64_t)b.xLeft + dx, t = (int64_t)b.yBottom + dy, rr = (int64_t)b.xRight + dx,
                bb = (int64_t)b.yTop + dy;
        if (l < -(1L << 26) || rr >= (1L << 26) || t < -(1L << 26) || bb >= (1L << 26))
        {
            valid = FALSE;
            break;
        }
    }
    if (valid)
        for (size_t i = 0; i < r->count; i++)
        {
            r->rects[i].xLeft += dx;
            r->rects[i].xRight += dx;
            r->rects[i].yBottom += dy;
            r->rects[i].yTop += dy;
        }
    LONG result = valid ? region_type(r) : 0;
    lx_handle_release(&borrow);
    if (!valid)
        open32SetError(87);
    return result;
}
BOOL PtInRegion(ULONG h, LONG x, LONG y)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    PmRegion *r = borrow.resource;
    BOOL found = FALSE;
    for (size_t i = 0; i < r->count; i++)
    {
        RECTL b = r->rects[i];
        if (x >= b.xLeft && x < b.xRight && y >= b.yBottom && y < b.yTop)
        {
            found = TRUE;
            break;
        }
    }
    lx_handle_release(&borrow);
    return found;
}
BOOL RectInRegion(ULONG h, PVOID p)
{
    LxHandleBorrow borrow = {0};
    if (!p || !lx_handle_borrow(h, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(p ? 6 : 87);
        return FALSE;
    }
    LONG l = (LONG)lx_guest_load_u32(p), t = (LONG)lx_guest_load_u32((uint8 *)p + 4),
         r = (LONG)lx_guest_load_u32((uint8 *)p + 8), b = (LONG)lx_guest_load_u32((uint8 *)p + 12);
    PmRegion *region = borrow.resource;
    BOOL found = FALSE;
    for (size_t i = 0; i < region->count; i++)
    {
        RECTL q = region->rects[i];
        if (l < q.xRight && r > q.xLeft && t < q.yTop && b > q.yBottom && l < r && t < b)
        {
            found = TRUE;
            break;
        }
    }
    lx_handle_release(&borrow);
    return found;
}
LONG GetClipRgn(ULONG h, ULONG out)
{
    if (!out)
        return 0;
    Open32DC *dc = open32FindDC(h);
    LxHandleBorrow borrow = {0}, native = {0};
    if (!dc || !lx_handle_borrow(out, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(6);
        return -1;
    }
    if (!dc->clip_region)
    {
        lx_handle_release(&borrow);
        return 0;
    }
    PmRegion *source = lx_handle_borrow(dc->clip_region, LX_HANDLE_PM_REGION, &native)
                           ? native.resource
                           : NULL,
             *target = borrow.resource;
    PmRegion converted = {0};
    LONG result = -1;
    if (source)
    {
        converted.count = source->count;
        converted.rects = calloc(source->count ? source->count : 1, sizeof *converted.rects);
        if (converted.rects)
        {
            LONG height = open32DCHeight(h);
            for (size_t i = 0; i < source->count; i++)
            {
                RECTL b = source->rects[i];
                converted.rects[i] =
                    (RECTL){b.xLeft, height - b.yTop, b.xRight, height - b.yBottom};
            }
            if (regionBoolean(target, &converted, NULL, 2))
                result = 1;
        }
    }
    free(converted.rects);
    lx_handle_release(&native);
    lx_handle_release(&borrow);
    if (result < 0)
        open32SetError(8);
    return result;
}
LONG ExtSelectClipRgn(ULONG h, ULONG region, LONG mode)
{
    if (mode == 5)
        return SelectClipRgn(h, region);
    if (mode < 1 || mode > 4 || !region || !open32FindDC(h))
    {
        open32SetError(87);
        return 0;
    }
    ULONG base = CreateRectRgn(0, 0, 0, 0);
    if (!base)
        return 0;
    LONG copied = GetClipRgn(h, base);
    if (!copied)
        SetRectRgn(base, 0, 0, open32DCWidth(h), open32DCHeight(h));
    LONG result = copied < 0 ? 0 : CombineRgn(base, base, region, mode);
    if (result)
        result = SelectClipRgn(h, base);
    open32DeleteRegion(base);
    return result;
}
BOOL RectVisible(ULONG h, PVOID p)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !p)
    {
        open32SetError(dc ? 87 : 6);
        return FALSE;
    }
    uint8 logical[16];
    memcpy(logical, p, 16);
    LONG l = (LONG)lx_guest_load_u32(logical), t = (LONG)lx_guest_load_u32(logical + 4),
         r = (LONG)lx_guest_load_u32(logical + 8), b = (LONG)lx_guest_load_u32(logical + 12);
    if (!open32MapPoint(dc, &l, &t, TRUE) || !open32MapPoint(dc, &r, &b, TRUE))
        return FALSE;
    if (l > r)
    {
        LONG v = l;
        l = r;
        r = v;
    }
    if (t > b)
    {
        LONG v = t;
        t = b;
        b = v;
    }
    lx_guest_store_u32(logical, (ULONG)l);
    lx_guest_store_u32(logical + 4, (ULONG)t);
    lx_guest_store_u32(logical + 8, (ULONG)r);
    lx_guest_store_u32(logical + 12, (ULONG)b);
    uint8 box[16];
    if (!GetClipBox(h, box))
        return FALSE;
    ULONG clip = CreateRectRgnIndirect(box);
    if (!clip)
        return FALSE;
    LONG copied = GetClipRgn(h, clip);
    BOOL result = copied >= 0 && RectInRegion(clip, logical);
    open32DeleteRegion(clip);
    return result;
}
BOOL FillRgn(ULONG h, ULONG region, ULONG brush)
{
    LxHandleBorrow borrow = {0};
    if (!open32FindDC(h) || !lx_handle_borrow(region, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    PmRegion *r = borrow.resource;
    BOOL ok = TRUE;
    for (size_t i = 0; ok && i < r->count; i++)
    {
        uint8 b[16];
        RECTL q = r->rects[i];
        lx_guest_store_u32(b, (ULONG)q.xLeft);
        lx_guest_store_u32(b + 4, (ULONG)q.yBottom);
        lx_guest_store_u32(b + 8, (ULONG)q.xRight);
        lx_guest_store_u32(b + 12, (ULONG)q.yTop);
        ok = FillRect(h, b, brush) != 0;
    }
    lx_handle_release(&borrow);
    return ok;
}
BOOL PaintRgn(ULONG h, ULONG region)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(6);
        return FALSE;
    }
    return FillRgn(h, region, dc->brush_handle);
}
BOOL FrameRgn(ULONG h, ULONG region, ULONG brush, LONG width, LONG height)
{
    if (!open32FindDC(h))
    {
        open32SetError(6);
        return FALSE;
    }
    width = width < 0 ? (width == INT32_MIN ? INT32_MAX : -width) : width;
    height = height < 0 ? (height == INT32_MIN ? INT32_MAX : -height) : height;
    if (width > 32768 || height > 32768)
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG interior = CreateRectRgn(0, 0, 0, 0), copy = CreateRectRgn(0, 0, 0, 0),
          border = CreateRectRgn(0, 0, 0, 0);
    BOOL ok = interior && copy && border && CombineRgn(interior, region, 0, 5);
    /* Erosion by a rectangular pen separates into a horizontal then a vertical pass. */
    for (LONG dx = -width; ok && dx <= width; dx++)
        if (dx)
            ok = CombineRgn(copy, region, 0, 5) && OffsetRgn(copy, dx, 0) &&
                 CombineRgn(interior, interior, copy, 1);
    ULONG eroded = ok ? CreateRectRgn(0, 0, 0, 0) : 0;
    ok = ok && eroded && CombineRgn(eroded, interior, 0, 5);
    for (LONG dy = -height; ok && dy <= height; dy++)
        if (dy)
            ok = CombineRgn(copy, eroded, 0, 5) && OffsetRgn(copy, 0, dy) &&
                 CombineRgn(interior, interior, copy, 1);
    if (eroded)
        open32DeleteRegion(eroded);
    if (ok)
        ok = CombineRgn(border, region, interior, 4) && FillRgn(h, border, brush);
    if (border)
        open32DeleteRegion(border);
    if (copy)
        open32DeleteRegion(copy);
    if (interior)
        open32DeleteRegion(interior);
    return ok;
}
LONG GetUpdateRgn(ULONG window, ULONG output, BOOL erase)
{
    PmRegion raw = {0};
    LONG height = 0;
    LxHandleBorrow borrow = {0};
    BOOL found = lx_pm_window_damage(open32ClientWindow(window), &raw.rects, &raw.count, &height);
    if (!found || !lx_handle_borrow(output, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        free(raw.rects);
        open32SetError(found ? 6 : 1400);
        return 0;
    }
    BOOL ok = !raw.count || raw.rects;
    if (ok)
    {
        for (size_t i = 0; i < raw.count; i++)
        {
            const RECTL damage = raw.rects[i];
            raw.rects[i] =
                (RECTL){damage.xLeft, height - damage.yTop, damage.xRight, height - damage.yBottom};
        }
        ok = regionBoolean(borrow.resource, &raw, NULL, 2);
    }
    free(raw.rects);
    LONG result = ok ? region_type(borrow.resource) : 0;
    lx_handle_release(&borrow);
    if (ok && erase && result > 1)
        GetUpdateRect(window, NULL, TRUE);
    if (!ok)
        open32SetError(8);
    return result;
}
