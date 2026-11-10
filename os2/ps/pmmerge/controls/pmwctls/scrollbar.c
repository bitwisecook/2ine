/* Public domain. No rights reserved. Wholly AI-authored. */
/* Scrollbar ranges, proportional thumbs and owner scroll notifications. */
#include "os2/include/private/pmwctlp.h"
#include "emulator/abi.h"

enum
{
    /* SBCDATA in guest memory; without SBS_THUMBSIZE it may end before cVisible. */
    SBCDATA_CB_OFFSET = 0,
    SBCDATA_HILITE_OFFSET = 2,
    SBCDATA_POSFIRST_OFFSET = 4,
    SBCDATA_POSLAST_OFFSET = 6,
    SBCDATA_POSTHUMB_OFFSET = 8,
    SBCDATA_CVISIBLE_OFFSET = 10,
    SBCDATA_CTOTAL_OFFSET = 12,
    SBCDATA_SIZE = 14,
    /* A thumb never shrinks below this many pixels unless the track does. */
    SCROLLBAR_MIN_THUMB = 4,
    /* Each arrow is a three-row triangle, five pixels at its base. */
    ARROW_GLYPH_ROWS = 3,
    ARROW_GLYPH_MIN_SIZE = 5
};

static LONG bound(LONG value, LONG low, LONG high)
{
    return value < low ? low : value > high ? high : value;
}

static void range(PmControl *c, LONG first, LONG last, LONG position)
{
    c->lower = first < 0 ? 0 : first;
    c->upper = last < c->lower ? c->lower : last;
    c->value = bound(position, c->lower, c->upper);
}

BOOL initializeScrollbar(Window *window, MPARAM data)
{
    PmControl *c = window->control;
    range(c, 0, 0, 0);
    if (!data)
        return TRUE;
    uint8 bytes[SBCDATA_SIZE];
    if (!controlRead(data, bytes, sizeof(USHORT)))
        return FALSE;
    unsigned size = lx_guest_load_u16(bytes + SBCDATA_CB_OFFSET);
    if (size < ((window->style & SBS_THUMBSIZE) ? (unsigned)SBCDATA_SIZE
                                                : (unsigned)SBCDATA_CVISIBLE_OFFSET) ||
        size > sizeof bytes || !controlRead(data, bytes, size) ||
        lx_guest_load_u16(bytes + SBCDATA_HILITE_OFFSET))
        return FALSE;
    range(c, (SHORT)lx_guest_load_u16(bytes + SBCDATA_POSFIRST_OFFSET),
          (SHORT)lx_guest_load_u16(bytes + SBCDATA_POSLAST_OFFSET),
          (SHORT)lx_guest_load_u16(bytes + SBCDATA_POSTHUMB_OFFSET));
    if (window->style & SBS_THUMBSIZE)
    {
        SHORT visible = (SHORT)lx_guest_load_u16(bytes + SBCDATA_CVISIBLE_OFFSET);
        SHORT total = (SHORT)lx_guest_load_u16(bytes + SBCDATA_CTOTAL_OFFSET);
        c->first = visible > 0 ? (unsigned)visible : 0;
        c->last = total > 0 ? (unsigned)total : 0;
    }
    return TRUE;
}

static void geometry(Window *w, LONG *arrow, LONG *start, LONG *thumb, LONG *travel)
{
    PmControl *c = w->control;
    BOOL vertical = (w->style & SBS_VERT) != 0;
    LONG length = vertical ? w->h : w->w;
    *arrow = vertical ? w->w : w->h;
    if (*arrow > length / 2)
        *arrow = length / 2;
    LONG track = length - *arrow * 2;
    *thumb = c->last ? (LONG)((int64_t)track * c->first / c->last) : *arrow;
    *thumb = bound(*thumb, track < SCROLLBAR_MIN_THUMB ? track : SCROLLBAR_MIN_THUMB, track);
    *travel = track - *thumb;
    *start =
        *arrow + (c->upper > c->lower
                      ? (LONG)((int64_t)(c->value - c->lower) * *travel / (c->upper - c->lower))
                      : 0);
}

static RECTL axis(Window *w, LONG first, LONG last)
{
    return (w->style & SBS_VERT) ? (RECTL){0, w->h - last, w->w, w->h - first}
                                 : (RECTL){first, 0, last, w->h};
}

static void paint(Window *window)
{
    HPS ps = WinBeginPaint(window->hwnd, 0, NULL);
    if (!ps)
        return;
    LONG arrow, start, thumb, travel;
    geometry(window, &arrow, &start, &thumb, &travel);
    (void)travel;
    LONG length = window->style & SBS_VERT ? window->h : window->w;
    RECTL whole = {0, 0, window->w, window->h};
    WinFillRect(ps, &whole, CLR_DARKGRAY);
    RECTL parts[] = {axis(window, 0, arrow), axis(window, length - arrow, length),
                     axis(window, start, start + thumb)};
    for (unsigned i = 0; i < 3; i++)
    {
        WinFillRect(ps, &parts[i], CLR_PALEGRAY);
        WinDrawBorder(ps, &parts[i], 1, 1, CLR_BLACK, CLR_PALEGRAY, 0);
    }
    for (unsigned i = 0; i < 2 && arrow >= ARROW_GLYPH_MIN_SIZE; i++)
        for (LONG row = 0; row < ARROW_GLYPH_ROWS; row++)
        {
            LONG middle = (i ? length - arrow : 0) + arrow / 2;
            LONG at = middle + (i ? 1 - row : row - 1);
            RECTL line = axis(window, at, at + 1);
            LONG half = window->style & SBS_VERT ? window->w / 2 : window->h / 2;
            if (window->style & SBS_VERT)
                line.xLeft = half - row, line.xRight = half + row + 1;
            else
                line.yBottom = half - row, line.yTop = half + row + 1;
            WinFillRect(ps, &line, window->enabled ? CLR_BLACK : CLR_DARKGRAY);
        }
    WinEndPaint(ps);
}

static void notify(Window *w, USHORT command, SHORT position)
{
    if (w->owner)
        WinPostMsg(w->owner->hwnd, (w->style & SBS_VERT) ? WM_VSCROLL : WM_HSCROLL,
                   (MPARAM)(uintptr_t)(USHORT)w->id,
                   (MPARAM)(uintptr_t)((USHORT)position | (ULONG)command << 16));
}

MRESULT scrollbarMessage(Window *w, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = w->control;
    ULONG p = (ULONG)(uintptr_t)first, q = (ULONG)(uintptr_t)second;
    if (message == SBM_QUERYPOS)
        return controlReply(c->value);
    if (message == SBM_QUERYRANGE)
        return (MRESULT)(uintptr_t)((USHORT)c->lower | (ULONG)(USHORT)c->upper << 16);
    if (message == SBM_SETSCROLLBAR || message == SBM_SETPOS)
    {
        if (message == SBM_SETSCROLLBAR)
            range(c, (SHORT)q, (SHORT)(q >> 16), (SHORT)p);
        else
            c->value = bound((SHORT)p, c->lower, c->upper);
        controlRepaint(w);
        return controlReply(TRUE);
    }
    if (message == SBM_SETTHUMBSIZE)
    {
        c->first = (SHORT)p > 0 ? (unsigned)(SHORT)p : 0;
        c->last = (SHORT)(p >> 16) > 0 ? (unsigned)(SHORT)(p >> 16) : 0;
        WinEnableWindow(w->hwnd, c->first < c->last);
        controlRepaint(w);
        return controlReply(TRUE);
    }
    if (message == WM_PAINT)
    {
        paint(w);
        return 0;
    }
    if (message == WM_BUTTON1DOWN || message == WM_MOUSEMOVE || message == WM_BUTTON1UP)
    {
        LONG arrow, start, thumb, travel;
        geometry(w, &arrow, &start, &thumb, &travel);
        LONG at = (w->style & SBS_VERT) ? w->h - 1 - (SHORT)(p >> 16) : (SHORT)p;
        LONG length = w->style & SBS_VERT ? w->h : w->w;
        if (message == WM_BUTTON1DOWN)
        {
            if (at >= start && at < start + thumb && travel)
            {
                c->pressed = TRUE;
                c->drag_offset = at - start;
                c->view = (unsigned)c->value;
                WinSetCapture(HWND_DESKTOP, w->hwnd);
            }
            else
                notify(w,
                       at < arrow             ? SB_LINEUP
                       : at >= length - arrow ? SB_LINEDOWN
                       : at < start           ? SB_PAGEUP
                                              : SB_PAGEDOWN,
                       (SHORT)c->value);
        }
        else if (message == WM_MOUSEMOVE && c->pressed && travel)
        {
            LONG offset = bound(at - c->drag_offset - arrow, 0, travel);
            c->view = (unsigned)(c->lower + (int64_t)offset * (c->upper - c->lower) / travel);
            if (w->style & SBS_AUTOTRACK)
            {
                c->value = (LONG)c->view;
                controlRepaint(w);
            }
            notify(w, SB_SLIDERTRACK, (SHORT)c->view);
        }
        else if (message == WM_BUTTON1UP)
        {
            if (c->pressed)
            {
                c->pressed = FALSE;
                WinSetCapture(HWND_DESKTOP, 0);
                notify(w, SB_SLIDERPOSITION, (SHORT)c->view);
            }
            notify(w, SB_ENDSCROLL, (SHORT)c->value);
        }
        return 0;
    }
    return WinDefWindowProc(w->hwnd, message, first, second);
}
