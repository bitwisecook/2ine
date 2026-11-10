/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window DC capabilities describe the PM renderer and its configured logical display DPI. */
#include "os2/include/private/export.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "host/font.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmdevp.h"
#include "os2/ps/pmgpi/internal.h"

enum
{
    /* lx_pm_printer_kind: lx_printer_queues index + 1, so 0 is not a printer. */
    PRINTER_KIND_PDF = 1,
    /* The printers render A4 pages at 300 dpi. */
    PRINTER_DPI = 300,
    PRINTER_PELS_PER_METRE = 11811,
    A4_WIDTH_MM = 210,
    A4_HEIGHT_MM = 297,
    A4_WIDTH_PELS = 2480,
    A4_HEIGHT_PELS = 3508,
    /* DevQueryDeviceNames returns STR32 names, STR64 descriptions and STR16 data types. */
    DEVICE_NAME_SIZE = 32,
    DEVICE_DESCRIPTION_SIZE = 64,
    DATA_TYPE_SIZE = 16,
    /* HCINFO: CHAR szFormname[32], then nine LONGs from cx to flAttributes. */
    HCINFO_SIZE = 68,
    HCINFO_FORMNAME_SIZE = 32,
    HCINFO_FIELDS = 9
};

HDC DevOpenDC(HAB hab, LONG type, PSZ token, LONG count, PVOID data, HDC compatible)
{
    TRACE_NATIVE("DevOpenDC(%u, %d, '%s', %d, %p, %u)", (uint)hab, (int)type, token, (int)count,
                 data, (uint)compatible);
    if (count < 0 || count > GPI_DEVOPEN_FIELDS || (count && !data))
        return 0;
    PSZ *fields = data;
    int printer =
        count > GPI_DEVOPEN_DRIVERNAME ? lx_printer_index(fields[GPI_DEVOPEN_DRIVERNAME]) : -1;
    if (printer < 0 && count)
        printer = lx_printer_index(fields[GPI_DEVOPEN_LOGADDRESS]);
    if (printer < 0)
        printer = lx_printer_index(token);
    if (printer >= 0)
        return lx_pm_open_printer_dc(hab, type, (unsigned)printer + 1);
    if ((token && strcmp(token, "*") && strcasecmp(token, "DISPLAY")) ||
        (count > GPI_DEVOPEN_DRIVERNAME && fields[GPI_DEVOPEN_DRIVERNAME] &&
         strcasecmp(fields[GPI_DEVOPEN_DRIVERNAME], "DISPLAY")))
        return 0;
    return lx_pm_open_dc(hab, type, compatible);
}

ULONG DevCloseDC(HDC dc)
{
    return (ULONG)lx_pm_close_dc(dc);
}

BOOL DevQueryCaps(HDC dc, LONG start, LONG count, PLONG output)
{
    TRACE_NATIVE("DevQueryCaps(%u, %d, %d, %p)", (uint)dc, (int)start, (int)count, output);
    if (start < 0 || count < 0 || start > GPI_CAPS_COUNT || count > GPI_CAPS_COUNT - start ||
        (count && !output) || !lx_pm_dc_valid(dc))
        return FALSE;
    LONG caps[GPI_CAPS_COUNT] = {0};
    unsigned dpi = lx_pm_dc_dpi(dc);
    LxHostFont *font = lx_host_font_open(
        0, (GPI_DEFAULT_FONT_PIXELS * dpi + GPI_REFERENCE_DPI / 2) / GPI_REFERENCE_DPI);
    LxHostFontMetrics m;
    if (!lx_host_font_metrics(font, &m))
    {
        lx_host_font_close(font);
        return FALSE;
    }
    lx_host_font_close(font);
    LONG width = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
         height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    caps[CAPS_IO_CAPS] = CAPS_IO_SUPPORTS_OP;
    caps[CAPS_TECHNOLOGY] = CAPS_TECH_RASTER_DISPLAY;
    caps[CAPS_WIDTH] = width;
    caps[CAPS_HEIGHT] = height;
    caps[CAPS_WIDTH_IN_CHARS] = m.average_width ? width / m.average_width : 0;
    caps[CAPS_HEIGHT_IN_CHARS] = m.ascent + m.descent ? height / (m.ascent + m.descent) : 0;
    /* Pels per metre: dpi * 10000 / 254, rounded. */
    caps[CAPS_HORIZONTAL_RESOLUTION] = caps[CAPS_VERTICAL_RESOLUTION] =
        (LONG)((dpi * 10000 + 127) / 254);
    caps[CAPS_CHAR_WIDTH] = caps[CAPS_SMALL_CHAR_WIDTH] = caps[CAPS_GRAPHICS_CHAR_WIDTH] =
        m.average_width;
    caps[CAPS_CHAR_HEIGHT] = caps[CAPS_SMALL_CHAR_HEIGHT] = caps[CAPS_GRAPHICS_CHAR_HEIGHT] =
        m.ascent + m.descent;
    /* Device colors are independent of a PS's initial 16 indexed entries.
       The retained canvas supports all 24-bit RGB values simultaneously. */
    const unsigned depth =
        GProcessState.display_depth ? GProcessState.display_depth : GPI_DEFAULT_DISPLAY_DEPTH;
    caps[CAPS_COLORS] = 1L << depth;
    caps[CAPS_COLOR_PLANES] = 1;
    caps[CAPS_COLOR_BITCOUNT] = (LONG)depth;
    caps[CAPS_COLOR_TABLE_SUPPORT] = CAPS_COLTABL_RGB_8;
    caps[CAPS_MOUSE_BUTTONS] = 3;
    caps[CAPS_FOREGROUND_MIX_SUPPORT] = CAPS_FM_OVERPAINT;
    caps[CAPS_WINDOW_BYTE_ALIGNMENT] = CAPS_BYTE_ALIGN_RECOMMENDED;
    caps[CAPS_DEVICE_FONTS] = (LONG)lx_host_font_count();
    caps[CAPS_DEVICE_WINDOWING] = CAPS_DEV_WINDOWING_SUPPORT;
    caps[CAPS_PHYS_COLORS] = 1L << depth;
    /* The highest index a logical colour table may load. */
    caps[CAPS_COLOR_INDEX] = depth > 8 ? 255 : (1L << depth) - 1;
    caps[CAPS_HORIZONTAL_FONT_RES] = caps[CAPS_VERTICAL_FONT_RES] = (LONG)dpi;
    unsigned printer = lx_pm_printer_kind(dc);
    if (printer)
    {
        caps[CAPS_TECHNOLOGY] = CAPS_TECH_RASTER_PRINTER;
        caps[CAPS_WIDTH] = A4_WIDTH_PELS;
        caps[CAPS_HEIGHT] = A4_HEIGHT_PELS;
        caps[CAPS_WIDTH_IN_CHARS] = caps[CAPS_HEIGHT_IN_CHARS] = 0;
        caps[CAPS_HORIZONTAL_RESOLUTION] = caps[CAPS_VERTICAL_RESOLUTION] = PRINTER_PELS_PER_METRE;
        /* The PDF printer is 24-bit colour; the PCL printer is monochrome. */
        caps[CAPS_COLORS] = caps[CAPS_PHYS_COLORS] = printer == PRINTER_KIND_PDF ? 16777216 : 2;
        caps[CAPS_COLOR_BITCOUNT] = printer == PRINTER_KIND_PDF ? 24 : 1;
        caps[CAPS_MOUSE_BUTTONS] = 0;
        /* Every Boolean raster mix. */
        caps[CAPS_FOREGROUND_MIX_SUPPORT] = caps[CAPS_BACKGROUND_MIX_SUPPORT] = 127;
        caps[CAPS_BITMAP_FORMATS] = 2;
        /* Bitblt, scaling, pel and bitmap support. */
        caps[CAPS_RASTER_CAPS] = 0x1ff;
        caps[CAPS_DEVICE_WINDOWING] = 0;
        caps[CAPS_COLOR_INDEX] = 15;
        caps[CAPS_HORIZONTAL_FONT_RES] = caps[CAPS_VERTICAL_FONT_RES] = PRINTER_DPI;
    }
    for (LONG i = 0; i < count; i++)
        output[i] = caps[start + i];
    return TRUE;
}

BOOL DevQueryDeviceNames(HAB hab, PSZ driver, PLONG devices, PVOID names, PVOID descriptions,
                         PLONG types, PVOID data_types)
{
    TRACE_NATIVE("DevQueryDeviceNames(%u, '%s', %p, %p, %p, %p, %p)", (uint)hab, driver, devices,
                 names, descriptions, types, data_types);
    if (!devices || !types || *devices < 0 || *types < 0 || !driver)
        return FALSE;
    LONG device_capacity = *devices, type_capacity = *types;
    *devices = *types = 0;
    int printer = lx_printer_index(driver);
    if (printer < 0 && strcasecmp(driver, "DISPLAY"))
        return FALSE;
    if (device_capacity && (!names || !descriptions))
        return FALSE;
    if (type_capacity && !data_types)
        return FALSE;
    if (device_capacity)
    {
        memset(names, 0, DEVICE_NAME_SIZE);
        memset(descriptions, 0, DEVICE_DESCRIPTION_SIZE);
        strcpy(names, printer >= 0 ? lx_printer_queues[printer] : "DISPLAY");
        strcpy(descriptions, printer >= 0 ? lx_printer_descriptions[printer] : "2ine display");
    }
    if (type_capacity)
    {
        memset(data_types, 0, DATA_TYPE_SIZE);
        memcpy(data_types, "PM_Q_STD", sizeof "PM_Q_STD");
    }
    *devices = *types = 1;
    return TRUE;
}

LONG DevQueryHardcopyCaps(HDC dc, LONG first, LONG count, PVOID output)
{
    TRACE_NATIVE("DevQueryHardcopyCaps(%u, %d, %d, %p)", (uint)dc, (int)first, (int)count, output);
    if (!lx_pm_dc_valid(dc) || first < 0 || count < 0 || (count && !output))
        return DQHC_ERROR;
    if (!lx_pm_printer_kind(dc))
        return 0;
    /* Printers offer one form, A4. */
    if (first > 1 || count > 1)
        return DQHC_ERROR;
    if (!count)
        return 1;
    if (first == 1)
        return 0;
    unsigned char *record = output;
    memset(record, 0, HCINFO_SIZE);
    memcpy(record, "A4", sizeof "A4");
    const LONG values[HCINFO_FIELDS] = {A4_WIDTH_MM,
                                        A4_HEIGHT_MM,
                                        0,
                                        0,
                                        A4_WIDTH_MM,
                                        A4_HEIGHT_MM,
                                        A4_WIDTH_PELS,
                                        A4_HEIGHT_PELS,
                                        HCAPS_CURRENT | HCAPS_SELECTABLE};
    for (unsigned i = 0; i < HCINFO_FIELDS; i++)
        lx_guest_store_u32(record + HCINFO_FORMNAME_SIZE + i * 4, (ULONG)values[i]);
    return 1;
}

LONG DevEscape(HDC dc, LONG code, LONG input_count, PVOID input, PLONG output_count, PVOID output)
{
    TRACE_NATIVE("DevEscape(%u, %d, %d, %p, %p, %p)", (uint)dc, (int)code, (int)input_count, input,
                 output_count, output);
    return lx_pm_print_escape(dc, code, input_count, input, output_count, output);
}

LONG DevPostDeviceModes(HAB hab, PVOID data, PSZ driver, PSZ device, PSZ name, ULONG options)
{
    (void)hab;
    (void)device;
    (void)name;
    int printer = lx_printer_index(driver);
    if (printer < 0 || options > DPDM_QUERYJOBPROP)
        return DPDM_ERROR;
    /* The fixed native devices have no private job properties. DRIVDATA's
       public 40-byte prefix is sufficient and can be queried without a UI. */
    if (!data)
        return LX_PRINTER_DRIVER_DATA_SIZE;
    if (lx_guest_load_u32(data) < LX_PRINTER_DRIVER_DATA_SIZE)
        return DPDM_ERROR;
    lx_printer_driver_data(data, (unsigned)printer);
    return DEV_OK;
}
