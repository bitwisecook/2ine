/* Public domain. No rights reserved. Wholly AI-authored. */
/* Locale comparisons, window identity and scrolling used by the help manager. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/nls.h"

ULONG WinCompareStrings(HAB hab, ULONG codepage, ULONG country, PSZ first, PSZ second,
                        ULONG reserved)
{
    if (!getAnchorBlock(hab) || !first || !second || reserved)
        return 0;
    COUNTRYCODE code = {country, codepage};
    char weights[256];
    ULONG length;
    if (DosQueryCollate(sizeof weights, &code, weights, &length))
        return 0;
    const unsigned char *a = (const unsigned char *)first, *b = (const unsigned char *)second;
    while (*a && *b && weights[*a] == weights[*b])
    {
        a++;
        b++;
    }
    unsigned left = (unsigned char)weights[*a], right = (unsigned char)weights[*b];
    return left == right ? 1 : left < right ? 2 : 3;
}

HWND WinQueryDesktopWindow(HAB hab, HDC dc)
{
    return getAnchorBlock(hab) && (!dc || lx_pm_dc_valid(dc)) ? HWND_DESKTOP : 0;
}

BOOL WinQueryWindowProcess(HWND handle, PULONG process, PULONG thread)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    ULONG owner = lx_guest_load_u32(&LX_GETTIB2()->thread_id);
    if ((!anchor || !getWindowFromHWND(anchor, handle)) && !windowOwnerThread(handle, &owner))
        return FALSE;
    if (process)
        *process = GProcessState.pib.process_id;
    if (thread)
        *thread = owner;
    return TRUE;
}

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
    if (!window || flags & ~3u)
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
        LONG result = !region || lx_pm_region_set(ps, region, 0, NULL) ? 1 : 0;
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
        uint32_t *pixels = (uint64_t)width * height <= 16777216
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
    if (ok && flags & 1)
        for (Window *child = window->children; child; child = child->sibling)
            if (nonempty(intersect(
                    bounds, (RECTL){child->x, child->y, child->x + child->w, child->y + child->h})))
                if (!WinSetWindowPos(child->hwnd, 0, child->x + dx, child->y + dy, 0, 0, SWP_MOVE))
                    ok = FALSE;
    if (ok && flags & 2)
        for (LONG i = 0; i < count; i++)
            WinInvalidateRect(handle, &uncovered[i], FALSE);
    return ok ? count ? count == 1 ? 2 : 3 : 1 : 0;
}
