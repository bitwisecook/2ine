/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_DRAWING_H
#define TWOINE_OPEN32_DRAWING_H
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
typedef struct Open32DC
{
    HPS ps;
    HWND window;
    BOOL foreign_window;
    ULONG kind, default_bitmap, text_color, background_color;
    LONG background_mode;
    ULONG text_alignment;
    ULONG font_handle;
    LxHandleBorrow font, brush, palette, pen;
    ULONG pen_handle;
    LONG position_x, position_y, rop2;
    ULONG palette_handle, clip_region;
    ULONG brush_handle;
    LONG brush_origin_x, brush_origin_y;
    struct Open32SavedDC *saved;
    struct Open32DC *next;
} Open32DC;
void open32SavedDCCleanup(Open32DC *dc);
BOOL open32BrushPixel(Open32DC *dc, LONG x, LONG y, uint32 *rgb);
Open32DC *open32FindDC(ULONG handle);
BOOL open32CanDraw(ULONG handle);
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
#endif
