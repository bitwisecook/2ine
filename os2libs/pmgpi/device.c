/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window DC capabilities describe the PM renderer and its logical 96 dpi display. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "hal/font.h"

BOOL DevQueryCaps(HDC dc, LONG start, LONG count, PLONG output)
{
    TRACE_NATIVE("DevQueryCaps(%u, %d, %d, %p)", (uint)dc, (int)start, (int)count, output);
    RECTL rect;
    if (start < 0 || count < 0 || start > 42 || count > 42 - start || (count && !output) ||
        !WinQueryWindowRect(dc, &rect))
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
