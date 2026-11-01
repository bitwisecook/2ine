/* Public domain. No rights reserved. Wholly AI-authored. */
/* RECT arithmetic uses inclusive left/top and exclusive right/bottom edges. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
typedef struct Rectangle32
{
    LONG left, top, right, bottom;
} Rectangle32;
static Rectangle32 load_rectangle(PVOID input)
{
    const uint8 *p = input;
    return (Rectangle32){(LONG)lx_guest_load_u32(p), (LONG)lx_guest_load_u32(p + 4),
                         (LONG)lx_guest_load_u32(p + 8), (LONG)lx_guest_load_u32(p + 12)};
}
static BOOL store_rectangle(PVOID output, Rectangle32 rect)
{
    if (!output)
    {
        open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)rect.left);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)rect.top);
    lx_guest_store_u32((uint8 *)output + 8, (ULONG)rect.right);
    lx_guest_store_u32((uint8 *)output + 12, (ULONG)rect.bottom);
    return TRUE;
}
static BOOL empty(Rectangle32 rect)
{
    return rect.left >= rect.right || rect.top >= rect.bottom;
}
static Rectangle32 intersection(Rectangle32 a, Rectangle32 b)
{
    Rectangle32 result = {a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
                          a.right < b.right ? a.right : b.right,
                          a.bottom < b.bottom ? a.bottom : b.bottom};
    return empty(result) ? (Rectangle32){0} : result;
}
BOOL SetRect(PVOID output, LONG left, LONG top, LONG right, LONG bottom)
{
    return store_rectangle(output, (Rectangle32){left, top, right, bottom});
}
BOOL SetRectEmpty(PVOID output)
{
    return store_rectangle(output, (Rectangle32){0});
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
    Rectangle32 a = load_rectangle(first), b = load_rectangle(second);
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
BOOL UnionRect(PVOID output, PVOID first, PVOID second)
{
    if (!output || !first || !second)
    {
        open32SetError(87);
        return FALSE;
    }
    Rectangle32 a = load_rectangle(first), b = load_rectangle(second), result;
    if (empty(a))
        result = empty(b) ? (Rectangle32){0} : b;
    else if (empty(b))
        result = a;
    else
        result = (Rectangle32){a.left < b.left ? a.left : b.left, a.top < b.top ? a.top : b.top,
                               a.right > b.right ? a.right : b.right,
                               a.bottom > b.bottom ? a.bottom : b.bottom};
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
    Rectangle32 result = intersection(load_rectangle(first), load_rectangle(second));
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
    Rectangle32 a = load_rectangle(first), b = intersection(a, load_rectangle(second)), result = a;
    if (empty(a))
        result = (Rectangle32){0};
    else if (!empty(b))
    {
        if (b.left == a.left && b.right == a.right)
        {
            if (b.top == a.top)
                result.top = b.bottom;
            else if (b.bottom == a.bottom)
                result.bottom = b.top;
        }
        else if (b.top == a.top && b.bottom == a.bottom)
        {
            if (b.left == a.left)
                result.left = b.right;
            else if (b.right == a.right)
                result.right = b.left;
        }
        if (empty(result))
            result = (Rectangle32){0};
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
    Rectangle32 rect = load_rectangle(input);
    rect.left = (LONG)((ULONG)rect.left + (ULONG)x);
    rect.right = (LONG)((ULONG)rect.right + (ULONG)x);
    rect.top = (LONG)((ULONG)rect.top + (ULONG)y);
    rect.bottom = (LONG)((ULONG)rect.bottom + (ULONG)y);
    return store_rectangle(input, rect);
}
BOOL InflateRect(PVOID input, LONG x, LONG y)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    Rectangle32 rect = load_rectangle(input);
    rect.left = (LONG)((ULONG)rect.left - (ULONG)x);
    rect.right = (LONG)((ULONG)rect.right + (ULONG)x);
    rect.top = (LONG)((ULONG)rect.top - (ULONG)y);
    rect.bottom = (LONG)((ULONG)rect.bottom + (ULONG)y);
    return store_rectangle(input, rect);
}
BOOL PtInRect(PVOID input, LONG x, LONG y)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    Rectangle32 rect = load_rectangle(input);
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}
