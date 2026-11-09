/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 window-rectangle sizing for PMWINX ordinal 6. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/os2win.h"
#include <string.h>

enum
{
    style_maximize_box = 0x00010000,
    style_minimize_box = 0x00020000,
    style_thick_frame = 0x00040000,
    style_system_menu = 0x00080000,
    style_dialog_frame = 0x00400000,
    style_border = 0x00800000,
    style_caption = style_border | style_dialog_frame,
    style_overlapped_window = style_caption | style_system_menu | style_thick_frame |
                              style_minimize_box | style_maximize_box,
    metric_size_border_x = 4,
    metric_size_border_y = 5,
    metric_border_x = 26,
    metric_border_y = 27,
    metric_dialog_frame_x = 28,
    metric_dialog_frame_y = 29,
    metric_title_bar = 30,
    metric_menu = 35
};

static void grow(LONG *rectangle, LONG metric_x, LONG metric_y)
{
    rectangle[0] -= WinQuerySysValue(HWND_DESKTOP, metric_x);
    rectangle[1] -= WinQuerySysValue(HWND_DESKTOP, metric_y);
    rectangle[2] += WinQuerySysValue(HWND_DESKTOP, metric_x);
    rectangle[3] += WinQuerySysValue(HWND_DESKTOP, metric_y);
}

/* rectangle: left, top, right and bottom in Open32 orientation, updated in
   place from the client area to the window area. The extended style does not
   affect the result. A menu reserves its height plus one row and one border. */
BOOL AdjustWindowRectEx(PVOID rectangle, ULONG style, BOOL menu, ULONG extended_style)
{
    LONG edges[4];
    (void)extended_style;
    memcpy(edges, rectangle, sizeof edges);
    BOOL standard = !style || (style & style_overlapped_window) == style_overlapped_window;
    if (menu)
    {
        edges[1] -= WinQuerySysValue(HWND_DESKTOP, metric_menu) + 1;
        edges[1] -= WinQuerySysValue(HWND_DESKTOP, metric_border_y);
    }
    if (standard || (style & style_caption) == style_caption ||
        style & (style_system_menu | style_minimize_box | style_maximize_box))
        edges[1] -= WinQuerySysValue(HWND_DESKTOP, metric_title_bar);
    if (standard || style & style_thick_frame)
        grow(edges, metric_size_border_x, metric_size_border_y);
    else if (style & style_dialog_frame)
        grow(edges, metric_dialog_frame_x, metric_dialog_frame_y);
    else if (style & style_border)
        grow(edges, metric_border_x, metric_border_y);
    memcpy(rectangle, edges, sizeof edges);
    return TRUE;
}
