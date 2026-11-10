/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows DC coordinates have their origin at the top left of the PM drawable. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/include/private/pmdevp.h"

enum
{
    /* BitBlt's widest and tallest copy, in device pixels. */
    BLT_EXTENT_MAX = 16384,
    /* A ternary raster operation keeps its operation index in bits 16-23. */
    RASTER_OPERATION_INDEX_SHIFT = 16,
    RASTER_OPERATION_INDEX_MASK = 0xff,
    /* A compatible bitmap made while no bitmap is selected matches the display. */
    DISPLAY_BITMAP_DEPTH = 24
};

/* The high byte of a COLORREF is 0 for an RGB value or 2 for PALETTERGB. */
enum
{
    COLORREF_FLAGS_MASK = 0xFF000000u,
    COLORREF_PALETTE_RGB = 0x02000000u
};

static _Thread_local Open32DC *contexts;

Open32DC *open32FindDC(ULONG handle)
{
    for (Open32DC *dc = contexts; dc; dc = dc->next)
        if (dc->ps == handle && lx_pm_valid_ps(handle))
        {
            BOOL locked = dc->window && open32WindowUpdateLocked(dc->window);
            if (locked)
            {
                if (!dc->lock_region)
                    dc->lock_region = GpiCreateRegion(handle, 0, NULL);
                if (dc->lock_region)
                    GpiSetClipRegion(handle, dc->lock_region, NULL);
            }
            else if (dc->lock_region)
            {
                GpiSetClipRegion(handle, dc->clip_region, NULL);
                GpiDestroyRegion(handle, dc->lock_region);
                dc->lock_region = 0;
            }
            return dc;
        }
    return NULL;
}

LONG open32DCHeight(ULONG handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return 0;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    return bitmap ? (LONG)bitmap->height : ps->window ? ps->window->h : 0;
}

LONG open32DCWidth(ULONG handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return 0;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    return bitmap ? (LONG)bitmap->width : ps->window ? ps->window->w : 0;
}

static Open32DC *register_context(HPS ps, HWND window, ULONG kind)
{
    Open32DC *dc = calloc(1, sizeof *dc);
    if (!dc)
        return NULL;
    dc->ps = ps;
    dc->window = window;
    dc->kind = kind;
    dc->map_mode = WIN32_MM_TEXT;
    dc->poly_fill_mode = ALTERNATE;
    dc->stretch_mode = BLACKONWHITE;
    dc->window_width = dc->window_height = dc->viewport_width = dc->viewport_height = 1;
    dc->background_mode = OPAQUE;
    dc->rop2 = R2_COPYPEN;
    dc->background_color = RGB_WHITE;
    /* Win32 colours are RGB values, so the PS works in RGB mode. */
    GpiCreateLogColorTable(ps, 0, LCOLF_RGB, 0, 0, NULL);
    GpiSetColor(ps, 0);
    GpiSetBackColor(ps, RGB_WHITE);
    GpiSetBackMix(ps, BM_OVERPAINT);
    open32SelectFont(dc, GetStockObject(SYSTEM_FONT));
    if (!dc->font.resource)
    {
        free(dc);
        return NULL;
    }
    open32SelectBrush(dc, GetStockObject(WHITE_BRUSH));
    if (!dc->brush.resource)
    {
        lx_handle_release(&dc->font);
        free(dc);
        return NULL;
    }
    open32SelectPen(dc, GetStockObject(BLACK_PEN));
    if (!dc->pen.resource)
    {
        lx_handle_release(&dc->brush);
        lx_handle_release(&dc->font);
        free(dc);
        return NULL;
    }
    dc->next = contexts;
    contexts = dc;
    SelectPalette(ps, open32StockPalette(), FALSE);
    if (!dc->palette.resource)
    {
        contexts = dc->next;
        lx_handle_release(&dc->pen);
        lx_handle_release(&dc->brush);
        lx_handle_release(&dc->font);
        free(dc);
        return NULL;
    }
    return dc;
}

static BOOL discard_context(Open32DC *dc)
{
    open32MetafileDCCleanup(dc->ps);
    open32PathCleanup(dc);
    open32SavedDCCleanup(dc);
    if (dc->lock_region)
    {
        GpiSetClipRegion(dc->ps, dc->clip_region, NULL);
        GpiDestroyRegion(dc->ps, dc->lock_region);
        dc->lock_region = 0;
    }
    Open32DC **link = &contexts;
    while (*link != dc)
        link = &(*link)->next;
    *link = dc->next;
    if (dc->clip_region)
    {
        GpiSetClipRegion(dc->ps, 0, NULL);
        GpiDestroyRegion(dc->ps, dc->clip_region);
    }
    if (dc->kind == OPEN32_DC_MEMORY)
    {
        GpiSetBitmap(dc->ps, 0);
        if (dc->default_bitmap)
            GpiDeleteBitmap(dc->default_bitmap);
        GpiDestroyPS(dc->ps);
    }
    else if (dc->kind == OPEN32_DC_PAINT)
        WinEndPaint(dc->ps);
    else if (dc->kind == OPEN32_DC_PRINTER || dc->kind == OPEN32_DC_PRINTER_INFORMATION)
    {
        HDC printer = GpiQueryDevice(dc->ps);
        GpiDestroyPS(dc->ps);
        DevCloseDC(printer);
    }
    else if (dc->kind != OPEN32_DC_PS_WRAPPER)
        WinReleasePS(dc->ps);
    lx_handle_release(&dc->pen);
    lx_handle_release(&dc->palette);
    lx_handle_release(&dc->brush);
    lx_handle_release(&dc->font);
    free(dc);
    return TRUE;
}

ULONG GetDC(ULONG window)
{
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    HWND target = window ? open32ClientWindow(window) : HWND_DESKTOP;
    if (!WinIsWindow(0, target))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    /* Foreign windows expose display capabilities through a local screen PS. */
    BOOL foreign = !WinQueryWindowDC(target);
    HPS ps = WinGetPS(foreign ? HWND_DESKTOP : target);
    Open32DC *dc = ps ? register_context(ps, window, OPEN32_DC_WINDOW) : NULL;
    if (!dc)
    {
        if (ps)
            WinReleasePS(ps);
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    dc->foreign_window = foreign;
    return ps;
}

LONG ReleaseDC(ULONG window, ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || dc->kind != OPEN32_DC_WINDOW || dc->window != window)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return discard_context(dc);
}

ULONG CreateCompatibleDC(ULONG source)
{
    if (source && !open32FindDC(source))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    SIZEL size = {0, 0};
    HPS ps = GpiCreatePS(0, 0, &size, 0);
    Open32DC *dc = ps ? register_context(ps, 0, OPEN32_DC_MEMORY) : NULL;
    if (!dc)
    {
        if (ps)
            GpiDestroyPS(ps);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    /* As in Win32, a new memory DC holds a 1x1 monochrome bitmap. */
    dc->default_bitmap = lx_pm_bitmap_create(1, 1, 1, 1);
    if (!dc->default_bitmap || GpiSetBitmap(ps, dc->default_bitmap) == UINT32_MAX)
    {
        discard_context(dc);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return ps;
}

BOOL DeleteDC(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (dc->kind != OPEN32_DC_MEMORY && dc->kind != OPEN32_DC_DISPLAY_INFORMATION &&
                dc->kind != OPEN32_DC_PRINTER && dc->kind != OPEN32_DC_PRINTER_INFORMATION &&
                dc->kind != OPEN32_DC_DISPLAY))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return discard_context(dc);
}

ULONG SelectObject(ULONG handle, ULONG object)
{
    Open32DC *dc = open32FindDC(handle);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(object, &borrow);
    if (dc && !bitmap)
    {
        lx_handle_release(&borrow);
        ULONG brush = open32SelectBrush(dc, object);
        if (brush)
        {
            open32MetafileObject(handle, object);
            return brush;
        }
        ULONG pen = open32SelectPen(dc, object);
        ULONG selected = pen ? pen : open32SelectFont(dc, object);
        if (selected)
            open32MetafileObject(handle, object);
        return selected;
    }
    if (!dc || dc->kind != OPEN32_DC_MEMORY || !bitmap)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    ULONG previous = GpiSetBitmap(handle, object);
    lx_handle_release(&borrow);
    if (previous == UINT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return previous;
}

BOOL BitBlt(ULONG destination, LONG x, LONG y, LONG width, LONG height, ULONG source, LONG source_x,
            LONG source_y, ULONG operation)
{
    TRACE_NATIVE("BitBlt(%u, %d, %d, %d, %d, %u, %d, %d, %08X)", (uint)destination, (int)x, (int)y,
                 (int)width, (int)height, (uint)source, (int)source_x, (int)source_y,
                 (uint)operation);
    if (open32IsMetafile(destination))
        return StretchBlt(destination, x, y, width, height, source, source_x, source_y, width,
                          height, operation);
    Open32DC *dst_mapping = open32FindDC(destination), *src_mapping = open32FindDC(source);
    if (dst_mapping && src_mapping &&
        (!open32IdentityMapping(dst_mapping) || !open32IdentityMapping(src_mapping)))
        return StretchBlt(destination, x, y, width, height, source, source_x, source_y, width,
                          height, operation);
    if (!open32CanDraw(destination) || !open32CanDraw(source) || width <= 0 || height <= 0 ||
        width > BLT_EXTENT_MAX || height > BLT_EXTENT_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    int64_t right = (int64_t)x + width, bottom = (int64_t)open32DCHeight(destination) - y - height;
    int64_t src_bottom = (int64_t)open32DCHeight(source) - source_y - height;
    if (right < INT32_MIN || right > INT32_MAX || bottom < INT32_MIN || bottom > INT32_MAX ||
        src_bottom < INT32_MIN || src_bottom > INT32_MAX || (int64_t)source_x + width > INT32_MAX ||
        bottom + height > INT32_MAX || src_bottom + height > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    POINTL points[4] = {{x, (LONG)bottom},
                        {(LONG)right, (LONG)(bottom + height)},
                        {source_x, (LONG)src_bottom},
                        {source_x + width, (LONG)(src_bottom + height)}};
    return open32BitmapBlt(
        destination, source, points,
        (LONG)((operation >> RASTER_OPERATION_INDEX_SHIFT) & RASTER_OPERATION_INDEX_MASK));
}

ULONG BeginPaint(ULONG window, PVOID output)
{
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    RECTL rect;
    open32CaretPaint(window, TRUE);
    HPS ps = WinBeginPaint(open32ClientWindow(window), 0, &rect);
    Open32DC *context = ps ? register_context(ps, window, OPEN32_DC_PAINT) : NULL;
    if (!context)
    {
        if (ps)
            WinEndPaint(ps);
        open32CaretPaint(window, FALSE);
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    context->clip_region = GpiCreateRegion(ps, WinIsRectEmpty(0, &rect) ? 0 : 1, &rect);
    if (!context->clip_region || !GpiSetClipRegion(ps, context->clip_region, NULL))
    {
        discard_context(context);
        open32CaretPaint(window, FALSE);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    open32ValidateQueuedPaint(window);
    uint8 bytes[WIN32_PAINTSTRUCT_SIZE] = {0};
    uint8 *paint = bytes + WIN32_PAINTSTRUCT_PAINT;
    lx_guest_store_u32(bytes + WIN32_PAINTSTRUCT_HDC, ps);
    lx_guest_store_u32(bytes + WIN32_PAINTSTRUCT_ERASE, !open32EraseBackground(window, ps));
    lx_guest_store_u32(paint + WIN32_RECT_LEFT, (ULONG)rect.xLeft);
    lx_guest_store_u32(paint + WIN32_RECT_TOP, (ULONG)(open32DCHeight(ps) - rect.yTop));
    lx_guest_store_u32(paint + WIN32_RECT_RIGHT, (ULONG)rect.xRight);
    lx_guest_store_u32(paint + WIN32_RECT_BOTTOM, (ULONG)(open32DCHeight(ps) - rect.yBottom));
    memcpy(output, bytes, sizeof bytes);
    return ps;
}

BOOL EndPaint(ULONG window, PVOID input)
{
    Open32DC *dc = input ? open32FindDC(lx_guest_load_u32(input)) : NULL;
    if (!dc || dc->kind != OPEN32_DC_PAINT || dc->window != window)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BOOL result = discard_context(dc);
    open32CaretPaint(window, FALSE);
    return result;
}

LONG SetBkMode(ULONG handle, LONG mode)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (mode != TRANSPARENT && mode != OPAQUE))
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return 0;
    }
    LONG previous = dc->background_mode;
    if (!GpiSetBackMix(handle, mode == TRANSPARENT ? BM_LEAVEALONE : BM_OVERPAINT))
        return 0;
    dc->background_mode = mode;
    open32MetafileWords(handle, WIN32_META_SETBKMODE, 1, &mode);
    return previous;
}

static LONG rgb_color(ULONG color)
{
    return (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
}

ULONG SetTextColor(ULONG handle, ULONG color)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc ||
        ((color & COLORREF_FLAGS_MASK) && (color & COLORREF_FLAGS_MASK) != COLORREF_PALETTE_RGB))
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    ULONG previous = dc->text_color;
    if (!GpiSetColor(handle, rgb_color(color)))
        return UINT32_MAX;
    dc->text_color = color;
    open32MetafileColor(handle, WIN32_META_SETTEXTCOLOR, color);
    return previous;
}

ULONG SetBkColor(ULONG handle, ULONG color)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc ||
        ((color & COLORREF_FLAGS_MASK) && (color & COLORREF_FLAGS_MASK) != COLORREF_PALETTE_RGB))
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    ULONG previous = dc->background_color;
    if (!GpiSetBackColor(handle, rgb_color(color)))
        return UINT32_MAX;
    dc->background_color = color;
    open32MetafileColor(handle, WIN32_META_SETBKCOLOR, color);
    return previous;
}

/* Palette edits reach every recording DC that has the palette selected. */
void open32MetafilePaletteChange(ULONG palette, USHORT function, ULONG first, ULONG count,
                                 PVOID entries)
{
    for (Open32DC *dc = contexts; dc; dc = dc->next)
        if (dc->palette_handle == palette)
            open32MetafilePaletteRecord(dc->ps, function, first, count, entries);
}

void open32DrawingCleanup(void)
{
    while (contexts)
        discard_context(contexts);
}

ULONG CreateCompatibleBitmap(ULONG handle, LONG width, LONG height)
{
    if (!open32FindDC(handle) || width < 0 || height < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmBitmap *selected = ps->bitmap_lease.resource;
    ULONG result = width && height
                       ? lx_pm_bitmap_create((ULONG)width, (ULONG)height, 1,
                                             selected ? selected->depth : DISPLAY_BITMAP_DEPTH)
                       : lx_pm_bitmap_create(1, 1, 1, 1);
    if (!result)
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    return result;
}

ULONG GetPixel(ULONG handle, LONG x, LONG y)
{
    uint32 pixel;
    if (!open32CanDraw(handle) || x < 0 || y < 0 || y >= open32DCHeight(handle) ||
        !lx_pm_raster_read(handle, x, open32DCHeight(handle) - 1 - y, 1, 1, &pixel))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return UINT32_MAX;
    }
    return (ULONG)rgb_color(pixel);
}

ULONG CreateIC(PSZ driver, PSZ device, PSZ output, PVOID mode)
{
    TRACE_NATIVE("CreateIC('%s', '%s', '%s', %p)", driver, device, output, mode);
    int printer = lx_printer_index(device);
    if (printer < 0)
        printer = lx_printer_index(driver);
    if (printer >= 0)
    {
        ULONG context = open32PrinterDefaults((unsigned)printer + 1, output, mode)
                            ? open32CreatePrinterContext((unsigned)printer + 1, TRUE)
                            : 0;
        if (context && !open32PrinterApply(context, mode))
        {
            DeleteDC(context);
            context = 0;
        }
        return context;
    }
    BOOL display = (driver && !strcasecmp(driver, "DISPLAY")) ||
                   (!driver && device && !strcasecmp(device, "DISPLAY"));
    if (!display)
    {
        open32SetError(ERROR_INVALID_PRINTER_NAME);
        return 0;
    }
    if (mode || (output && *output))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    HPS ps = WinGetPS(HWND_DESKTOP);
    if (!ps || !register_context(ps, 0, OPEN32_DC_DISPLAY_INFORMATION))
    {
        if (ps)
            WinReleasePS(ps);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return ps;
}

BOOL open32CanDraw(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    return dc && dc->kind != OPEN32_DC_DISPLAY_INFORMATION &&
           dc->kind != OPEN32_DC_PRINTER_INFORMATION && !dc->foreign_window;
}

ULONG open32CreatePrinterContext(unsigned kind, BOOL information)
{
    if (!open32Presentation())
        return 0;
    HDC printer = lx_pm_open_printer_dc(0, information ? OD_INFO : OD_QUEUED, kind);
    SIZEL size = {0, 0};
    HPS ps = printer ? GpiCreatePS(0, printer, &size, PU_PELS | GPIA_ASSOC) : 0;
    if (!ps ||
        !register_context(ps, 0, information ? OPEN32_DC_PRINTER_INFORMATION : OPEN32_DC_PRINTER))
    {
        if (ps)
            GpiDestroyPS(ps);
        if (printer)
            DevCloseDC(printer);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return ps;
}

ULONG CreateDC(PSZ driver, PSZ device, PSZ output, PVOID mode)
{
    TRACE_NATIVE("CreateDC('%s', '%s', '%s', %p)", driver, device, output, mode);
    int printer = lx_printer_index(device);
    if (printer < 0)
        printer = lx_printer_index(driver);
    if (printer >= 0)
    {
        ULONG context = open32PrinterDefaults((unsigned)printer + 1, output, mode)
                            ? open32CreatePrinterContext((unsigned)printer + 1, FALSE)
                            : 0;
        if (context && !open32PrinterApply(context, mode))
        {
            DeleteDC(context);
            context = 0;
        }
        return context;
    }
    if ((driver && !strcasecmp(driver, "DISPLAY")) ||
        (!driver && device && !strcasecmp(device, "DISPLAY")))
    {
        if (mode || (output && *output))
        {
            open32SetError(ERROR_NOT_SUPPORTED);
            return 0;
        }
        if (!open32Presentation())
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        HPS ps = WinGetPS(HWND_DESKTOP);
        if (!ps || !register_context(ps, 0, OPEN32_DC_DISPLAY))
        {
            if (ps)
                WinReleasePS(ps);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        return ps;
    }
    open32SetError(ERROR_INVALID_PRINTER_NAME);
    return 0;
}

ULONG GetWindowDC(ULONG window)
{
    if (!window)
        return GetDC(0);
    if (!open32Presentation() || !WinIsWindow(0, window))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    HPS ps = WinGetPS(window);
    if (!ps || !register_context(ps, window, OPEN32_DC_WINDOW))
    {
        if (ps)
            WinReleasePS(ps);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return ps;
}

ULONG HPSToHDC(ULONG window, ULONG ps, PSZ driver, PVOID rectangle)
{
    if (!lx_pm_valid_ps(ps))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    Open32DC *dc = open32FindDC(ps);
    if (!dc)
        dc = register_context(ps, window, OPEN32_DC_PS_WRAPPER);
    if (!dc)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    /* The target rectangle applies only when a driver name selects a device mapping. */
    if (driver && rectangle)
    {
        LONG l = (LONG)lx_guest_load_u32((uint8 *)rectangle + WIN32_RECT_LEFT),
             t = (LONG)lx_guest_load_u32((uint8 *)rectangle + WIN32_RECT_TOP),
             r = (LONG)lx_guest_load_u32((uint8 *)rectangle + WIN32_RECT_RIGHT),
             b = (LONG)lx_guest_load_u32((uint8 *)rectangle + WIN32_RECT_BOTTOM);
        if (r < l || b < t)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        /* The PS page maps onto the target rectangle, so a smaller rectangle scales. */
        LONG page_width = open32DCWidth(ps), page_height = open32DCHeight(ps);
        if (r > l && b > t && page_width > 0 && page_height > 0)
        {
            dc->map_mode = WIN32_MM_ANISOTROPIC;
            dc->window_x = dc->window_y = 0;
            dc->window_width = page_width;
            dc->window_height = page_height;
            dc->viewport_width = r - l;
            dc->viewport_height = b - t;
        }
        dc->viewport_x = l;
        dc->viewport_y = t;
    }
    return ps;
}

VOID DeleteHDCObjects(ULONG ps)
{
    Open32DC *dc = open32FindDC(ps);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return;
    }
    /* Only HPSToHDC wrappers own state here; ordinary DCs are left untouched. */
    if (dc->kind == OPEN32_DC_PS_WRAPPER)
        discard_context(dc);
}
