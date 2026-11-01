/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_DRAWING_H
#define TWOINE_OPEN32_DRAWING_H
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
typedef struct Open32DC
{
    HPS ps;
    HWND window;
    ULONG kind, default_bitmap, text_color, background_color;
    LONG background_mode;
    ULONG font_handle;
    LxHandleBorrow font, brush;
    ULONG brush_handle;
    struct Open32DC *next;
} Open32DC;
Open32DC *open32FindDC(ULONG handle);
LONG open32DCHeight(ULONG handle);
ULONG open32SelectFont(Open32DC *dc, ULONG handle);
BOOL open32DeleteFont(ULONG handle);
LONG open32FontObject(ULONG handle, LONG capacity, PVOID output);
ULONG open32StockBrush(LONG index);
ULONG open32SelectBrush(Open32DC *dc, ULONG handle);
BOOL open32DeleteBrush(ULONG handle);
LONG open32BrushObject(ULONG handle, LONG capacity, PVOID output);
#endif
