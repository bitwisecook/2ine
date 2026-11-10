/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows font enumeration records contain measured faces and temporary guest callback data. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmgrep.h"
#include "host/font.h"
#include "os2/ps/common/pm_text.h"

enum
{
    /* Guests write Windows Latin 1 (OS/2 code page 1004); the PM font tables use 437. */
    GUEST_CODEPAGE = 1004,
    FONT_CODEPAGE = 437,
    CHAR_CODE_MAX = 255,
    CHAR_CODES = 256,
    LONG_BYTES = 4,
    /* Every font reports a 96-dpi digitized aspect, as on a VGA screen. */
    DIGITIZED_ASPECT = 96,
    /* Host fonts are numbered family * 4 + style; family 1 is the Times face. */
    HOST_FONT_STYLES = 4,
    HOST_FONT_FAMILY_TIMES = 1,
    /* The callback page holds ENUMLOGFONT at its start and NEWTEXTMETRIC from here. */
    ENUM_PAGE_METRICS = 256,
    /* PMWINX's OUTLINETEXTMETRIC is a fixed record, the names following the structure. */
    OUTLINE_RECORD_SIZE = 0x116,
    OUTLINE_BUFFER_SIZE = 512,
    OUTLINE_NAMES = 4
};

LONG GetDialogBaseUnits(void)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    ULONG dc = GetDC(0);
    uint8 metrics[WIN32_TEXTMETRIC_SIZE], extent[WIN32_SIZE_SIZE];
    LONG result = 0;
    if (dc && GetTextMetrics(dc, metrics) &&
        GetTextExtentPoint(dc, (PCHAR)alphabet, sizeof alphabet - 1, extent))
    {
        /* The average width of the 52 letters, rounded: (width / 26 + 1) / 2. */
        ULONG width = (lx_guest_load_u32(extent + WIN32_SIZE_CX) / 26 + 1) / 2;
        ULONG height = lx_guest_load_u32(metrics + WIN32_TEXTMETRIC_HEIGHT);
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
    memset(output, 0, WIN32_NEWTEXTMETRIC_SIZE);
    /* TEXTMETRIC's LONGs, tmHeight to tmDigitizedAspectY. */
    LONG fields[] = {fm->lMaxAscender + fm->lMaxDescender,
                     fm->lMaxAscender,
                     fm->lMaxDescender,
                     fm->lInternalLeading,
                     fm->lExternalLeading,
                     fm->lAveCharWidth,
                     fm->lMaxCharInc,
                     host->bold ? WIN32_FW_BOLD : WIN32_FW_NORMAL,
                     0,
                     DIGITIZED_ASPECT,
                     DIGITIZED_ASPECT};
    for (unsigned i = 0; i < 11; i++)
        lx_guest_store_u32(output + WIN32_TEXTMETRIC_HEIGHT + i * LONG_BYTES, (ULONG)fields[i]);
    output[WIN32_TEXTMETRIC_FIRST_CHAR] = 0;
    output[WIN32_TEXTMETRIC_LAST_CHAR] = CHAR_CODE_MAX;
    output[WIN32_TEXTMETRIC_DEFAULT_CHAR] = '?';
    output[WIN32_TEXTMETRIC_BREAK_CHAR] = ' ';
    output[WIN32_TEXTMETRIC_ITALIC] = host->italic;
    output[WIN32_TEXTMETRIC_UNDERLINED] = logfont ? logfont[WIN32_LOGFONT_UNDERLINE] : 0;
    output[WIN32_TEXTMETRIC_STRUCK_OUT] = logfont ? logfont[WIN32_LOGFONT_STRIKEOUT] : 0;
    /* TMPF_FIXED_PITCH is set for variable-pitch fonts, as Windows defines it. */
    output[WIN32_TEXTMETRIC_PITCH_AND_FAMILY] = (host->fixed ? 0 : WIN32_TMPF_FIXED_PITCH) |
                                                WIN32_TMPF_VECTOR |
                                                (host->sfnt ? WIN32_TMPF_TRUETYPE : 0);
    lx_guest_store_u32(output + WIN32_NEWTEXTMETRIC_FLAGS,
                       (host->bold ? WIN32_NTM_BOLD : 0) | (host->italic ? WIN32_NTM_ITALIC : 0) |
                           (!host->bold && !host->italic ? WIN32_NTM_REGULAR : 0));
    lx_guest_store_u32(output + WIN32_NEWTEXTMETRIC_SIZE_EM, (ULONG)fm->lEmHeight);
    lx_guest_store_u32(output + WIN32_NEWTEXTMETRIC_CELL_HEIGHT, (ULONG)fields[0]);
    lx_guest_store_u32(output + WIN32_NEWTEXTMETRIC_AVERAGE_WIDTH, (ULONG)fm->lAveCharWidth);
}

BOOL GetTextMetrics(ULONG handle, PVOID output)
{
    Open32DC *dc = open32FindDC(handle);
    FONTMETRICS fm;
    LxHostFontMetrics host;
    if (!dc || !output || !GpiQueryFontMetrics(handle, sizeof fm, &fm) ||
        !lx_host_font_metrics(lx_pm_font(handle), &host))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8 record[WIN32_NEWTEXTMETRIC_SIZE], logfont[WIN32_LOGFONT_SIZE];
    BOOL known = open32FontObject(dc->font_handle, sizeof logfont, logfont) == WIN32_LOGFONT_SIZE;
    encode_metrics(&fm, &host, known ? logfont : NULL, record);
    memcpy(output, record, WIN32_TEXTMETRIC_SIZE);
    return TRUE;
}

static LONG enumerate_fonts(ULONG handle, PSZ family, ULONG procedure, ULONG parameter, LONG result)
{
    if (!open32FindDC(handle) ||
        !lx_execution_memory(GProcessState.execution, procedure, 1, LX_GUEST_EXECUTE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    PSZ wanted = family && *family ? family : NULL;
    LONG count = 0;
    LONG total = GpiQueryFonts(handle, QF_PUBLIC, wanted, &count, sizeof(FONTMETRICS), NULL);
    if (total < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    FONTMETRICS *fonts = calloc(total ? (size_t)total : 1, sizeof *fonts);
    LxGuestMapping *mapping = fonts ? lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                                       LX_GUEST_READ | LX_GUEST_WRITE, 0)
                                    : NULL;
    if (!mapping)
    {
        free(fonts);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    count = total;
    if (count && GpiQueryFonts(handle, QF_PUBLIC, wanted, &count, sizeof *fonts, fonts) < 0)
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        free(fonts);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint8 *record = lx_execution_mapping_host(mapping);
    uint32 address = lx_execution_mapping_address(mapping);
    for (LONG i = 0; i < count; i++)
    {
        FONTMETRICS *fm = &fonts[i];
        /* Without a family, each face is enumerated once, by its regular style. */
        if ((!family || !*family) && (fm->fsSelection & (FM_SEL_ITALIC | FM_SEL_BOLD)))
            continue;
        unsigned index = fm->lMatch > 0 ? (unsigned)fm->lMatch - 1 : 0;
        LxHostFont *font = lx_host_font_open(index, (unsigned)fm->lEmHeight);
        LxHostFontMetrics metrics;
        BOOL valid = font && lx_host_font_metrics(font, &metrics);
        lx_host_font_close(font);
        if (!valid)
            continue;
        memset(record, 0, WIN32_ENUMLOGFONT_SIZE);
        lx_guest_store_u32(record + WIN32_LOGFONT_HEIGHT, (ULONG)-fm->lEmHeight);
        lx_guest_store_u32(record + WIN32_LOGFONT_WEIGHT,
                           metrics.bold ? WIN32_FW_BOLD : WIN32_FW_NORMAL);
        record[WIN32_LOGFONT_ITALIC] = metrics.italic;
        record[WIN32_LOGFONT_PITCH_AND_FAMILY] =
            metrics.fixed ? WIN32_FIXED_PITCH | WIN32_FF_MODERN
            : index / HOST_FONT_STYLES == HOST_FONT_FAMILY_TIMES
                ? WIN32_VARIABLE_PITCH | WIN32_FF_ROMAN
                : WIN32_VARIABLE_PITCH | WIN32_FF_SWISS;
        memcpy(record + WIN32_LOGFONT_FACE_NAME, fm->szFamilyname, WIN32_LF_FACESIZE);
        memcpy(record + WIN32_ENUMLOGFONT_FULL_NAME, fm->szFacename, WIN32_LF_FACESIZE);
        const char *style = metrics.bold ? (metrics.italic ? "Bold Italic" : "Bold")
                                         : (metrics.italic ? "Italic" : "Regular");
        strcpy((char *)record + WIN32_ENUMLOGFONT_STYLE, style);
        encode_metrics(fm, &metrics, record, record + ENUM_PAGE_METRICS);
        result = (LONG)lx_execution_callback32_4(
            GProcessState.execution, procedure, address, address + ENUM_PAGE_METRICS,
            metrics.sfnt ? WIN32_TRUETYPE_FONTTYPE : WIN32_DEVICE_FONTTYPE, parameter);
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
        open32SetError(ERROR_INVALID_PARAMETER);
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
    return lx_pm_codepoint(WinCpTranslateChar(0, GUEST_CODEPAGE, (uint8)c, FONT_CODEPAGE));
}

BOOL GetCharWidth(ULONG h, ULONG first, ULONG last, PVOID output)
{
    if (!open32FindDC(h) || !output || first > last || last > CHAR_CODE_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LxHostFont *font = lx_pm_font(h);
    for (ULONG c = first; c <= last; c++)
    {
        LONG width = lx_host_font_advance(font, metric_codepoint(c));
        if (width < 0)
            return FALSE;
        lx_guest_store_u32((uint8 *)output + (c - first) * LONG_BYTES, (ULONG)width);
    }
    return TRUE;
}

BOOL GetCharABCWidths(ULONG h, ULONG first, ULONG last, PVOID output)
{
    if (!open32FindDC(h) || !output || first > last || last > CHAR_CODE_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* PMWINX reports the whole advance as abcB, for every font technology. */
    LxHostFont *font = lx_pm_font(h);
    for (ULONG c = first; c <= last; c++)
    {
        LONG width = lx_host_font_advance(font, metric_codepoint(c));
        if (width < 0)
            return FALSE;
        uint8 *p = (uint8 *)output + (c - first) * WIN32_ABC_SIZE;
        lx_guest_store_u32(p + WIN32_ABC_A, 0);
        lx_guest_store_u32(p + WIN32_ABC_B, (ULONG)width);
        lx_guest_store_u32(p + WIN32_ABC_C, 0);
    }
    return TRUE;
}

ULONG GetKerningPairs(ULONG h, ULONG capacity, PVOID output)
{
    if (!open32FindDC(h))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LxHostFont *font = lx_pm_font(h);
    ULONG count = 0;
    for (ULONG first = 0; first < CHAR_CODES; first++)
        for (ULONG second = 0; second < CHAR_CODES; second++)
        {
            LONG value =
                lx_host_font_kerning(font, metric_codepoint(first), metric_codepoint(second));
            if (!value)
                continue;
            if (output && count < capacity)
            {
                uint8 *p = (uint8 *)output + count * WIN32_KERNINGPAIR_SIZE;
                lx_guest_store_u16(p + WIN32_KERNINGPAIR_FIRST, (uint16)first);
                lx_guest_store_u16(p + WIN32_KERNINGPAIR_SECOND, (uint16)second);
                lx_guest_store_u32(p + WIN32_KERNINGPAIR_AMOUNT, (ULONG)value);
            }
            count++;
        }
    return output && count > capacity ? capacity : count;
}

ULONG GetOutlineTextMetrics(ULONG h, ULONG capacity, PVOID output)
{
    LxHostFontMetrics metrics;
    uint8 tm[WIN32_TEXTMETRIC_SIZE];
    if (!open32FindDC(h) || !lx_host_font_metrics(lx_pm_font(h), &metrics) ||
        !GetTextMetrics(h, tm))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!capacity)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* PMWINX stores family and face, then an empty style and full name, in a fixed 0x116 record. */
    const char *names[] = {metrics.family, metrics.face, "", ""};
    ULONG needed = OUTLINE_RECORD_SIZE;
    if (!output)
        return needed;
    uint8 record[OUTLINE_BUFFER_SIZE] = {0};
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_SIZE_FIELD, needed);
    memcpy(record + WIN32_OUTLINETEXTMETRIC_TEXT_METRICS, tm, WIN32_TEXTMETRIC_SIZE);
    static const uint8 no_panose[WIN32_PANOSE_SIZE];
    if (memcmp(metrics.panose, no_panose, sizeof no_panose))
        memcpy(record + WIN32_OUTLINETEXTMETRIC_PANOSE, metrics.panose, sizeof metrics.panose);
    else
    {
        /* Fonts without an OS/2 table are classified from their measured style. */
        record[WIN32_OUTLINETEXTMETRIC_PANOSE + WIN32_PANOSE_FAMILY_TYPE] =
            WIN32_PAN_FAMILY_TEXT_DISPLAY;
        record[WIN32_OUTLINETEXTMETRIC_PANOSE + WIN32_PANOSE_WEIGHT] =
            metrics.bold ? WIN32_PAN_WEIGHT_BOLD : WIN32_PAN_WEIGHT_BOOK;
        record[WIN32_OUTLINETEXTMETRIC_PANOSE + WIN32_PANOSE_PROPORTION] =
            metrics.fixed ? WIN32_PAN_PROP_MONOSPACED : WIN32_PAN_PROP_MODERN;
    }
    /* otmfsSelection has the NEWTEXTMETRIC ntmFlags bit layout. */
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_SELECTION,
                       (metrics.italic ? WIN32_NTM_ITALIC : 0) |
                           (metrics.bold ? WIN32_NTM_BOLD : 0) |
                           (!metrics.bold && !metrics.italic ? WIN32_NTM_REGULAR : 0));
    /* An upright slope: rise 1, run 0. */
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_SLOPE_RISE, 1);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_SLOPE_RUN, 0);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_EM_SQUARE, (ULONG)metrics.em);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_ASCENT, (ULONG)metrics.ascent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_DESCENT, (ULONG)-metrics.descent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_LINE_GAP, (ULONG)metrics.leading);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_CAP_EM_HEIGHT,
                       (ULONG)metrics.lowercase_ascent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_X_HEIGHT, (ULONG)metrics.x_height);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_FONT_BOX + WIN32_RECT_TOP,
                       (ULONG)metrics.ascent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_FONT_BOX + WIN32_RECT_RIGHT,
                       (ULONG)metrics.maximum_width);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_FONT_BOX + WIN32_RECT_BOTTOM,
                       (ULONG)-metrics.descent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_MAC_ASCENT, (ULONG)metrics.ascent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_MAC_DESCENT, (ULONG)-metrics.descent);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_MAC_LINE_GAP, (ULONG)metrics.leading);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_MINIMUM_PPEM, 1);
    const LONG scripts[] = {metrics.subscript_x_size,     metrics.subscript_y_size,
                            metrics.subscript_x_offset,   metrics.subscript_y_offset,
                            metrics.superscript_x_size,   metrics.superscript_y_size,
                            metrics.superscript_x_offset, metrics.superscript_y_offset};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_SUBSCRIPT_SIZE + i * LONG_BYTES,
                           (ULONG)scripts[i]);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_STRIKEOUT_SIZE,
                       (ULONG)metrics.strikeout_size);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_STRIKEOUT_POSITION,
                       (ULONG)metrics.strikeout_position);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_UNDERSCORE_SIZE,
                       (ULONG)metrics.underline_size);
    lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_UNDERSCORE_POSITION,
                       (ULONG)metrics.underline_position);
    ULONG offset = WIN32_OUTLINETEXTMETRIC_SIZE;
    for (unsigned i = 0; i < OUTLINE_NAMES; i++)
    {
        lx_guest_store_u32(record + WIN32_OUTLINETEXTMETRIC_FAMILY_NAME + i * LONG_BYTES, offset);
        size_t n = strlen(names[i]) + 1;
        memcpy(record + offset, names[i], n);
        offset += (ULONG)n;
    }
    memcpy(output, record, capacity < needed ? capacity : needed);
    return 1;
}
