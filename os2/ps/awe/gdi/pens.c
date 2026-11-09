/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pen objects: LOGPEN and EXTLOGPEN metadata, stock pens and selection. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/pens.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

static void free_pen(void *resource)
{
    Open32Pen *pen = resource;
    lx_handle_release(&pen->brush);
    if (pen->brush_handle)
        open32DeleteBrush(pen->brush_handle);
    free(pen);
}

ULONG CreatePen(LONG style, LONG width, ULONG color)
{
    TRACE_NATIVE("CreatePen(%d, %d, %08X)", (int)style, (int)width, (uint)color);
    if (style < 0 || style > 6 || width < 0 || width > 32767 ||
        ((color & 0xFF000000u) && (color & 0xFF000000u) != 0x02000000u))
    {
        open32SetError(87);
        return 0;
    }
    Open32Pen *pen = calloc(1, sizeof *pen);
    if (!pen)
    {
        open32SetError(8);
        return 0;
    }
    pen->style = width > 1 && style >= 1 && style <= 4 ? 0 : style;
    pen->width = width;
    pen->color = color;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_PEN, pen, free_pen);
    if (!handle)
    {
        free(pen);
        open32SetError(8);
    }
    return handle;
}

ULONG CreatePenIndirect(PVOID record)
{
    if (!record)
    {
        open32SetError(87);
        return 0;
    }
    return CreatePen((LONG)lx_guest_load_u32(record), (LONG)lx_guest_load_u32((uint8 *)record + 4),
                     lx_guest_load_u32((uint8 *)record + 12));
}

ULONG open32StockPen(LONG index)
{
    static _Thread_local ULONG stocks[3];
    if (index < 6 || index > 8)
        return 0;
    unsigned slot = (unsigned)index - 6;
    if (!stocks[slot])
    {
        ULONG handle = CreatePen(index == 8 ? 5 : 0, 0, index == 6 ? 0xFFFFFF : 0);
        LxHandleBorrow borrow = {0};
        if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &borrow))
            return 0;
        ((Open32Pen *)borrow.resource)->stock = TRUE;
        lx_handle_release(&borrow);
        stocks[slot] = handle;
    }
    return stocks[slot];
}

ULONG open32SelectPen(Open32DC *dc, ULONG handle)
{
    LxHandleBorrow next = {0};
    if (!dc || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &next))
        return 0;
    ULONG previous = dc->pen_handle;
    lx_handle_release(&dc->pen);
    dc->pen = next;
    dc->pen_handle = handle;
    return previous;
}

BOOL open32DeletePen(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &borrow))
        return FALSE;
    BOOL result = !((Open32Pen *)borrow.resource)->stock &&
                  lx_handle_close_guarded(handle, LX_HANDLE_OPEN32_PEN, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}

LONG open32PenObject(ULONG handle, LONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &borrow))
        return 0;
    Open32Pen *pen = borrow.resource;
    LONG result = !output || capacity >= 16 ? 16 : 0;
    if (output && result)
    {
        lx_guest_store_u32(output, (ULONG)pen->style);
        lx_guest_store_u32((uint8 *)output + 4, (ULONG)pen->width);
        lx_guest_store_u32((uint8 *)output + 8, 0);
        lx_guest_store_u32((uint8 *)output + 12, pen->color);
    }
    lx_handle_release(&borrow);
    return result;
}

ULONG ExtCreatePen(ULONG style, ULONG width, PVOID brush, ULONG count, PVOID entries)
{
    (void)count;
    (void)entries;
    ULONG basic = style & 15;
    if (!brush)
    {
        open32SetError(87);
        return 0;
    }
    /* PMWINX refuses PS_USERSTYLE and requires cosmetic pens to be one-pel solid. */
    if (basic == 7)
    {
        open32SetError(50);
        return 0;
    }
    ULONG brush_style = lx_guest_load_u32(brush);
    if (basic > 8 || (style & 0x10000 ? brush_style > 8 : width != 1 || brush_style))
    {
        open32SetError(87);
        return 0;
    }
    ULONG object = CreatePen((LONG)(basic > 6 ? 0 : basic), (LONG)width,
                             lx_guest_load_u32((uint8 *)brush + 4));
    if (!object)
        return 0;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(object, LX_HANDLE_OPEN32_PEN, &borrow))
    {
        DeleteObject(object);
        return 0;
    }
    Open32Pen *pen = borrow.resource;
    BOOL valid = TRUE;
    pen->style = (LONG)basic;
    if (style & 0x10000)
    {
        pen->cap = (style >> 8) & 15;
        pen->join = (style >> 12) & 15;
    }
    if (brush_style == 1)
        pen->style = 5;
    if (brush_style >= 2)
    {
        pen->brush_handle = CreateBrushIndirect(brush);
        valid = pen->brush_handle &&
                lx_handle_borrow(pen->brush_handle, LX_HANDLE_OPEN32_BRUSH, &pen->brush);
    }
    lx_handle_release(&borrow);
    if (!valid)
    {
        DeleteObject(object);
        open32SetError(87);
        return 0;
    }
    return object;
}
