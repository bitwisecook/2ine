/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pen objects: LOGPEN and EXTLOGPEN metadata, stock pens and selection. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/pens.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

enum
{
    PEN_WIDTH_MAX = 32767,
    RGB_WHITE = 0xFFFFFF,
    /* PS_ENDCAP_* and PS_JOIN_* as the 0-2 selectors Open32Pen keeps. */
    PEN_ENDCAP_SHIFT = 8,
    PEN_JOIN_SHIFT = 12,
    PEN_SELECTOR_MASK = 15
};

/* A COLORREF's high byte is 0 (RGB) or 2 (PALETTERGB); other forms are refused. */
static const ULONG COLORREF_TYPE_MASK = 0xFF000000u, COLORREF_PALETTE_RGB = 0x02000000u;

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
    if (style < 0 || style > PS_INSIDEFRAME || width < 0 || width > PEN_WIDTH_MAX ||
        ((color & COLORREF_TYPE_MASK) && (color & COLORREF_TYPE_MASK) != COLORREF_PALETTE_RGB))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Open32Pen *pen = calloc(1, sizeof *pen);
    if (!pen)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    /* Wide pens cannot be styled; Windows draws them solid. */
    pen->style = width > 1 && style >= PS_DASH && style <= PS_DASHDOTDOT ? PS_SOLID : style;
    pen->width = width;
    pen->color = color;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_PEN, pen, free_pen);
    if (!handle)
    {
        free(pen);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return handle;
}

ULONG CreatePenIndirect(PVOID record)
{
    if (!record)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return CreatePen((LONG)lx_guest_load_u32((uint8 *)record + WIN32_LOGPEN_STYLE),
                     (LONG)lx_guest_load_u32((uint8 *)record + WIN32_LOGPEN_WIDTH),
                     lx_guest_load_u32((uint8 *)record + WIN32_LOGPEN_COLOR));
}

ULONG open32StockPen(LONG index)
{
    static _Thread_local ULONG stocks[NULL_PEN - WHITE_PEN + 1];
    if (index < WHITE_PEN || index > NULL_PEN)
        return 0;
    unsigned slot = (unsigned)index - WHITE_PEN;
    if (!stocks[slot])
    {
        ULONG handle = CreatePen(index == NULL_PEN ? PS_NULL : PS_SOLID, 0,
                                 index == WHITE_PEN ? RGB_WHITE : 0);
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
    LONG result = !output || capacity >= WIN32_LOGPEN_SIZE ? WIN32_LOGPEN_SIZE : 0;
    if (output && result)
    {
        lx_guest_store_u32((uint8 *)output + WIN32_LOGPEN_STYLE, (ULONG)pen->style);
        lx_guest_store_u32((uint8 *)output + WIN32_LOGPEN_WIDTH, (ULONG)pen->width);
        lx_guest_store_u32((uint8 *)output + WIN32_LOGPEN_WIDTH_Y, 0);
        lx_guest_store_u32((uint8 *)output + WIN32_LOGPEN_COLOR, pen->color);
    }
    lx_handle_release(&borrow);
    return result;
}

ULONG ExtCreatePen(ULONG style, ULONG width, PVOID brush, ULONG count, PVOID entries)
{
    (void)count;
    (void)entries;
    ULONG basic = style & PS_STYLE_MASK;
    if (!brush)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* PMWINX refuses PS_USERSTYLE and requires cosmetic pens to be one-pel solid. */
    if (basic == PS_USERSTYLE)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    ULONG brush_style = lx_guest_load_u32((uint8 *)brush + WIN32_LOGBRUSH_STYLE);
    if (basic > PS_ALTERNATE ||
        (style & PS_GEOMETRIC ? brush_style > BS_DIBPATTERN8X8 : width != 1 || brush_style))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG object = CreatePen((LONG)(basic > PS_INSIDEFRAME ? PS_SOLID : basic), (LONG)width,
                             lx_guest_load_u32((uint8 *)brush + WIN32_LOGBRUSH_COLOR));
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
    if (style & PS_GEOMETRIC)
    {
        pen->cap = (style >> PEN_ENDCAP_SHIFT) & PEN_SELECTOR_MASK;
        pen->join = (style >> PEN_JOIN_SHIFT) & PEN_SELECTOR_MASK;
    }
    if (brush_style == BS_NULL)
        pen->style = PS_NULL;
    if (brush_style >= BS_HATCHED)
    {
        pen->brush_handle = CreateBrushIndirect(brush);
        valid = pen->brush_handle &&
                lx_handle_borrow(pen->brush_handle, LX_HANDLE_OPEN32_BRUSH, &pen->brush);
    }
    lx_handle_release(&borrow);
    if (!valid)
    {
        DeleteObject(object);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return object;
}
