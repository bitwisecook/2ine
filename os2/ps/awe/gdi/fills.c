/* Public domain. No rights reserved. Wholly AI-authored. */
/* Rectangle fills with a brush: FillRect, FrameRect, InvertRect and PatBlt. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/brushes.h"
#include "os2/include/private/pmgrep.h"

enum
{
    /* FillRect accepts COLOR_* + 1 in place of a brush handle. */
    SYSCOLOR_BRUSH_LAST = WIN32_COLOR_BTNHIGHLIGHT + 1,
    QUADRILATERAL_CORNERS = 4,
    LONG_BYTES = 4,
    /* Hatch brushes repeat every 8 pels. */
    HATCH_PHASE_MASK = 7,
    /* A ternary raster operation's index is bits 16-23 of the ROP code. */
    ROP_INDEX_SHIFT = 16,
    ROP_INDEX_MASK = 255,
    /* Largest area one PatBlt renders through the pel callback. */
    BLIT_PELS_MAX = 16 * 1024 * 1024
};

static BOOL brush_blt(ULONG handle, LONG x, LONG y, LONG width, LONG height, ULONG operation,
                      Open32Brush *brush);

/* A rectangle under a rotating transform is a filled quadrilateral without an outline. */
BOOL open32RotatedFill(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, ULONG brush)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
        return FALSE;
    uint8 points[QUADRILATERAL_CORNERS * WIN32_POINT_SIZE];
    const LONG corners[QUADRILATERAL_CORNERS * 2] = {left,  top,    right, top,
                                                     right, bottom, left,  bottom};
    for (unsigned i = 0; i < QUADRILATERAL_CORNERS * 2; i++)
        lx_guest_store_u32(points + i * LONG_BYTES, (ULONG)corners[i]);
    ULONG old_brush = open32SelectBrush(dc, brush),
          old_pen = open32SelectPen(dc, GetStockObject(NULL_PEN));
    BOOL ok = old_brush && old_pen && Polygon(handle, points, QUADRILATERAL_CORNERS);
    if (old_pen)
        open32SelectPen(dc, old_pen);
    if (old_brush)
        open32SelectBrush(dc, old_brush);
    return ok;
}

LONG FillRect(ULONG handle, PVOID rectangle, ULONG brush_handle)
{
    if (!open32CanDraw(handle) || !rectangle)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (open32Rotated(open32FindDC(handle)) && brush_handle > SYSCOLOR_BRUSH_LAST)
    {
        const uint8 *corner = rectangle;
        return open32RotatedFill(handle, (LONG)lx_guest_load_u32(corner + WIN32_RECT_LEFT),
                                 (LONG)lx_guest_load_u32(corner + WIN32_RECT_TOP),
                                 (LONG)lx_guest_load_u32(corner + WIN32_RECT_RIGHT),
                                 (LONG)lx_guest_load_u32(corner + WIN32_RECT_BOTTOM), brush_handle);
    }
    LxHandleBorrow borrow = {0};
    ULONG color, style = 0;
    if (brush_handle > 0 && brush_handle <= SYSCOLOR_BRUSH_LAST)
        color = GetSysColor((LONG)brush_handle - 1);
    else if (lx_handle_borrow(brush_handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
    {
        Open32Brush *brush = borrow.resource;
        color = brush->color;
        style = brush->style;
    }
    else
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    const uint8 *wire = rectangle;
    LONG left = (LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
         top = (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP);
    LONG right = (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
         bottom = (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM);
    if (open32IsMetafile(handle))
    {
        if ((int64_t)right - left > INT32_MAX || (int64_t)right - left < INT32_MIN ||
            (int64_t)bottom - top > INT32_MAX || (int64_t)bottom - top < INT32_MIN)
        {
            lx_handle_release(&borrow);
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        ULONG recorded_brush = brush_handle, temporary = 0;
        if (brush_handle > 0 && brush_handle <= SYSCOLOR_BRUSH_LAST)
        {
            /* A metafile records a real brush, not a system-colour index. */
            temporary = CreateSolidBrush(color);
            recorded_brush = temporary;
        }
        open32MetafileObject(handle, recorded_brush);
        open32MetafilePatBlt(handle, left, top, right - left, bottom - top, PATCOPY);
        Open32DC *recorded_dc = open32FindDC(handle);
        open32MetafileObject(handle, recorded_dc->brush_handle);
        if (temporary)
            DeleteObject(temporary);
    }
    Open32DC *mapping = open32FindDC(handle);
    if (!open32MapPoint(mapping, &left, &top, TRUE) ||
        !open32MapPoint(mapping, &right, &bottom, TRUE))
    {
        lx_handle_release(&borrow);
        return 0;
    }
    if (left > right)
    {
        LONG v = left;
        left = right;
        right = v;
    }
    if (top > bottom)
    {
        LONG v = top;
        top = bottom;
        bottom = v;
    }
    int64_t lower = (int64_t)open32DCHeight(handle) - bottom,
            upper = (int64_t)open32DCHeight(handle) - top;
    BOOL result = FALSE;
    if (lower >= INT32_MIN && upper <= INT32_MAX && lower <= upper && left <= right)
    {
        RECTL rect = {left, (LONG)lower, right, (LONG)upper};
        /* COLORREF is 0x00BBGGRR; PM takes 0x00RRGGBB. */
        LONG rgb = (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
        if (style == BS_HATCHED || style == BS_PATTERN)
        {
            if ((int64_t)right - left <= INT32_MAX && (int64_t)bottom - top <= INT32_MAX)
            {
                LONG lx = left, ly = top, rx = right, ry = bottom;
                if (open32MapPoint(mapping, &lx, &ly, FALSE) &&
                    open32MapPoint(mapping, &rx, &ry, FALSE))
                    result = brush_blt(handle, lx, ly, rx - lx, ry - ly, PATCOPY, borrow.resource);
            }
        }
        else
            result = style == BS_NULL || WinFillRect(handle, &rect, rgb);
    }
    lx_handle_release(&borrow);
    if (!result)
        open32SetError(ERROR_INVALID_PARAMETER);
    return result;
}

LONG FrameRect(ULONG handle, PVOID input, ULONG brush)
{
    if (!input || !open32CanDraw(handle))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const uint8 *wire = input;
    LONG left = (LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
         top = (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP);
    LONG right = (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
         bottom = (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM);
    if (left >= right || top >= bottom)
        return 0;
    const LONG edges[4][4] = {{left, top, right, top + 1},
                              {left, bottom - 1, right, bottom},
                              {left, top, left + 1, bottom},
                              {right - 1, top, right, bottom}};
    for (unsigned edge = 0; edge < 4; edge++)
    {
        uint8 rectangle[WIN32_RECT_SIZE];
        for (unsigned i = 0; i < 4; i++)
            lx_guest_store_u32(rectangle + i * LONG_BYTES, (ULONG)edges[edge][i]);
        if (!FillRect(handle, rectangle, brush))
            return 0;
    }
    return 1;
}

BOOL InvertRect(ULONG handle, PVOID input)
{
    if (!input || !open32CanDraw(handle))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG height = open32DCHeight(handle);
    LONG left = (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_LEFT),
         right = (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_RIGHT);
    int64_t top = (int64_t)height - (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_TOP);
    int64_t bottom = (int64_t)height - (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_BOTTOM);
    if (top < INT32_MIN || top > INT32_MAX || bottom < INT32_MIN || bottom > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    RECTL rectangle = {left, (LONG)bottom, right, (LONG)top};
    open32MetafilePatBlt(handle, left, (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_TOP),
                         right - left,
                         (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_BOTTOM) -
                             (LONG)lx_guest_load_u32((uint8 *)input + WIN32_RECT_TOP),
                         DSTINVERT);
    return WinInvertRect(handle, &rectangle);
}

typedef struct BrushBlit
{
    Open32DC *dc;
    Open32Brush *brush;
    unsigned rop;
    LONG canvas_height;
} BrushBlit;

static uint32 brush_rgb(ULONG color)
{
    return (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
}

/* Pattern-only raster operation at one pel; Win32 brush origins and hatches are top-down. */
static int brush_blit_pel(void *context, LONG x, LONG device_y, uint32_t *pel)
{
    BrushBlit *blit = context;
    Open32DC *dc = blit->dc;
    Open32Brush *brush = blit->brush;
    LONG y = blit->canvas_height - 1 - device_y;
    BOOL pattern = lx_pm_rop_uses_pattern(blit->rop);
    uint32 p = brush_rgb(brush ? brush->color : 0);
    if (pattern && brush->style == BS_HATCHED)
    {
        LONG px = (x - dc->brush_origin_x) & HATCH_PHASE_MASK,
             py = (y - dc->brush_origin_y) & HATCH_PHASE_MASK;
        BOOL ink = brush->bitmap == HS_HORIZONTAL  ? py == 0
                   : brush->bitmap == HS_VERTICAL  ? px == 0
                   : brush->bitmap == HS_FDIAGONAL ? ((px + py) & HATCH_PHASE_MASK) == 0
                   : brush->bitmap == HS_BDIAGONAL ? ((px - py) & HATCH_PHASE_MASK) == 0
                   : brush->bitmap == HS_CROSS     ? (px == 0 || py == 0)
                                                   : (((px + py) & HATCH_PHASE_MASK) == 0 ||
                                                  ((px - py) & HATCH_PHASE_MASK) == 0);
        if (!ink && dc->background_mode == TRANSPARENT)
            return LX_PM_PEL_KEEP;
        p = brush_rgb(ink ? brush->color : dc->background_color);
    }
    if (pattern && brush->style == BS_PATTERN)
    {
        LONG px = (LONG)(((int64_t)x - dc->brush_origin_x) % brush->width);
        LONG py = (LONG)(((int64_t)y - dc->brush_origin_y) % brush->height);
        if (px < 0)
            px += (LONG)brush->width;
        if (py < 0)
            py += (LONG)brush->height;
        p = brush->pixels[(size_t)(brush->height - 1 - py) * brush->width + px];
        if (brush->depth == 1)
            p = brush_rgb(p ? dc->background_color : dc->text_color);
    }
    /* The operation ignores the source (checked by brush_blt), so any source value will do. */
    *pel = lx_pm_rop3(p, 0, *pel, blit->rop);
    return LX_PM_PEL_SET;
}

static BOOL brush_blt(ULONG handle, LONG x, LONG y, LONG width, LONG height, ULONG operation,
                      Open32Brush *brush)
{
    TRACE_NATIVE("PatBlt(%u, %d, %d, %d, %d, %X)", handle, x, y, width, height, operation);
    Open32DC *dc = open32FindDC(handle);
    unsigned rop = (operation >> ROP_INDEX_SHIFT) & ROP_INDEX_MASK;
    if (!dc || !open32CanDraw(handle))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (lx_pm_rop_uses_source(rop))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    int64_t ex = (int64_t)x + width, ey = (int64_t)y + height;
    if (ex < INT32_MIN || ex > INT32_MAX || ey < INT32_MIN || ey > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG mapped_x = (LONG)ex, mapped_y = (LONG)ey;
    if (!open32MapPoint(dc, &x, &y, TRUE) || !open32MapPoint(dc, &mapped_x, &mapped_y, TRUE))
        return FALSE;
    ex = (int64_t)mapped_x - x;
    ey = (int64_t)mapped_y - y;
    if (ex < INT32_MIN || ex > INT32_MAX || ey < INT32_MIN || ey > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    width = (LONG)ex;
    height = (LONG)ey;
    BOOL pattern = lx_pm_rop_uses_pattern(rop);
    if (pattern && !brush)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!width || !height || (pattern && brush->style == BS_NULL))
        return TRUE;
    int64_t end_x = (int64_t)x + width, end_y = (int64_t)y + height;
    int64_t left = x < end_x ? x : end_x, right = x < end_x ? end_x : x;
    int64_t top = y < end_y ? y : end_y, bottom = y < end_y ? end_y : y;
    LONG canvas_width = open32DCWidth(handle), canvas_height = open32DCHeight(handle);
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > canvas_width)
        right = canvas_width;
    if (bottom > canvas_height)
        bottom = canvas_height;
    if (left >= right || top >= bottom)
        return TRUE;
    if ((uint64)(right - left) * (uint64)(bottom - top) > BLIT_PELS_MAX)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BrushBlit blit = {dc, brush, rop, canvas_height};
    RECTL device = {(LONG)left, canvas_height - (LONG)bottom, (LONG)right,
                    canvas_height - (LONG)top};
    return lx_pm_blit_rect(handle, &device, LX_PM_BLIT_READ, brush_blit_pel, &blit);
}

BOOL PatBlt(ULONG handle, LONG x, LONG y, LONG width, LONG height, ULONG operation)
{
    Open32DC *dc = open32FindDC(handle);
    open32MetafilePatBlt(handle, x, y, width, height, operation);
    if (dc && open32Rotated(dc) && operation == PATCOPY)
        return open32RotatedFill(handle, x, y, x + width, y + height, dc->brush_handle);
    return brush_blt(handle, x, y, width, height, operation, dc ? dc->brush.resource : NULL);
}
