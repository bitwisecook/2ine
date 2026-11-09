/* Public domain. No rights reserved. Wholly AI-authored. */
/* The pen object, shared by pen management and line drawing. */
#ifndef TWOINE_PMWINX_PENS_H
#define TWOINE_PMWINX_PENS_H
#include "os2/include/os2win.h"
#include "emulator/handles.h"

typedef struct Open32Pen
{
    LONG style, width;
    ULONG color;
    BOOL stock;
    /* Windows PS_ENDCAP and PS_JOIN selectors: 0 round; caps 1 square, 2 flat; joins 1 bevel,
       2 miter. */
    ULONG cap, join;
    ULONG brush_handle;
    LxHandleBorrow brush;
} Open32Pen;
#endif
