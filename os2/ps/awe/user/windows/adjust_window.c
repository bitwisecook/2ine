/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 window-rectangle sizing for PMWINX ordinal 6. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/os2win.h"
#include "os2/ps/awe/win32.h"
#include <string.h>

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
    BOOL standard = !style || (style & WIN32_WS_OVERLAPPEDWINDOW) == WIN32_WS_OVERLAPPEDWINDOW;
    if (menu)
    {
        edges[1] -= WinQuerySysValue(HWND_DESKTOP, SV_CYMENU) + 1;
        edges[1] -= WinQuerySysValue(HWND_DESKTOP, SV_CYBORDER);
    }
    if (standard || (style & WIN32_WS_CAPTION) == WIN32_WS_CAPTION ||
        style & (WIN32_WS_SYSMENU | WIN32_WS_MINIMIZEBOX | WIN32_WS_MAXIMIZEBOX))
        edges[1] -= WinQuerySysValue(HWND_DESKTOP, SV_CYTITLEBAR);
    if (standard || style & WIN32_WS_THICKFRAME)
        grow(edges, SV_CXSIZEBORDER, SV_CYSIZEBORDER);
    else if (style & WIN32_WS_DLGFRAME)
        grow(edges, SV_CXDLGFRAME, SV_CYDLGFRAME);
    else if (style & WIN32_WS_BORDER)
        grow(edges, SV_CXBORDER, SV_CYBORDER);
    memcpy(rectangle, edges, sizeof edges);
    return TRUE;
}

BOOL AdjustWindowRect(PVOID rectangle, ULONG style, BOOL menu)
{
    return AdjustWindowRectEx(rectangle, style, menu, 0);
}
