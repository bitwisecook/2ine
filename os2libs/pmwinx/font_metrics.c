/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows font enumeration records contain measured faces and temporary guest callback data. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/graphics.h"
#include "host/font.h"
#include "os2libs/pm_text.h"

LONG GetDialogBaseUnits(void)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    ULONG dc = GetDC(0);
    uint8 metrics[56], extent[8];
    LONG result = 0;
    if (dc && GetTextMetrics(dc, metrics) &&
        GetTextExtentPoint(dc, (PCHAR)alphabet, sizeof alphabet - 1, extent))
    {
        ULONG width = (lx_guest_load_u32(extent) / 26 + 1) / 2;
        ULONG height = lx_guest_load_u32(metrics);
        if (width && width <= UINT16_MAX && height && height <= UINT16_MAX)
            result = (LONG)(width | (height << 16));
    }
    if (dc)
        ReleaseDC(0, dc);
    return result;
}

static void encode_metrics(const FONTMETRICS *fm, const LxHostFontMetrics *host,
                           const uint8 *logfont, uint8 *output)
{
    memset(output, 0, 72);
    LONG fields[] = {fm->lMaxAscender + fm->lMaxDescender,
                     fm->lMaxAscender,
                     fm->lMaxDescender,
                     fm->lInternalLeading,
                     fm->lExternalLeading,
                     fm->lAveCharWidth,
                     fm->lMaxCharInc,
                     host->bold ? 700 : 400,
                     0,
                     96,
                     96};
    for (unsigned i = 0; i < 11; i++)
        lx_guest_store_u32(output + i * 4, (ULONG)fields[i]);
    output[44] = 0;
    output[45] = 255;
    output[46] = '?';
    output[47] = ' ';
    output[48] = host->italic;
    output[49] = logfont ? logfont[21] : 0;
    output[50] = logfont ? logfont[22] : 0;
    output[51] = (host->fixed ? 0 : 1) | 2 | (host->sfnt ? 4 : 0);
    lx_guest_store_u32(output + 56, (host->bold ? 0x20 : 0) | (host->italic ? 1 : 0) |
                                        (!host->bold && !host->italic ? 0x40 : 0));
    lx_guest_store_u32(output + 60, (ULONG)fm->lEmHeight);
    lx_guest_store_u32(output + 64, (ULONG)fields[0]);
    lx_guest_store_u32(output + 68, (ULONG)fm->lAveCharWidth);
}

BOOL GetTextMetrics(ULONG handle, PVOID output)
{
    Open32DC *dc = open32FindDC(handle);
    FONTMETRICS fm;
    LxHostFontMetrics host;
    if (!dc || !output || !GpiQueryFontMetrics(handle, sizeof fm, &fm) ||
        !lx_host_font_metrics(lx_pm_font(handle), &host))
    {
        open32SetError(87);
        return FALSE;
    }
    uint8 record[72], logfont[60];
    BOOL known = open32FontObject(dc->font_handle, sizeof logfont, logfont) == 60;
    encode_metrics(&fm, &host, known ? logfont : NULL, record);
    memcpy(output, record, 56);
    return TRUE;
}

static LONG enumerate_fonts(ULONG handle, PSZ family, ULONG procedure, ULONG parameter, LONG result)
{
    if (!open32FindDC(handle) ||
        !lx_execution_memory(GProcessState.execution, procedure, 1, LX_GUEST_EXECUTE))
    {
        open32SetError(87);
        return 0;
    }
    PSZ wanted = family && *family ? family : NULL;
    LONG count = 0;
    LONG total = GpiQueryFonts(handle, 1, wanted, &count, sizeof(FONTMETRICS), NULL);
    if (total < 0)
    {
        open32SetError(87);
        return 0;
    }
    FONTMETRICS *fonts = calloc(total ? (size_t)total : 1, sizeof *fonts);
    LxGuestMapping *mapping =
        fonts ? lx_execution_map(GProcessState.execution, 0, 4096, 3, 0) : NULL;
    if (!mapping)
    {
        free(fonts);
        open32SetError(8);
        return 0;
    }
    count = total;
    if (count && GpiQueryFonts(handle, 1, wanted, &count, sizeof *fonts, fonts) < 0)
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        free(fonts);
        open32SetError(87);
        return 0;
    }
    uint8 *record = lx_execution_mapping_host(mapping);
    uint32 address = lx_execution_mapping_address(mapping);
    for (LONG i = 0; i < count; i++)
    {
        FONTMETRICS *fm = &fonts[i];
        if ((!family || !*family) && (fm->fsSelection & 0x21))
            continue;
        unsigned index = fm->lMatch > 0 ? (unsigned)fm->lMatch - 1 : 0;
        LxHostFont *font = lx_host_font_open(index, (unsigned)fm->lEmHeight);
        LxHostFontMetrics metrics;
        BOOL valid = font && lx_host_font_metrics(font, &metrics);
        lx_host_font_close(font);
        if (!valid)
            continue;
        memset(record, 0, 156);
        lx_guest_store_u32(record, (ULONG)-fm->lEmHeight);
        lx_guest_store_u32(record + 16, metrics.bold ? 700 : 400);
        record[20] = metrics.italic;
        record[27] = metrics.fixed ? 0x31 : index / 4 == 1 ? 0x12 : 0x22;
        memcpy(record + 28, fm->szFamilyname, 32);
        memcpy(record + 60, fm->szFacename, 32);
        const char *style = metrics.bold ? (metrics.italic ? "Bold Italic" : "Bold")
                                         : (metrics.italic ? "Italic" : "Regular");
        strcpy((char *)record + 124, style);
        encode_metrics(fm, &metrics, record, record + 256);
        result = (LONG)lx_execution_callback32_4(GProcessState.execution, procedure, address,
                                                 address + 256, metrics.sfnt ? 4 : 2, parameter);
        if (!result)
            break;
    }
    lx_execution_unmap(GProcessState.execution, mapping);
    free(fonts);
    return result;
}

LONG EnumFontFamilies(ULONG handle, PSZ family, ULONG procedure, ULONG parameter)
{
    return enumerate_fonts(handle, family, procedure, parameter, 0);
}

LONG GetTextFace(ULONG handle, LONG capacity, PCHAR output)
{
    FONTMETRICS metrics;
    if (!open32FindDC(handle) || capacity < 0 ||
        !GpiQueryFontMetrics(handle, sizeof metrics, &metrics))
    {
        open32SetError(87);
        return 0;
    }
    size_t length = strnlen(metrics.szFacename, sizeof metrics.szFacename);
    if (!output)
        return (LONG)length + 1;
    if (!capacity)
        return 0;
    if (length >= (size_t)capacity)
        length = (size_t)capacity - 1;
    memcpy(output, metrics.szFacename, length);
    output[length] = 0;
    return (LONG)length;
}

LONG EnumFonts(ULONG h, PSZ face, ULONG procedure, ULONG parameter)
{
    /* An enumeration that finds no font still reports success. */
    return enumerate_fonts(h, face, procedure, parameter, 1);
}

static uint32 metric_codepoint(ULONG c)
{
    return lx_pm_codepoint(WinCpTranslateChar(0, 1004, (uint8)c, 437));
}

BOOL GetCharWidth(ULONG h, ULONG first, ULONG last, PVOID output)
{
    if (!open32FindDC(h) || !output || first > last || last > 255)
    {
        open32SetError(87);
        return FALSE;
    }
    LxHostFont *font = lx_pm_font(h);
    for (ULONG c = first; c <= last; c++)
    {
        LONG width = lx_host_font_advance(font, metric_codepoint(c));
        if (width < 0)
            return FALSE;
        lx_guest_store_u32((uint8 *)output + (c - first) * 4, (ULONG)width);
    }
    return TRUE;
}

BOOL GetCharABCWidths(ULONG h, ULONG first, ULONG last, PVOID output)
{
    if (!open32FindDC(h) || !output || first > last || last > 255)
    {
        open32SetError(87);
        return FALSE;
    }
    /* PMWINX reports the whole advance as abcB, for every font technology. */
    LxHostFont *font = lx_pm_font(h);
    for (ULONG c = first; c <= last; c++)
    {
        LONG width = lx_host_font_advance(font, metric_codepoint(c));
        if (width < 0)
            return FALSE;
        uint8 *p = (uint8 *)output + (c - first) * 12;
        lx_guest_store_u32(p, 0);
        lx_guest_store_u32(p + 4, (ULONG)width);
        lx_guest_store_u32(p + 8, 0);
    }
    return TRUE;
}

ULONG GetKerningPairs(ULONG h, ULONG capacity, PVOID output)
{
    if (!open32FindDC(h))
    {
        open32SetError(87);
        return 0;
    }
    LxHostFont *font = lx_pm_font(h);
    ULONG count = 0;
    for (ULONG first = 0; first < 256; first++)
        for (ULONG second = 0; second < 256; second++)
        {
            LONG value =
                lx_host_font_kerning(font, metric_codepoint(first), metric_codepoint(second));
            if (!value)
                continue;
            if (output && count < capacity)
            {
                uint8 *p = (uint8 *)output + count * 8;
                lx_guest_store_u16(p, (uint16)first);
                lx_guest_store_u16(p + 2, (uint16)second);
                lx_guest_store_u32(p + 4, (ULONG)value);
            }
            count++;
        }
    return output && count > capacity ? capacity : count;
}

ULONG GetOutlineTextMetrics(ULONG h, ULONG capacity, PVOID output)
{
    LxHostFontMetrics metrics;
    uint8 tm[56];
    if (!open32FindDC(h) || !lx_host_font_metrics(lx_pm_font(h), &metrics) ||
        !GetTextMetrics(h, tm))
    {
        open32SetError(6);
        return 0;
    }
    if (!capacity)
    {
        open32SetError(87);
        return 0;
    }
    /* PMWINX stores family and face, then an empty style and full name, in a fixed 0x116 record. */
    const char *names[] = {metrics.family, metrics.face, "", ""};
    ULONG needed = 0x116;
    if (!output)
        return needed;
    uint8 record[512] = {0};
    lx_guest_store_u32(record, needed);
    memcpy(record + 4, tm, 56);
    static const uint8 no_panose[10];
    if (memcmp(metrics.panose, no_panose, sizeof no_panose))
        memcpy(record + 61, metrics.panose, sizeof metrics.panose);
    else
    {
        /* Fonts without an OS/2 table are classified from their measured style. */
        record[61] = 2;
        record[63] = metrics.bold ? 8 : 5;
        record[64] = metrics.fixed ? 9 : 3;
    }
    lx_guest_store_u32(record + 72, (metrics.italic ? 1 : 0) | (metrics.bold ? 0x20 : 0) |
                                        (!metrics.bold && !metrics.italic ? 0x40 : 0));
    lx_guest_store_u32(record + 80, 1);
    lx_guest_store_u32(record + 84, 0);
    lx_guest_store_u32(record + 92, (ULONG)metrics.em);
    lx_guest_store_u32(record + 96, (ULONG)metrics.ascent);
    lx_guest_store_u32(record + 100, (ULONG)-metrics.descent);
    lx_guest_store_u32(record + 104, (ULONG)metrics.leading);
    lx_guest_store_u32(record + 108, (ULONG)metrics.lowercase_ascent);
    lx_guest_store_u32(record + 112, (ULONG)metrics.x_height);
    lx_guest_store_u32(record + 120, (ULONG)metrics.ascent);
    lx_guest_store_u32(record + 124, (ULONG)metrics.maximum_width);
    lx_guest_store_u32(record + 128, (ULONG)-metrics.descent);
    lx_guest_store_u32(record + 132, (ULONG)metrics.ascent);
    lx_guest_store_u32(record + 136, (ULONG)-metrics.descent);
    lx_guest_store_u32(record + 140, (ULONG)metrics.leading);
    lx_guest_store_u32(record + 144, 1);
    const LONG scripts[] = {metrics.subscript_x_size,     metrics.subscript_y_size,
                            metrics.subscript_x_offset,   metrics.subscript_y_offset,
                            metrics.superscript_x_size,   metrics.superscript_y_size,
                            metrics.superscript_x_offset, metrics.superscript_y_offset};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(record + 148 + i * 4, (ULONG)scripts[i]);
    lx_guest_store_u32(record + 180, (ULONG)metrics.strikeout_size);
    lx_guest_store_u32(record + 184, (ULONG)metrics.strikeout_position);
    lx_guest_store_u32(record + 188, (ULONG)metrics.underline_size);
    lx_guest_store_u32(record + 192, (ULONG)metrics.underline_position);
    ULONG offset = 212;
    for (unsigned i = 0; i < 4; i++)
    {
        lx_guest_store_u32(record + 196 + i * 4, offset);
        size_t n = strlen(names[i]) + 1;
        memcpy(record + offset, names[i], n);
        offset += (ULONG)n;
    }
    memcpy(output, record, capacity < needed ? capacity : needed);
    return 1;
}
