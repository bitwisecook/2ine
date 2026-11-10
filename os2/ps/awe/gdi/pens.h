/* Public domain. No rights reserved. Wholly AI-authored. */
/* The pen object, shared by pen management and line drawing. */
#ifndef TWOINE_PMWINX_PENS_H
#define TWOINE_PMWINX_PENS_H
#include "os2/include/os2win.h"
#include "emulator/handles.h"

/* Open32Pen's cap and join: the PS_ENDCAP_* and PS_JOIN_* selectors shifted down. */
enum
{
    PEN_CAP_ROUND = 0,
    PEN_CAP_SQUARE = 1,
    PEN_CAP_FLAT = 2,
    PEN_JOIN_ROUND = 0,
    PEN_JOIN_BEVEL = 1,
    PEN_JOIN_MITER = 2
};

typedef struct Open32Pen
{
    LONG style, width;
    ULONG color;
    BOOL stock;
    ULONG cap, join;
    ULONG brush_handle;
    LxHandleBorrow brush;
} Open32Pen;
#endif
