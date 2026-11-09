/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Colours of a presentation space: the default colour indexes, the logical colour table, RGB
   mode and palettes. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"

void os2ColorIndexToRGB(const LONG os2color, uint8 *r, uint8 *g, uint8 *b)
{
    assert(r != NULL);
    assert(g != NULL);
    assert(b != NULL);
#define RETURNRGB(rr, gg, bb)                                                                      \
    {                                                                                              \
        *r = rr;                                                                                   \
        *g = gg;                                                                                   \
        *b = bb;                                                                                   \
        return;                                                                                    \
    }
    switch (os2color)
    {
        case CLR_FALSE:
            FIXME("this should reverse on a printer");
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_TRUE:
            FIXME("this should reverse on a printer");
            RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_DEFAULT:
            FIXME("Let users specify this on command line or system-wide .ini?");
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_WHITE:
            RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_BLACK:
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_BACKGROUND:
            FIXME("Let users specify this on command line or system-wide .ini?");
            RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_BLUE:
            RETURNRGB(0x00, 0x00, 0xFF);
        case CLR_RED:
            RETURNRGB(0xFF, 0x00, 0x00);
        case CLR_PINK:
            RETURNRGB(0xFF, 0x00, 0xFF);
        case CLR_GREEN:
            RETURNRGB(0x00, 0xFF, 0x00);
        case CLR_CYAN:
            RETURNRGB(0x00, 0xFF, 0xFF);
        case CLR_YELLOW:
            RETURNRGB(0xFF, 0xFF, 0x00);
        case CLR_NEUTRAL:
            FIXME("Let users specify this on command line or system-wide .ini?");
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_DARKGRAY:
            RETURNRGB(0x80, 0x80, 0x80);
        case CLR_DARKBLUE:
            RETURNRGB(0x00, 0x00, 0x80);
        case CLR_DARKRED:
            RETURNRGB(0x80, 0x00, 0x00);
        case CLR_DARKPINK:
            RETURNRGB(0x80, 0x00, 0x80);
        case CLR_DARKGREEN:
            RETURNRGB(0x00, 0x80, 0x00);
        case CLR_DARKCYAN:
            RETURNRGB(0x00, 0x80, 0x80);
        case CLR_BROWN:
            RETURNRGB(0x80, 0x80, 0x00);
        case CLR_PALEGRAY:
            RETURNRGB(0xCC, 0xCC, 0xCC);
        default:
            break;
    }

    FIXME("unknown color index");
    RETURNRGB(0x00, 0x00, 0x00);
#undef RETURNRGB
}

LONG lx_pm_rgb(LONG index)
{
    if (index < CLR_FALSE || index > CLR_PALEGRAY)
        return -255;
    uint8 r, g, b;
    os2ColorIndexToRGB(index, &r, &g, &b);
    return (LONG)((ULONG)r << 16 | (ULONG)g << 8 | b);
}

LONG lx_pm_color(HPS hps, LONG color)
{
    PresentationSpace *ps = lx_pm_font_space(hps);
    if (!ps || (ps->rgb_colors
                    ? color < CLR_FALSE || color > 0xffffff
                    : color < CLR_FALSE ||
                          (color > CLR_PALEGRAY &&
                           !((ps->palette_lease.resource &&
                              (ULONG)color < ((PmPalette *)ps->palette_lease.resource)->count) ||
                             (color < 256 && ps->color_valid[color])))))
        return -255;
    LONG previous = ps->color;
    ps->color = color;
    for (unsigned i = 0; i < 5; i++)
        ps->primitive_color[i] = color;
    return previous;
}

LONG lx_pm_resolve_color(HPS handle, LONG color)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return -255;
    if (color < CLR_FALSE)
        return color >= -50 ? WinQuerySysColor(HWND_DESKTOP, color, 0) : -255;
    if (color < 0)
        return lx_pm_rgb(color);
    if (ps->rgb_colors)
        return color <= 0xffffff ? color : -255;
    PmPalette *palette = ps->palette_lease.resource;
    if (palette && (ULONG)color < palette->count)
        return palette->colors[color] & 0xffffff;
    if (color < 256 && ps->color_valid[color])
        return ps->colors[color];
    return lx_pm_rgb(color);
}

BOOL lx_pm_color_table(HPS handle, ULONG options, LONG format, LONG start, LONG count,
                       const LONG *colors)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || (options & ~1u) || format < 0 || format > 3)
        return FALSE;
    if (format == 3)
    {
        /* RGB mode ignores the palette range and table arguments. */
        LONG current = lx_pm_resolve_color(handle, ps->color);
        ps->rgb_colors = TRUE;
        ps->color_format = format;
        ps->color = current < 0 ? 0 : current;
        return TRUE;
    }
    if (count < 0 || (count && !colors))
        return FALSE;
    if (format == 1)
    {
        /* INDRGB counts LONGs, not pairs; lStart is ignored. Validate the
           entire array before changing the logical color table. */
        if (count > 512 || (count & 1))
            return FALSE;
        for (LONG i = 0; i < count; i += 2)
            if (colors[i] < 0 || colors[i] >= 256 || colors[i + 1] < 0 || colors[i + 1] > 0xffffff)
                return FALSE;
    }
    else
    {
        if (start < 0 || start > 256 || count > 256 - start)
            return FALSE;
        for (LONG i = 0; i < count; i++)
            if (colors[i] < 0 || colors[i] > 0xffffff)
                return FALSE;
    }
    /* Changing RGB to either indexed format implies LCOL_RESET, including
       the zero-entry switch used by the original Lotus caption procedure. */
    if (options & 1 || format == 0 || ps->rgb_colors)
        memset(ps->color_valid, 0, sizeof ps->color_valid);
    ps->rgb_colors = FALSE;
    ps->color_format = format;
    for (LONG i = 0; i < count; i += format == 1 ? 2 : 1)
    {
        LONG index = format == 1 ? colors[i] : start + i;
        ps->colors[index] = colors[i + (format == 1)];
        ps->color_valid[index] = TRUE;
    }
    return TRUE;
}

LONG WinRealizePalette(HWND handle, HPS ps, PULONG changed)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || !getWindowFromHWND(a, handle) || !lx_pm_valid_ps(ps))
        return -1;
    if (changed)
        *changed =
            0; /* True-color surfaces realize palette RGB values without changing display entries. */
    return 0;
}
