/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows pixel metrics follow the active PM display and control geometry. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include "guest/abi.h"
LONG GetMenuCheckMarkDimensions(void)
{
    TRACE_NATIVE("GetMenuCheckMarkDimensions()");
    if (!open32Presentation())
        return 0;
    enum
    {
        menu_check_bitmap = 6
    }; /* SBMP_MENUCHECK */
    ULONG bitmap = WinGetSysBitmap(HWND_DESKTOP, menu_check_bitmap);
    if (!bitmap)
        return 0;
    uint8 header[12] = {0};
    BOOL found = GpiQueryBitmapParameters(bitmap, header);
    GpiDeleteBitmap(bitmap);
    if (!found)
        return 0;
    return (LONG)((ULONG)lx_guest_load_u16(header + 4) | (ULONG)lx_guest_load_u16(header + 6)
                                                             << 16);
}

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
    unsigned printer = lx_pm_printer_kind(GpiQueryDevice(handle));
    LONG width = printer ? 2480 : WinQuerySysValue(HWND_DESKTOP, 20),
         height = printer ? 3508 : WinQuerySysValue(HWND_DESKTOP, 21);
    switch (index)
    {
        case 0:
            return 0x100; /* DRIVERVERSION */
        case 2:
            return printer ? 2 : 1; /* DT_RASPRINTER or DT_RASDISPLAY */
        case 4:
            return printer ? 210 : (LONG)((int64_t)width * 254 / (lx_pm_dc_dpi(0) * 10));
        case 6:
            return printer ? 297 : (LONG)((int64_t)height * 254 / (lx_pm_dc_dpi(0) * 10));
        case 8:
            return width; /* HORZRES */
        case 10:
            return height; /* VERTRES */
        case 12:
            return printer == 2 ? 1 : 24;
        case 14:
            return 1; /* PLANES */
        case 16:
        case 18:
            return -1; /* Unlimited RGB brushes/pens/colors */
        case 24:
            return printer == 2 ? 2 : -1;
        case 38:
            return 1; /* RASTERCAPS: RC_BITBLT */
        case 40:
        case 42:
            return 36; /* ASPECTX/Y: square raster pixels in conventional GDI aspect units. */
        case 44:
            return 51; /* ASPECTXY: rounded diagonal of the 36-by-36 aspect vector. */
        case 88:
        case 90:
            return printer ? 300 : (LONG)lx_pm_dc_dpi(0);
        case 110:
            return printer ? width : 0; /* PHYSICALWIDTH */
        case 111:
            return printer ? height : 0; /* PHYSICALHEIGHT */
        case 108:
            return printer == 2 ? 1 : 24; /* COLORRES */
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
