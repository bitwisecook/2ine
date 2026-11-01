/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window DC capabilities describe the PM renderer and its logical 96 dpi display. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "hal/font.h"
#include "os2libs/pmwin/graphics.h"

HDC DevOpenDC(HAB hab, LONG type, PSZ token, LONG count, PVOID data, HDC compatible)
{
    TRACE_NATIVE("DevOpenDC(%u, %d, '%s', %d, %p, %u)", (uint)hab, (int)type, token, (int)count,
                 data, (uint)compatible);
    if (count < 0 || count > 9 || (count && !data) ||
        (token && strcmp(token, "*") && strcasecmp(token, "DISPLAY")))
        return 0;
    PSZ *fields = data;
    if (count > 1 && fields[1] && strcasecmp(fields[1], "DISPLAY"))
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
    if (start < 0 || count < 0 || start > 42 || count > 42 - start || (count && !output) ||
        !lx_pm_dc_valid(dc))
        return FALSE;
    LONG caps[42] = {0};
    LxHostFont *font = lx_host_font_open(0, 16);
    LxHostFontMetrics m;
    if (!lx_host_font_metrics(font, &m))
    {
        lx_host_font_close(font);
        return FALSE;
    }
    lx_host_font_close(font);
    LONG width = WinQuerySysValue(HWND_DESKTOP, 20), height = WinQuerySysValue(HWND_DESKTOP, 21);
    caps[1] = 2; /* CAPS_IO_SUPPORTS_OP */
    caps[2] = 2; /* CAPS_TECH_RASTER_DISPLAY */
    caps[4] = width;
    caps[5] = height;
    caps[6] = m.average_width ? width / m.average_width : 0;
    caps[7] = m.ascent + m.descent ? height / (m.ascent + m.descent) : 0;
    caps[8] = caps[9] = 3780; /* pels per metre at logical 96 dpi */
    caps[10] = caps[12] = caps[35] = m.average_width;
    caps[11] = caps[13] = caps[36] = m.ascent + m.descent;
    caps[14] = 16; /* indexed drawing palette */
    caps[15] = 1;  /* planes */
    caps[16] = 24; /* RGB components in the host texture */
    caps[18] = 3;  /* pointer buttons */
    caps[19] = 2;  /* CAPS_FM_OVERPAINT */
    caps[22] = 1;  /* byte alignment */
    caps[27] = (LONG)lx_host_font_count();
    caps[31] = 1;             /* CAPS_DEV_WINDOWING_SUPPORT */
    caps[33] = 16777216;      /* physical RGB colors */
    caps[34] = 15;            /* highest indexed color */
    caps[37] = caps[38] = 96; /* logical font dpi */
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
    if (strcasecmp(driver, "DISPLAY"))
        return FALSE;
    if (device_capacity && (!names || !descriptions))
        return FALSE;
    if (type_capacity && !data_types)
        return FALSE;
    if (device_capacity)
    {
        memset(names, 0, 32);
        memset(descriptions, 0, 64);
        memcpy(names, "DISPLAY", 8);
        memcpy(descriptions, "2ine display", 13);
    }
    if (type_capacity)
    {
        memset(data_types, 0, 16);
        memcpy(data_types, "PM_Q_STD", 9);
    }
    *devices = *types = 1;
    return TRUE;
}

LONG DevQueryHardcopyCaps(HDC dc, LONG first, LONG count, PVOID output)
{
    TRACE_NATIVE("DevQueryHardcopyCaps(%u, %d, %d, %p)", (uint)dc, (int)first, (int)count, output);
    if (!lx_pm_dc_valid(dc) || first < 0 || count < 0 || (count && !output))
        return -1;
    /* Display and display-compatible memory devices have no hardcopy forms. */
    return 0;
}
