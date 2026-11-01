/* Public domain. No rights reserved. Wholly AI-authored. */
/* Scrollbar ranges, proportional thumbs and owner scroll notifications. */
#include "os2libs/pmwin/controls.h"
#include "guest/abi.h"
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
    uint8 bytes[14];
    if (!controlRead(data, bytes, 2))
        return FALSE;
    unsigned size = lx_guest_load_u16(bytes);
    if (size < ((window->style & 2) ? 14u : 10u) || size > sizeof bytes ||
        !controlRead(data, bytes, size) || lx_guest_load_u16(bytes + 2))
        return FALSE;
    range(c, (SHORT)lx_guest_load_u16(bytes + 4), (SHORT)lx_guest_load_u16(bytes + 6),
          (SHORT)lx_guest_load_u16(bytes + 8));
    if (window->style & 2)
    {
        SHORT visible = (SHORT)lx_guest_load_u16(bytes + 10);
        SHORT total = (SHORT)lx_guest_load_u16(bytes + 12);
        c->first = visible > 0 ? (unsigned)visible : 0;
        c->last = total > 0 ? (unsigned)total : 0;
    }
    return TRUE;
}
static void geometry(Window *w, LONG *arrow, LONG *start, LONG *thumb, LONG *travel)
{
    PmControl *c = w->control;
    BOOL vertical = (w->style & 1) != 0;
    LONG length = vertical ? w->h : w->w;
    *arrow = vertical ? w->w : w->h;
    if (*arrow > length / 2)
        *arrow = length / 2;
    LONG track = length - *arrow * 2;
    *thumb = c->last ? (LONG)((int64_t)track * c->first / c->last) : *arrow;
    *thumb = bound(*thumb, track < 4 ? track : 4, track);
    *travel = track - *thumb;
    *start =
        *arrow + (c->upper > c->lower
                      ? (LONG)((int64_t)(c->value - c->lower) * *travel / (c->upper - c->lower))
                      : 0);
}
static RECTL axis(Window *w, LONG first, LONG last)
{
    return (w->style & 1) ? (RECTL){0, w->h - last, w->w, w->h - first}
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
    LONG length = window->style & 1 ? window->h : window->w;
    RECTL whole = {0, 0, window->w, window->h};
    WinFillRect(ps, &whole, CLR_DARKGRAY);
    RECTL parts[] = {axis(window, 0, arrow), axis(window, length - arrow, length),
                     axis(window, start, start + thumb)};
    for (unsigned i = 0; i < 3; i++)
    {
        WinFillRect(ps, &parts[i], CLR_PALEGRAY);
        WinDrawBorder(ps, &parts[i], 1, 1, CLR_BLACK, CLR_PALEGRAY, 0);
    }
    for (unsigned i = 0; i < 2 && arrow >= 5; i++)
        for (LONG row = 0; row < 3; row++)
        {
            LONG middle = (i ? length - arrow : 0) + arrow / 2;
            LONG at = middle + (i ? 1 - row : row - 1);
            RECTL line = axis(window, at, at + 1);
            LONG half = window->style & 1 ? window->w / 2 : window->h / 2;
            if (window->style & 1)
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
        WinPostMsg(w->owner->hwnd, (w->style & 1) ? WM_VSCROLL : WM_HSCROLL,
                   (MPARAM)(uintptr_t)(USHORT)w->id,
                   (MPARAM)(uintptr_t)((USHORT)position | (ULONG)command << 16));
}
MRESULT scrollbarMessage(Window *w, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = w->control;
    ULONG p = (ULONG)(uintptr_t)first, q = (ULONG)(uintptr_t)second;
    if (message == 0x1a2)
        return controlReply(c->value);
    if (message == 0x1a3)
        return (MRESULT)(uintptr_t)((USHORT)c->lower | (ULONG)(USHORT)c->upper << 16);
    if (message == 0x1a0 || message == 0x1a1)
    {
        if (message == 0x1a0)
            range(c, (SHORT)q, (SHORT)(q >> 16), (SHORT)p);
        else
            c->value = bound((SHORT)p, c->lower, c->upper);
        controlRepaint(w);
        return controlReply(TRUE);
    }
    if (message == 0x1a6)
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
        LONG at = (w->style & 1) ? w->h - 1 - (SHORT)(p >> 16) : (SHORT)p;
        LONG length = w->style & 1 ? w->h : w->w;
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
                       at < arrow             ? 1
                       : at >= length - arrow ? 2
                       : at < start           ? 3
                                              : 4,
                       (SHORT)c->value);
        }
        else if (message == WM_MOUSEMOVE && c->pressed && travel)
        {
            LONG offset = bound(at - c->drag_offset - arrow, 0, travel);
            c->view = (unsigned)(c->lower + (int64_t)offset * (c->upper - c->lower) / travel);
            if (w->style & 4)
            {
                c->value = (LONG)c->view;
                controlRepaint(w);
            }
            notify(w, 5, (SHORT)c->view);
        }
        else if (message == WM_BUTTON1UP)
        {
            if (c->pressed)
            {
                c->pressed = FALSE;
                WinSetCapture(HWND_DESKTOP, 0);
                notify(w, 6, (SHORT)c->view);
            }
            notify(w, 7, (SHORT)c->value);
        }
        return 0;
    }
    return WinDefWindowProc(w->hwnd, message, first, second);
}
