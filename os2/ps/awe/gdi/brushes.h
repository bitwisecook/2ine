/* Public domain. No rights reserved. Wholly AI-authored. */
/* The brush object, shared by brush management and area fills. */
#ifndef TWOINE_PMWINX_BRUSHES_H
#define TWOINE_PMWINX_BRUSHES_H
#include "os2/include/os2win.h"

typedef struct Open32Brush
{
    ULONG color, style;
    ULONG bitmap, width, height, depth;
    uint32 *pixels;
    BOOL stock;
} Open32Brush;
#endif
