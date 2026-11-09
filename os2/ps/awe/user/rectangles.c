/* Public domain. No rights reserved. Wholly AI-authored. */
/* RECT arithmetic: inclusive left/top and exclusive right/bottom edges, computed by PM's rectangle
   calls. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"

/* A Win32 RECT (left, top, right, bottom) and a PM RECTL (xLeft, yBottom, xRight, yTop) both hold
   the low x, low y, high x and high y edges in that order, and both are half-open. PM's rectangle
   arithmetic therefore serves Win32 rectangles unchanged once they are loaded from guest memory. */
static RECTL load_rectangle(PVOID input)
{
    const uint8 *p = input;
    return (RECTL){(LONG)lx_guest_load_u32(p), (LONG)lx_guest_load_u32(p + 4),
                   (LONG)lx_guest_load_u32(p + 8), (LONG)lx_guest_load_u32(p + 12)};
}

static BOOL store_rectangle(PVOID output, RECTL rect)
{
    if (!output)
    {
        open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)rect.xLeft);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)rect.yBottom);
    lx_guest_store_u32((uint8 *)output + 8, (ULONG)rect.xRight);
    lx_guest_store_u32((uint8 *)output + 12, (ULONG)rect.yTop);
    return TRUE;
}

static BOOL empty(RECTL rect)
{
    return WinIsRectEmpty(0, &rect);
}

static RECTL intersection(RECTL a, RECTL b)
{
    RECTL result;
    WinIntersectRect(0, &result, &a, &b);
    return result;
}

BOOL SetRect(PVOID output, LONG left, LONG top, LONG right, LONG bottom)
{
    return store_rectangle(output, (RECTL){left, top, right, bottom});
}

BOOL SetRectEmpty(PVOID output)
{
    return store_rectangle(output, (RECTL){0});
}

BOOL CopyRect(PVOID output, PVOID input)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    return store_rectangle(output, load_rectangle(input));
}

BOOL IsRectEmpty(PVOID input)
{
    if (!input)
    {
        open32SetError(87);
        return TRUE;
    }
    return empty(load_rectangle(input));
}

BOOL EqualRect(PVOID first, PVOID second)
{
    if (!first || !second)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL a = load_rectangle(first), b = load_rectangle(second);
    return WinEqualRect(0, &a, &b);
}

BOOL UnionRect(PVOID output, PVOID first, PVOID second)
{
    if (!output || !first || !second)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL a = load_rectangle(first), b = load_rectangle(second), result = {0};
    /* Unlike WinUnionRect, which copies the second of two empty rectangles, Windows clears the
       result. */
    if (!empty(a) || !empty(b))
        WinUnionRect(0, &result, &a, &b);
    store_rectangle(output, result);
    return !empty(result);
}

BOOL IntersectRect(PVOID output, PVOID first, PVOID second)
{
    if (!output || !first || !second)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL result = intersection(load_rectangle(first), load_rectangle(second));
    store_rectangle(output, result);
    return !empty(result);
}

BOOL SubtractRect(PVOID output, PVOID first, PVOID second)
{
    if (!output || !first || !second)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL a = load_rectangle(first), b = intersection(a, load_rectangle(second)), result = a;
    if (empty(a))
        result = (RECTL){0};
    else if (!empty(b))
    {
        if (b.xLeft == a.xLeft && b.xRight == a.xRight)
        {
            if (b.yBottom == a.yBottom)
                result.yBottom = b.yTop;
            else if (b.yTop == a.yTop)
                result.yTop = b.yBottom;
        }
        else if (b.yBottom == a.yBottom && b.yTop == a.yTop)
        {
            if (b.xLeft == a.xLeft)
                result.xLeft = b.xRight;
            else if (b.xRight == a.xRight)
                result.xRight = b.xLeft;
        }
        if (empty(result))
            result = (RECTL){0};
    }
    store_rectangle(output, result);
    return !empty(result);
}

BOOL OffsetRect(PVOID input, LONG x, LONG y)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL rect = load_rectangle(input);
    WinOffsetRect(0, &rect, x, y);
    return store_rectangle(input, rect);
}

BOOL InflateRect(PVOID input, LONG x, LONG y)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL rect = load_rectangle(input);
    WinInflateRect(0, &rect, x, y);
    return store_rectangle(input, rect);
}

BOOL PtInRect(PVOID input, LONG x, LONG y)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL rect = load_rectangle(input);
    POINTL point = {x, y};
    return WinPtInRect(0, &rect, &point);
}
