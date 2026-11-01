/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
PresentationSpace *getPresentationSpaceFromHPS(const AnchorBlock *anchor, const HPS hps)
{
    if (hps >= FIRST_HPS_VALUE)
    {
        const size_t idx = (size_t)(hps - FIRST_HPS_VALUE);
        if (idx >= anchor->pres_spaces_array_len)
        {
            return NULL;
        }
        PresentationSpace *ps = &anchor->pres_spaces[idx];
        return (ps->hps == hps) ? ps : NULL;
    }
    return NULL;
}

void destroyPresentationSpace(AnchorBlock *anchor, PresentationSpace *ps)
{
    if (!ps || (ps->hps == NULLHANDLE))
    {
        return;
    }
    lx_host_font_close(ps->font);
    memset(ps, '\0', sizeof(*ps));
}

HPS WinBeginPaint(HWND hwnd, HPS hps, PRECTL prclPaint)
{
    TRACE_NATIVE("WinBeginPaint(%u, %u, %p)", (uint)hwnd, (uint)hps, prclPaint);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }
    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
    }

    PresentationSpace *ps = NULL;
    if (hps != NULLHANDLE)
    {
        ps = getPresentationSpaceFromHPS(anchor, hps);
        if (!ps)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
        }
        FIXME("check if this is already assigned to this drawable");
    }
    else
    {

        FIXME("optimize this");
        FIXME("cut-and-paste from WinCreateWindow");
        for (size_t i = 0; i < anchor->pres_spaces_array_len; i++)
        {
            if (anchor->pres_spaces[i].hps == NULLHANDLE)
            {
                ps = &anchor->pres_spaces[i];
                break;
            }
        }

        if (!ps)
        {
            const size_t len = anchor->pres_spaces_array_len;
            void *ptr = realloc(anchor->pres_spaces, sizeof(PresentationSpace) * len * 2);
            if (!ptr)
            {
                SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
            }
            anchor->pres_spaces = (PresentationSpace *)ptr;
            memset(&anchor->pres_spaces[len], '\0', sizeof(PresentationSpace) * len);
            ps = &anchor->pres_spaces[len];
            anchor->pres_spaces_array_len *= 2;
        }
        hps = (HPS)(((size_t)(ps - anchor->pres_spaces)) + FIRST_HPS_VALUE);
    }

    ps->hps = hps;
    ps->window = win;
    ps->color = CLR_BLACK;

    if (prclPaint)
    {
        FIXME("report the update region, not the whole window");
        prclPaint->xLeft = 0;
        prclPaint->yBottom = 0;
        prclPaint->xRight = win->w;
        prclPaint->yTop = win->h;
    }

    return hps;
}

BOOL WinEndPaint(HPS hps)
{
    TRACE_NATIVE("WinEndPaint(%u)", (uint)hps);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }
    PresentationSpace *ps = getPresentationSpaceFromHPS(anchor, hps);
    if (!ps)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
    }

    Window *win = ps->window;
    if (win->heavy.window)
        lx_host_window_present(win->heavy.window);

    if (!ps->persistent)
        destroyPresentationSpace(anchor, ps);
    return TRUE;
}

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

void calcHeavyweightRect(const Window *win, LxHostRect *rect)
{
    int x = 0, y = 0;

    rect->w = win->w;
    rect->h = win->h;

    while (win->parent && win->parent != &desktop_window)
    {
        x += win->x;
        y += win->y;
        win = win->parent;
    }

    rect->x = x;
    rect->y = (win->h - y) - rect->h;
}

BOOL WinFillRect(HPS hps, PRECTL prcl, LONG lColor)
{
    TRACE_NATIVE("WinFillRect(%u, %p, %d)", (uint)hps, prcl, (int)lColor);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }
    PresentationSpace *ps = getPresentationSpaceFromHPS(anchor, hps);
    if (!ps)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
    }
    if (!prcl)
        return FALSE;
    Window *win = ps->window;
    LxHostRect cliprect;
    calcHeavyweightRect(win, &cliprect);

    LxHostRect rect;
    rect.x = cliprect.x + prcl->xLeft;
    rect.y = cliprect.y + (cliprect.h - prcl->yTop);
    rect.w = prcl->xRight - prcl->xLeft;
    rect.h = prcl->yTop - prcl->yBottom;

    uint8 r, g, b;
    os2ColorIndexToRGB(lColor, &r, &g, &b);

    return win->heavy.window && lx_host_window_fill(win->heavy.window, &cliprect, &rect, r, g, b)
               ? TRUE
               : FALSE;
}
