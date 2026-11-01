/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM rectangles are half-open and window coordinates have an upward y axis. */
#include "os2libs/pmwin/internal.h"
BOOL WinIsRectEmpty(HAB hab, PRECTL r)
{
    (void)hab;
    return !r || r->xLeft >= r->xRight || r->yBottom >= r->yTop;
}
BOOL WinSetRect(HAB hab, PRECTL r, LONG left, LONG bottom, LONG right, LONG top)
{
    (void)hab;
    if (!r)
        return FALSE;
    *r = (RECTL){left, bottom, right, top};
    return TRUE;
}
BOOL WinSetRectEmpty(HAB hab, PRECTL r)
{
    return WinSetRect(hab, r, 0, 0, 0, 0);
}
BOOL WinCopyRect(HAB hab, PRECTL d, PRECTL s)
{
    (void)hab;
    if (!d || !s)
        return FALSE;
    *d = *s;
    return TRUE;
}
BOOL WinEqualRect(HAB hab, PRECTL a, PRECTL b)
{
    (void)hab;
    return a && b && !memcmp(a, b, sizeof *a);
}
BOOL WinIntersectRect(HAB hab, PRECTL d, PRECTL a, PRECTL b)
{
    if (!d || !a || !b)
        return FALSE;
    RECTL r = {a->xLeft > b->xLeft ? a->xLeft : b->xLeft,
               a->yBottom > b->yBottom ? a->yBottom : b->yBottom,
               a->xRight < b->xRight ? a->xRight : b->xRight,
               a->yTop < b->yTop ? a->yTop : b->yTop};
    if (WinIsRectEmpty(hab, &r))
    {
        WinSetRectEmpty(hab, d);
        return FALSE;
    }
    *d = r;
    return TRUE;
}
BOOL WinUnionRect(HAB hab, PRECTL d, PRECTL a, PRECTL b)
{
    if (!d || !a || !b)
        return FALSE;
    if (WinIsRectEmpty(hab, a))
    {
        *d = *b;
        return !WinIsRectEmpty(hab, d);
    }
    if (WinIsRectEmpty(hab, b))
    {
        *d = *a;
        return TRUE;
    }
    *d = (RECTL){a->xLeft < b->xLeft ? a->xLeft : b->xLeft,
                 a->yBottom < b->yBottom ? a->yBottom : b->yBottom,
                 a->xRight > b->xRight ? a->xRight : b->xRight,
                 a->yTop > b->yTop ? a->yTop : b->yTop};
    return TRUE;
}
BOOL WinOffsetRect(HAB hab, PRECTL r, LONG dx, LONG dy)
{
    (void)hab;
    if (!r)
        return FALSE;
    r->xLeft = (LONG)((ULONG)r->xLeft + dx);
    r->xRight = (LONG)((ULONG)r->xRight + dx);
    r->yBottom = (LONG)((ULONG)r->yBottom + dy);
    r->yTop = (LONG)((ULONG)r->yTop + dy);
    return TRUE;
}
BOOL WinInflateRect(HAB hab, PRECTL r, LONG dx, LONG dy)
{
    (void)hab;
    if (!r)
        return FALSE;
    r->xLeft = (LONG)((ULONG)r->xLeft - dx);
    r->xRight = (LONG)((ULONG)r->xRight + dx);
    r->yBottom = (LONG)((ULONG)r->yBottom - dy);
    r->yTop = (LONG)((ULONG)r->yTop + dy);
    return TRUE;
}
BOOL WinPtInRect(HAB hab, PRECTL r, PPOINTL p)
{
    (void)hab;
    return r && p && p->x >= r->xLeft && p->x < r->xRight && p->y >= r->yBottom && p->y < r->yTop;
}
BOOL WinMapWindowPoints(HWND from, HWND to, PPOINTL points, LONG count)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || count < 0 || (count && !points))
        return FALSE;
    Window *a = getWindowFromHWND(anchor, from ? from : HWND_DESKTOP);
    Window *b = getWindowFromHWND(anchor, to ? to : HWND_DESKTOP);
    if (!a || !b)
        return FALSE;
    ULONG x = 0, y = 0;
    for (; a && a != &desktop_window; a = a->parent)
    {
        x += a->x;
        y += a->y;
    }
    for (; b && b != &desktop_window; b = b->parent)
    {
        x -= b->x;
        y -= b->y;
    }
    for (LONG i = 0; i < count; i++)
    {
        points[i].x = (LONG)((ULONG)points[i].x + x);
        points[i].y = (LONG)((ULONG)points[i].y + y);
    }
    return TRUE;
}
