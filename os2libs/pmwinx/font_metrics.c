/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows font enumeration records contain measured faces and temporary guest callback data. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/graphics.h"
#include "hal/font.h"
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
LONG EnumFontFamilies(ULONG handle, PSZ family, ULONG procedure, ULONG parameter)
{
    if (!open32FindDC(handle) ||
        !lx_execution_memory(GLoaderState.execution, procedure, 1, LX_GUEST_EXECUTE))
    {
        open32SetError(87);
        return 0;
    }
    FONTMETRICS fonts[12];
    LONG count = 12;
    if (GpiQueryFonts(handle, 1, family && *family ? family : NULL, &count, sizeof fonts[0],
                      fonts) < 0)
    {
        open32SetError(87);
        return 0;
    }
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!mapping)
    {
        open32SetError(8);
        return 0;
    }
    uint8 *record = lx_execution_mapping_host(mapping);
    uint32 address = lx_execution_mapping_address(mapping);
    LONG result = 0;
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
        result = (LONG)lx_execution_callback32_4(GLoaderState.execution, procedure, address,
                                                 address + 256, metrics.sfnt ? 4 : 0, parameter);
        if (!result)
            break;
    }
    lx_execution_unmap(GLoaderState.execution, mapping);
    return result;
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
