/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_DRAWING_H
#define TWOINE_OPEN32_DRAWING_H
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"

typedef struct Open32DC
{
    HPS ps;
    HWND window;
    BOOL foreign_window;
    ULONG kind, default_bitmap, text_color, background_color;
    BOOL print_active, print_page;
    LONG background_mode;
    LONG poly_fill_mode, stretch_mode, character_extra, justify_extra, justify_breaks;
    ULONG abort_proc;
    ULONG text_alignment;
    ULONG font_handle;
    LxHandleBorrow font, brush, palette, pen;
    ULONG pen_handle;
    LONG position_x, position_y, rop2;
    ULONG palette_handle, clip_region, lock_region;
    ULONG brush_handle;
    LONG brush_origin_x, brush_origin_y;
    LONG map_mode, window_x, window_y, viewport_x, viewport_y;
    LONG window_width, window_height, viewport_width, viewport_height;
    /* Logical points pass through world, window-to-viewport, then playback-fit transforms. */
    double world[6], fit[6];
    BOOL world_active, fit_active;
    LONG arc_clockwise;
    double miter_limit;
    /* Job properties set by escapes; zero means the default of one copy, simplex, full quality. */
    ULONG print_copies, print_duplex;
    BOOL print_draft;
    struct Open32Path *path;
    struct Open32SavedDC *saved;
    struct Open32DC *next;
} Open32DC;

void open32SavedDCCleanup(Open32DC *dc);
/* The stock font for a GetStockObject index; 0 (ERROR_INVALID_PARAMETER) for any other. */
ULONG open32StockFont(LONG index);
/* Shapes and the world transform without exports of their own, which EMF playback draws. */
BOOL open32AngleArc(ULONG handle, LONG x, LONG y, ULONG radius, float start, float sweep);
BOOL open32ArcTo(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, LONG start_x,
                 LONG start_y, LONG end_x, LONG end_y);
BOOL open32Chord(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, LONG start_x,
                 LONG start_y, LONG end_x, LONG end_y);
BOOL open32FloodFill(ULONG handle, LONG x, LONG y, ULONG color, ULONG mode);
BOOL open32WorldTransform(ULONG handle, const double matrix[6], ULONG mode);
BOOL open32BrushPixel(Open32DC *dc, LONG x, LONG y, uint32 *rgb);
ULONG open32DibPatternBrush(PVOID information, PVOID bits, ULONG usage, BOOL monochrome);
uint8 *open32BrushDib(ULONG handle, size_t *information_bytes, size_t *bytes, BOOL *monochrome);
Open32DC *open32FindDC(ULONG handle);
BOOL open32MapPoint(Open32DC *dc, LONG *x, LONG *y, BOOL device);
BOOL open32Rotated(const Open32DC *dc);
BOOL open32RotatedFill(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, ULONG brush);
void open32MetafilePaletteChange(ULONG palette, USHORT function, ULONG first, ULONG count,
                                 PVOID entries);
BOOL open32IdentityMapping(Open32DC *dc);
ULONG open32CreatePrinterContext(unsigned kind, BOOL information);
BOOL open32PrinterDefaults(unsigned kind, const char *output, PVOID mode);
BOOL open32PrinterApply(ULONG handle, PVOID mode);
BOOL open32CanDraw(ULONG handle);
BOOL open32AbortCheck(ULONG handle);
BOOL open32BitmapBlt(ULONG destination, ULONG source, PPOINTL points, LONG rop);
LONG open32DCHeight(ULONG handle);
LONG open32DCWidth(ULONG handle);
ULONG open32SelectFont(Open32DC *dc, ULONG handle);
BOOL open32DeleteFont(ULONG handle);
LONG open32FontObject(ULONG handle, LONG capacity, PVOID output);
ULONG open32StockPen(LONG index);
ULONG open32SelectPen(Open32DC *dc, ULONG handle);
BOOL open32DeletePen(ULONG handle);
LONG open32PenObject(ULONG handle, LONG capacity, PVOID output);
ULONG open32StockBrush(LONG index);
ULONG open32SelectBrush(Open32DC *dc, ULONG handle);
BOOL open32DeleteBrush(ULONG handle);
LONG open32BrushObject(ULONG handle, LONG capacity, PVOID output);
ULONG open32StockPalette(void);
BOOL open32DeletePalette(ULONG handle);
LONG open32PaletteObject(ULONG handle, LONG capacity, PVOID output);
BOOL open32DeleteRegion(ULONG handle);
#include "os2/ps/awe/gdi/paths.h"
#include "os2/ps/awe/gdi/metafiles/metafile.h"
#endif
