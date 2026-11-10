/* Public domain. No rights reserved. Wholly AI-authored. */
/* GetDeviceCaps for display, memory and printer DCs. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "emulator/abi.h"

enum
{
    /* An unqueried printer page is A4 at 300 dpi. */
    DEFAULT_PAGE_WIDTH = 2480,
    DEFAULT_PAGE_HEIGHT = 3508,
    PRINTER_DPI = 300,
    /* lx_pm_printer_kind of the PCL queue, which prints in monochrome. */
    PCL_PRINTER = 2,
    /* Millimetres per inch, in tenths. */
    TENTHS_MM_PER_INCH = 254,
    REPORTED_DRIVER_VERSION = 0x100,
    /* ASPECTX/Y: square pixels in conventional GDI aspect units; ASPECTXY is their
       rounded diagonal. */
    PIXEL_ASPECT = 36,
    PIXEL_ASPECT_DIAGONAL = 51,
    TRUE_COLOR_BITS = 24
};

LONG GetDeviceCaps(ULONG handle, LONG index)
{
    if (!open32FindDC(handle))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    unsigned printer = lx_pm_printer_kind(GpiQueryDevice(handle));
    unsigned page_width = DEFAULT_PAGE_WIDTH, page_height = DEFAULT_PAGE_HEIGHT;
    if (printer)
        lx_pm_printer_size(GpiQueryDevice(handle), &page_width, &page_height);
    LONG width = printer ? (LONG)page_width : WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
         height = printer ? (LONG)page_height : WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    switch (index)
    {
        case WIN32_DRIVERVERSION:
            return REPORTED_DRIVER_VERSION;
        case WIN32_TECHNOLOGY:
            return printer ? WIN32_DT_RASPRINTER : WIN32_DT_RASDISPLAY;
        case WIN32_HORZSIZE:
            return printer ? width * TENTHS_MM_PER_INCH / (PRINTER_DPI * 10)
                           : (LONG)((int64_t)width * TENTHS_MM_PER_INCH / (lx_pm_dc_dpi(0) * 10));
        case WIN32_VERTSIZE:
            return printer ? height * TENTHS_MM_PER_INCH / (PRINTER_DPI * 10)
                           : (LONG)((int64_t)height * TENTHS_MM_PER_INCH / (lx_pm_dc_dpi(0) * 10));
        case WIN32_HORZRES:
            return width;
        case WIN32_VERTRES:
            return height;
        case WIN32_BITSPIXEL:
            return printer == PCL_PRINTER                    ? 1
                   : printer || !GProcessState.display_depth ? TRUE_COLOR_BITS
                                                             : (LONG)GProcessState.display_depth;
        case WIN32_PLANES:
            return 1;
        case WIN32_NUMBRUSHES:
        case WIN32_NUMPENS:
            return -1; /* Unlimited RGB brushes/pens/colors */
        case WIN32_NUMCOLORS:
            return printer == PCL_PRINTER ? 2 : -1;
        case WIN32_RASTERCAPS:
            return WIN32_RC_BITBLT;
        case WIN32_ASPECTX:
        case WIN32_ASPECTY:
            return PIXEL_ASPECT;
        case WIN32_ASPECTXY:
            return PIXEL_ASPECT_DIAGONAL;
        case WIN32_LOGPIXELSX:
        case WIN32_LOGPIXELSY:
            return printer ? PRINTER_DPI : (LONG)lx_pm_dc_dpi(0);
        case WIN32_PHYSICALWIDTH:
            return printer ? width : 0;
        case WIN32_PHYSICALHEIGHT:
            return printer ? height : 0;
        case WIN32_COLORRES:
            return printer == PCL_PRINTER ? 1 : TRUE_COLOR_BITS;
        default:
            return 0;
    }
}
