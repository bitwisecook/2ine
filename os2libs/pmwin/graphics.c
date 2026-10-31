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
    if (!ps || !point)
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
