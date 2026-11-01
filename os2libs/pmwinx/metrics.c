/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows pixel metrics follow the active PM display and control geometry. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmwinx/drawing.h"
LONG GetSystemMetrics(LONG index)
{
    TRACE_NATIVE("GetSystemMetrics(%d)", index);
    if (!open32Presentation())
        return 0;
    static const LONG pm_metrics[] = {20, 21, 22, 23, 30, 26, 27, 28, 29,
                                      24, 25, 38, 39, 40, 41, 35, 36, 37};
    if (index >= 0 && (ULONG)index < sizeof pm_metrics / sizeof *pm_metrics)
        return WinQuerySysValue(HWND_DESKTOP, pm_metrics[index]);
    switch (index)
    {
        case 19:
            return WinQuerySysValue(HWND_DESKTOP, 48); /* SM_MOUSEPRESENT */
        case 23:
            return WinQuerySysValue(HWND_DESKTOP, 0); /* SM_SWAPBUTTON */
        case 20:
            return WinQuerySysValue(HWND_DESKTOP, 24); /* SM_CYVSCROLL */
        case 21:
            return WinQuerySysValue(HWND_DESKTOP, 25); /* SM_CXHSCROLL */
        case 30:
            return WinQuerySysValue(HWND_DESKTOP, 33); /* SM_CXSIZE */
        case 31:
            return WinQuerySysValue(HWND_DESKTOP, 34); /* SM_CYSIZE */
        case 32:
            return WinQuerySysValue(HWND_DESKTOP, 4); /* SM_CXSIZEFRAME */
        case 33:
            return WinQuerySysValue(HWND_DESKTOP, 5); /* SM_CYSIZEFRAME */
        case 36:
            return WinQuerySysValue(HWND_DESKTOP, 2); /* SM_CXDOUBLECLK */
        case 37:
            return WinQuerySysValue(HWND_DESKTOP, 3); /* SM_CYDOUBLECLK */
        case 43:
            return WinQuerySysValue(HWND_DESKTOP, 43); /* SM_CMOUSEBUTTONS */
        default:
            return 0;
    }
}

LONG GetDeviceCaps(ULONG handle, LONG index)
{
    if (!open32FindDC(handle))
    {
        open32SetError(6);
        return 0;
    }
    LONG width = WinQuerySysValue(HWND_DESKTOP, 20), height = WinQuerySysValue(HWND_DESKTOP, 21);
    switch (index)
    {
        case 0:
            return 0x100; /* DRIVERVERSION */
        case 2:
            return 1; /* TECHNOLOGY: DT_RASDISPLAY */
        case 4:
            return (LONG)((int64_t)width * 254 / 960); /* HORZSIZE in mm at 96 DPI */
        case 6:
            return (LONG)((int64_t)height * 254 / 960); /* VERTSIZE */
        case 8:
            return width; /* HORZRES */
        case 10:
            return height; /* VERTRES */
        case 12:
            return 24; /* BITSPIXEL */
        case 14:
            return 1; /* PLANES */
        case 16:
        case 18:
        case 24:
            return -1; /* Unlimited RGB brushes/pens/colors */
        case 38:
            return 1; /* RASTERCAPS: RC_BITBLT */
        case 88:
        case 90:
            return 96; /* LOGPIXELSX / LOGPIXELSY */
        case 108:
            return 24; /* COLORRES */
        default:
            return 0;
    }
}

ULONG GetSysColor(LONG index)
{
    static const LONG colors[] = {-25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15,
                                  -14, -13, -32, -33, -40, -39, -46, -18, -29, -41};
    if (!open32Presentation())
        return 0;
    LONG pm_index = index < 0 ? index : index < 21 ? colors[index] : 0;
    ULONG rgb = (ULONG)WinQuerySysColor(HWND_DESKTOP, pm_index, 0);
    return (rgb & 255) << 16 | (rgb & 0xFF00) | (rgb >> 16 & 255);
}
