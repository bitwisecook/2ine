/* Public domain. No rights reserved. Wholly AI-authored. */
/* Presentation-space state is shared by paint and retained GPI drawing. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"

HDC WinOpenWindowDC(HWND hwnd)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getWindowFromHWND(anchor, hwnd) ? hwnd : 0;
}

HDC WinQueryWindowDC(HWND hwnd)
{
    return WinOpenWindowDC(hwnd);
}

HPS lx_pm_create_ps(HAB hab, HDC dc)
{
    if (!getAnchorBlock(hab))
        return 0;
    if (dc && !lx_pm_dc_valid(dc))
        return 0;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    HPS result =
        acquirePresentationSpace(getWindowFromHWND(anchor, dc) ? dc : HWND_DESKTOP, 0, NULL, FALSE);
    PresentationSpace *ps = result ? getPresentationSpaceFromHPS(anchor, result) : NULL;
    if (ps && lx_pm_associate_dc(result, dc))
        ps->persistent = 1;
    else if (ps)
    {
        destroyPresentationSpace(anchor, ps);
        result = 0;
    }
    return result;
}

BOOL lx_pm_destroy_ps(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps)
        return FALSE;
    destroyPresentationSpace(anchor, ps);
    return TRUE;
}

BOOL lx_pm_valid_ps(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getPresentationSpaceFromHPS(anchor, hps);
}

LONG lx_pm_color(HPS hps, LONG color)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
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

BOOL lx_pm_record_point(PresentationSpace *ps, const POINTL *point, BOOL move)
{
    PmPath *path = ps->record_path;
    if (!path)
        return TRUE;
    if (path->count == path->capacity)
    {
        size_t capacity = path->capacity ? path->capacity * 2 : 64;
        if (capacity > 65536)
            return FALSE;
        PmPathPoint *points = realloc(path->points, capacity * sizeof *points);
        if (!points)
            return FALSE;
        path->points = points;
        path->capacity = capacity;
    }
    POINTL device = *point;
    if (!lx_pm_transform_point(ps, &device, TRUE))
        return FALSE;
    path->points[path->count++] = (PmPathPoint){*point, move, device};
    return TRUE;
}

BOOL lx_pm_move(HPS hps, const POINTL *point)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps || !point)
        return FALSE;
    if (!lx_pm_record_point(ps, point, TRUE))
        return FALSE;
    ps->position = *point;
    return TRUE;
}

BOOL lx_pm_erase(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps || (!ps->window && !ps->bitmap_lease.resource))
        return FALSE;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    RECTL rect = {0, 0, bitmap ? (LONG)bitmap->width : ps->window->w,
                  bitmap ? (LONG)bitmap->height : ps->window->h};
    return WinFillRect(hps, &rect, CLR_BACKGROUND);
}

HPS WinGetPS(HWND window)
{
    return acquirePresentationSpace(window, 0, NULL, FALSE);
}

HPS lx_pm_get_clip_ps(HWND window, ULONG flags)
{
    HPS handle = WinGetPS(window);
    PresentationSpace *ps = handle ? lx_pm_font_space(handle) : NULL;
    if (ps)
        ps->clip_flags = flags;
    return handle;
}

HPS WinGetScreenPS(HWND desktop)
{
    if (desktop != HWND_DESKTOP)
        return 0;
    return WinGetPS(desktop);
}

BOOL WinReleasePS(HPS space)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, space) : NULL;
    if (!ps || ps->persistent)
        return FALSE;
    if (ps->window && ps->window->heavy.window)
        presentWindow(anchor, ps->window);
    destroyPresentationSpace(anchor, ps);
    return TRUE;
}

LONG lx_pm_rgb(LONG index)
{
    if (index < CLR_FALSE || index > CLR_PALEGRAY)
        return -255;
    uint8 r, g, b;
    os2ColorIndexToRGB(index, &r, &g, &b);
    return (LONG)((ULONG)r << 16 | (ULONG)g << 8 | b);
}

LxHostFont *lx_pm_font(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps)
        return NULL;
    unsigned dpi = lx_pm_dc_dpi(ps->dc);
    unsigned index = 0, pixels = (16 * dpi + 48) / 96;
    char attribute[128] = {0};
    ULONG bytes = ps->window ? WinQueryPresParam(ps->window->hwnd, 15, 0, NULL,
                                                 sizeof attribute - 1, attribute, 0)
                             : 0;
    if (bytes && memchr(attribute, 0, bytes))
    {
        char *end;
        unsigned long points = strtoul(attribute, &end, 10);
        if (points && points <= 384 && *end == '.')
        {
            const char *family = end + 1;
            pixels = (unsigned)(points * dpi + 36) / 72;
            if (!strncasecmp(family, "Courier", 7) || !strncasecmp(family, "System Monospaced", 17))
                index = 8;
            else if (!strncasecmp(family, "Tms Rmn", 7) ||
                     !strncasecmp(family, "Times New Roman", 15))
                index = 4;
            unsigned style = 0;
            for (const char *p = family; *p; p++)
            {
                if (!strncasecmp(p, "Bold", 4))
                    style |= 1;
                if (!strncasecmp(p, "Italic", 6))
                    style |= 2;
            }
            index += style;
        }
    }
    if (ps->logical_fonts[ps->charset].defined)
    {
        index = ps->logical_fonts[ps->charset].index;
        pixels = ps->logical_fonts[ps->charset].pixels;
    }
    if (ps->char_height > 0)
        pixels = (unsigned)((ps->char_height + 32768LL) / 65536);
    if (!pixels)
        pixels = 1;
    if (!ps->font || ps->font_index != index || ps->font_pixels != pixels)
    {
        LxHostFont *font = lx_host_font_open(index, pixels);
        if (!font)
            return NULL;
        lx_host_font_close(ps->font);
        ps->font = font;
        ps->font_index = index;
        ps->font_pixels = pixels;
    }

    return ps->font;
}

unsigned lx_pm_font_index(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    return ps ? ps->logical_fonts[ps->charset].defined ? ps->logical_fonts[ps->charset].metric_index
                                                       : ps->font_index
              : 0;
}

BOOL lx_pm_set_cp(HPS handle, ULONG codepage)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
    if (!ps || (codepage && codepage != 437))
        return FALSE;
    ps->codepage = codepage ? codepage : 437;
    return TRUE;
}

ULONG lx_pm_query_cp(HPS handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
    return ps ? ps->codepage : 0;
}

LONG lx_pm_resolve_color(HPS handle, LONG color)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
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
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
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

PresentationSpace *lx_pm_font_space(HPS handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
}
