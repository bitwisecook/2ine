/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinScrollWindow moves window contents and invalidates the uncovered area. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/bsedos.h"

enum
{
    SCROLL_MAX_PIXELS = 16777216 /* bounds the temporary copy of the retained area */
};

static RECTL intersect(RECTL a, RECTL b)
{
    return (RECTL){a.xLeft > b.xLeft ? a.xLeft : b.xLeft,
                   a.yBottom > b.yBottom ? a.yBottom : b.yBottom,
                   a.xRight < b.xRight ? a.xRight : b.xRight, a.yTop < b.yTop ? a.yTop : b.yTop};
}

static BOOL nonempty(RECTL a)
{
    return a.xLeft < a.xRight && a.yBottom < a.yTop;
}

LONG WinScrollWindow(HWND handle, LONG dx, LONG dy, PRECTL scroll, PRECTL clip, ULONG region,
                     PRECTL update, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || flags & ~(ULONG)(SW_SCROLLCHILDREN | SW_INVALIDATERGN))
        return 0;
    RECTL bounds = {0, 0, window->w, window->h};
    if (scroll)
        bounds = intersect(bounds, *scroll);
    if (clip)
        bounds = intersect(bounds, *clip);
    if (update)
        *update = (RECTL){0};
    HPS ps = WinGetPS(handle);
    if (!ps)
        return 0;
    if (!nonempty(bounds) || (!dx && !dy))
    {
        LONG result = !region || lx_pm_region_set(ps, region, 0, NULL) ? RGN_NULL : RGN_ERROR;
        WinReleasePS(ps);
        return result;
    }
    int64_t left = (int64_t)bounds.xLeft + dx, right = (int64_t)bounds.xRight + dx,
            bottom = (int64_t)bounds.yBottom + dy, top = (int64_t)bounds.yTop + dy;
    if (left < INT32_MIN || right > INT32_MAX || bottom < INT32_MIN || top > INT32_MAX)
    {
        WinReleasePS(ps);
        return 0;
    }
    RECTL retained = intersect(bounds, (RECTL){left, bottom, right, top});
    BOOL ok = TRUE;
    if (nonempty(retained))
    {
        unsigned width = retained.xRight - retained.xLeft,
                 height = retained.yTop - retained.yBottom;
        uint32_t *pixels = (uint64_t)width * height <= SCROLL_MAX_PIXELS
                               ? malloc((size_t)width * height * sizeof *pixels)
                               : NULL;
        ok = pixels &&
             lx_pm_raster_read(ps, retained.xLeft - dx, retained.yBottom - dy, width, height,
                               pixels) &&
             lx_pm_raster_write(ps, retained.xLeft, retained.yBottom, width, height, pixels);
        free(pixels);
    }
    RECTL uncovered[4];
    LONG count = 0;
    if (!nonempty(retained))
        uncovered[count++] = bounds;
    else
    {
        RECTL candidates[] = {{bounds.xLeft, bounds.yBottom, bounds.xRight, retained.yBottom},
                              {bounds.xLeft, retained.yTop, bounds.xRight, bounds.yTop},
                              {bounds.xLeft, retained.yBottom, retained.xLeft, retained.yTop},
                              {retained.xRight, retained.yBottom, bounds.xRight, retained.yTop}};
        for (unsigned i = 0; i < 4; i++)
            if (nonempty(candidates[i]))
                uncovered[count++] = candidates[i];
    }
    if (ok && region)
        ok = lx_pm_region_set(ps, region, count, uncovered) != 0;
    if (ok && count && update)
    {
        *update = uncovered[0];
        for (LONG i = 1; i < count; i++)
        {
            if (uncovered[i].xLeft < update->xLeft)
                update->xLeft = uncovered[i].xLeft;
            if (uncovered[i].yBottom < update->yBottom)
                update->yBottom = uncovered[i].yBottom;
            if (uncovered[i].xRight > update->xRight)
                update->xRight = uncovered[i].xRight;
            if (uncovered[i].yTop > update->yTop)
                update->yTop = uncovered[i].yTop;
        }
    }
    WinReleasePS(ps);
    if (ok && flags & SW_SCROLLCHILDREN)
        for (Window *child = window->children; child; child = child->sibling)
            if (nonempty(intersect(
                    bounds, (RECTL){child->x, child->y, child->x + child->w, child->y + child->h})))
                if (!WinSetWindowPos(child->hwnd, 0, child->x + dx, child->y + dy, 0, 0, SWP_MOVE))
                    ok = FALSE;
    if (ok && flags & SW_INVALIDATERGN)
        for (LONG i = 0; i < count; i++)
            WinInvalidateRect(handle, &uncovered[i], FALSE);
    return ok ? count ? count == 1 ? RGN_RECT : RGN_COMPLEX : RGN_NULL : RGN_ERROR;
}
