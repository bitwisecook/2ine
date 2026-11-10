/* Public domain. No rights reserved. Wholly AI-authored. */
/* Colour queries and palettes: GpiQueryPel, nearest and realised colours,
   GpiCreatePalette and its relatives. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include <math.h>
#include "os2/ps/pmgpi/internal.h"

LONG GpiQueryPel(HPS h, PPOINTL point)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !point)
        return GPI_ALTERROR;
    POINTL v = *point;
    uint32_t rgb;
    if (!lx_pm_transform_point(p, &v, TRUE) || !lx_pm_clip_point(h, v.x, v.y) ||
        !lx_pm_raster_read(h, v.x, v.y, 1, 1, &rgb))
        return GPI_ALTERROR;
    rgb &= RGB_WHITE;
    if (p->rgb_colors)
        return (LONG)rgb;
    LONG index = GpiQueryColorIndex(h, 0, (LONG)rgb);
    return lx_pm_resolve_color(h, index) == (LONG)rgb ? index : CLR_NOINDEX;
}

LONG GpiQueryNearestColor(HPS h, ULONG options, LONG rgb)
{
    return gpi_space(h) && !(options & ~LCOLOPT_REALIZED) && rgb >= 0 && rgb <= RGB_WHITE
               ? rgb
               : GPI_ALTERROR;
}

LONG GpiQueryRealColors(HPS h, ULONG options, LONG first, LONG count, PLONG colors)
{
    if (!gpi_space(h) || (options & ~(LCOLOPT_REALIZED | LCOLOPT_INDEX)) || first < 0 ||
        count < 0 || (count && !colors))
        return GPI_ALTERROR;
    BOOL pairs = (options & LCOLOPT_INDEX) != 0;
    LONG n = 0;
    for (LONG i = first; i < GPI_MAX_PALETTE_COLORS && n + (pairs ? 2 : 1) <= count; i++)
    {
        LONG c = GpiQueryRGBColor(h, 0, i);
        if (c < 0)
            break;
        if (pairs)
            colors[n++] = i;
        colors[n++] = c;
    }
    return n;
}

/* Bounds the host allocation for one palette. */
enum
{
    MAX_PALETTE_ENTRIES = 16 * 1024 * 1024
};

ULONG GpiCreatePalette(HAB hab, ULONG options, ULONG format, ULONG count, PULONG colors)
{
    if (!lxHostThreadState()->anchor_block ||
        (options & ~(LCOL_PURECOLOR | LCOL_OVERRIDE_DEFAULT_COLORS)) || format != LCOLF_CONSECRGB ||
        !count || count > MAX_PALETTE_ENTRIES || !colors)
        return 0;
    PmPalette *p = calloc(1, sizeof *p + (size_t)count * sizeof *p->colors);
    if (!p)
        return 0;
    p->count = count;
    for (ULONG i = 0; i < count; i++)
        p->colors[i] = colors[i];
    ULONG h = lx_handle_create(LX_HANDLE_PM_PALETTE, p, free);
    if (!h)
        free(p);
    return h;
}

BOOL GpiDeletePalette(ULONG h)
{
    int closed = lx_handle_close_guarded(h, LX_HANDLE_PM_PALETTE, 0, NULL);
    if (closed != 1)
        WinSetErrorInfo(closed < 0 ? PMERR_PALETTE_SELECTED : PMERR_INV_HPAL, 0);
    return closed == 1;
}

ULONG GpiSelectPalette(HPS h, ULONG palette)
{
    PresentationSpace *p = gpi_space(h);
    LxHandleBorrow b = {0};
    if (!p || (palette && !lx_handle_borrow(palette, LX_HANDLE_PM_PALETTE, &b)))
        return PAL_ERROR;
    ULONG old = p->gpi.palette;
    lx_handle_release(&p->palette_lease);
    p->palette_lease = b;
    p->gpi.palette = palette;
    /* A palette overrides any loaded table; deselecting restores the default table. */
    if (palette || old)
    {
        memset(p->color_valid, 0, sizeof p->color_valid);
        p->rgb_colors = FALSE;
        p->color_format = palette ? LCOLF_PALETTE : LCOLF_DEFAULT;
    }
    return old;
}

ULONG GpiQueryPalette(HPS h)
{
    PresentationSpace *p = gpi_space(h);
    return p ? p->gpi.palette : PAL_ERROR;
}

LONG GpiQueryPaletteInfo(ULONG handle, HPS h, ULONG options, ULONG start, ULONG count,
                         PULONG colors)
{
    LxHandleBorrow b = {0};
    if ((options & ~LCOLOPT_INDEX) || (count && !colors))
        return PAL_ERROR;
    PmPalette *p = NULL;
    if (handle)
    {
        if (!lx_handle_borrow(handle, LX_HANDLE_PM_PALETTE, &b))
            return PAL_ERROR;
        p = b.resource;
    }
    else
    {
        PresentationSpace *ps = gpi_space(h);
        if (!ps)
            return PAL_ERROR;
        p = ps->palette_lease.resource;
    }
    LONG n = 0;
    if (p)
    {
        BOOL pairs = (options & LCOLOPT_INDEX) != 0;
        ULONG available = start < p->count ? p->count - start : 0;
        if (!count)
            n = (LONG)(available * (pairs ? 2 : 1));
        else
            for (ULONG i = start; i < p->count && (ULONG)n + (pairs ? 2 : 1) <= count; i++)
            {
                if (pairs)
                    colors[n++] = i;
                colors[n++] = p->colors[i];
            }
    }
    lx_handle_release(&b);
    return n;
}
