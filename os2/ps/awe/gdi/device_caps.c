/* Public domain. No rights reserved. Wholly AI-authored. */
/* GetDeviceCaps for display, memory and printer DCs. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "emulator/abi.h"

LONG GetDeviceCaps(ULONG handle, LONG index)
{
    if (!open32FindDC(handle))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    unsigned printer = lx_pm_printer_kind(GpiQueryDevice(handle));
    unsigned page_width = 2480, page_height = 3508;
    if (printer)
        lx_pm_printer_size(GpiQueryDevice(handle), &page_width, &page_height);
    LONG width = printer ? (LONG)page_width : WinQuerySysValue(HWND_DESKTOP, 20),
         height = printer ? (LONG)page_height : WinQuerySysValue(HWND_DESKTOP, 21);
    switch (index)
    {
        case 0:
            return 0x100; /* DRIVERVERSION */
        case 2:
            return printer ? 2 : 1; /* DT_RASPRINTER or DT_RASDISPLAY */
        case 4:
            return printer ? width * 254 / 3000
                           : (LONG)((int64_t)width * 254 / (lx_pm_dc_dpi(0) * 10));
        case 6:
            return printer ? height * 254 / 3000
                           : (LONG)((int64_t)height * 254 / (lx_pm_dc_dpi(0) * 10));
        case 8:
            return width; /* HORZRES */
        case 10:
            return height; /* VERTRES */
        case 12:           /* BITSPIXEL */
            return printer == 2                              ? 1
                   : printer || !GProcessState.display_depth ? 24
                                                             : (LONG)GProcessState.display_depth;
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
