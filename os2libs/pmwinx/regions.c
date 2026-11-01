/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows regions retain top-left coordinates; selected clips are copied into GPI. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
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
