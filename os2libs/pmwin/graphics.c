/* Public domain. No rights reserved. Wholly AI-authored. */
/* Presentation-space state is shared by paint and retained GPI drawing. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
HDC WinOpenWindowDC(HWND hwnd)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getWindowFromHWND(anchor, hwnd) ? hwnd : 0;
}
HPS lx_pm_create_ps(HAB hab, HDC dc)
{
    if (!getAnchorBlock(hab))
        return 0;
    HPS result = WinBeginPaint(dc, 0, NULL);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = result ? getPresentationSpaceFromHPS(anchor, result) : NULL;
    if (ps)
        ps->persistent = 1;
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
    if (!ps || color < CLR_FALSE || color > CLR_PALEGRAY)
        return -255;
    LONG previous = ps->color;
    ps->color = color;
    return previous;
}
BOOL lx_pm_move(HPS hps, const POINTL *point)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps || !point)
        return FALSE;
    ps->position = *point;
    return TRUE;
}
LONG lx_pm_line(HPS hps, const POINTL *point, int pel)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps || !point || !ps->window->heavy.window)
        return 0;
    LxHostRect clip;
    calcHeavyweightRect(ps->window, &clip);
    uint8 r, g, b;
    os2ColorIndexToRGB(ps->color, &r, &g, &b);
    POINTL from = pel ? *point : ps->position;
    int ok = lx_host_window_line(ps->window->heavy.window, &clip, clip.x + from.x,
                                 clip.y + clip.h - 1 - from.y, clip.x + point->x,
                                 clip.y + clip.h - 1 - point->y, r, g, b);
    if (!pel)
        ps->position = *point;
    return ok ? 1 : 0;
}

BOOL lx_pm_erase(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps)
        return FALSE;
    RECTL rect = {0, 0, ps->window->w, ps->window->h};
    return WinFillRect(hps, &rect, CLR_BACKGROUND);
}

HPS WinGetPS(HWND window)
{
    return WinBeginPaint(window, 0, NULL);
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
    if (ps->window->heavy.window)
        lx_host_window_present(ps->window->heavy.window);
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
    unsigned index = 0, pixels = 16;
    char attribute[128] = {0};
    ULONG bytes =
        WinQueryPresParam(ps->window->hwnd, 15, 0, NULL, sizeof attribute - 1, attribute, 0);
    if (bytes && memchr(attribute, 0, bytes))
    {
        char *end;
        unsigned long points = strtoul(attribute, &end, 10);
        if (points && points <= 384 && *end == '.')
        {
            const char *family = end + 1;
            pixels = (unsigned)(points * 96 + 36) / 72;
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
    return ps ? ps->font_index : 0;
}
