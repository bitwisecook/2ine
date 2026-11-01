/* Public domain. No rights reserved. Wholly AI-authored. */
/* Public outline families are explicit substitutions backed by measured host faces. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/graphics.h"
#include "hal/font.h"

static const char *families[] = {"Helv", "Tms Rmn", "Courier"};
static const char *styles[] = {"", " Bold", " Italic", " Bold Italic"};

static int font_metrics(unsigned index, LxHostFont *font, FONTMETRICS *output)
{
    LxHostFontMetrics m;
    if (index >= 12 || !lx_host_font_metrics(font, &m))
        return 0;
    memset(output, 0, sizeof *output);
    snprintf(output->szFamilyname, FACESIZE, "%s", families[index / 4]);
    snprintf(output->szFacename, FACESIZE, "%s%s", families[index / 4], styles[index % 4]);
    output->usCodePage = 437;
    output->lEmHeight = m.em;
    output->lXHeight = m.x_height;
    output->lMaxAscender = m.ascent;
    output->lMaxDescender = m.descent;
    output->lLowerCaseAscent = m.ascent;
    output->lLowerCaseDescent = m.descent;
    output->lInternalLeading = m.ascent + m.descent > m.em ? m.ascent + m.descent - m.em : 0;
    output->lExternalLeading = m.leading;
    output->lAveCharWidth = m.average_width;
    output->lMaxCharInc = m.maximum_width;
    output->lEmInc = m.em;
    output->lMaxBaselineExt = m.ascent + m.descent;
    output->usWeightClass = m.bold ? 7 : 5;
    output->usWidthClass = 5;
    output->sXDeviceRes = output->sYDeviceRes = 96;
    output->sLastChar = 255;
    output->sDefaultChar = '?';
    output->sBreakChar = ' ';
    output->sNominalPointSize = (SHORT)(m.em * 720 / 96);
    output->sMinimumPointSize = 10;
    output->sMaximumPointSize = 3840;
    output->fsType = m.fixed ? 1 : 0; /* FM_TYPE_FIXED */
    output->fsDefn = 1;               /* FM_DEFN_OUTLINE */
    output->fsSelection = (m.bold ? 0x20 : 0) | (m.italic ? 1 : 0);
    output->lUnderscoreSize = m.underline_size;
    output->lUnderscorePosition = m.underline_position;
    output->lStrikeoutSize = m.strikeout_size;
    output->lStrikeoutPosition = m.strikeout_position;
    output->lMatch = (LONG)index + 1;
    return 1;
}
LONG GpiQueryFonts(HPS hps, ULONG options, PSZ face, PLONG requested, LONG record_size,
                   PFONTMETRICS output)
{
    TRACE_NATIVE("GpiQueryFonts(%u, %u, '%s', %p, %d, %p)", (uint)hps, (uint)options, face,
                 requested, (int)record_size, output);
    if (!lx_pm_valid_ps(hps) || !requested || *requested < 0 || record_size < 0 ||
        (options & ~15u) || (*requested && (!record_size || !output)) ||
        (uint64)(uint32)*requested * (uint32)record_size > SIZE_MAX)
        return -255;
    LONG found = 0, copied = 0, capacity = *requested;
    if ((options & 1) && !(options & 8)) /* public device fonts */
        for (unsigned index = 0; index < lx_host_font_count(); index++)
        {
            LxHostFont *font = lx_host_font_open(index, 16);
            FONTMETRICS metrics;
            LxHostFontMetrics host;
            int valid =
                font && font_metrics(index, font, &metrics) && lx_host_font_metrics(font, &host);
            lx_host_font_close(font);
            if (!valid ||
                (face && strcmp(face, metrics.szFamilyname) && strcmp(face, metrics.szFacename) &&
                 strcmp(face, host.family) && strcmp(face, host.face)))
                continue;
            found++;
            if (copied < capacity)
            {
                uint8 *record = (uint8 *)output + (size_t)copied * record_size;
                size_t bytes =
                    (size_t)record_size < sizeof metrics ? (size_t)record_size : sizeof metrics;
                memcpy(record, &metrics, bytes);
                copied++;
            }
        }
    *requested = copied;
    return found - copied;
}
BOOL GpiQueryFontMetrics(HPS hps, LONG record_size, PFONTMETRICS output)
{
    if (record_size < 0 || (record_size && !output))
        return FALSE;
    FONTMETRICS metrics;
    LxHostFont *font = lx_pm_font(hps);
    if (!font_metrics(lx_pm_font_index(hps), font, &metrics))
        return FALSE;
    size_t bytes = (size_t)record_size < sizeof metrics ? (size_t)record_size : sizeof metrics;
    if (bytes)
        memcpy(output, &metrics, bytes);
    return TRUE;
}

static uint32 text_codepoint(uint8 character)
{
    static const uint16 extended[128] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8,
        0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2,
        0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 0x00e1,
        0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd,
        0x00bc, 0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562,
        0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, 0x2514, 0x2534,
        0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560,
        0x2550, 0x256c, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
        0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x00df, 0x0393,
        0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6,
        0x03b5, 0x2229, 0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, 0x00b0,
        0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
    };
    return character < 128 ? character : extended[character - 128];
}
BOOL GpiQueryTextBox(HPS hps, LONG count, PCH text, LONG points, PPOINTL output)
{
    TRACE_NATIVE("GpiQueryTextBox(%u, %d, %p, %d, %p)", (uint)hps, (int)count, text, (int)points,
                 output);
    if (count < 0 || count > 512 || (count && !text) || points < 0 || (points && !output))
        return FALSE;
    LxHostFont *font = lx_pm_font(hps);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
        return FALSE;
    LONG width = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint32 codepoint = text_codepoint((uint8)text[i]);
        int advance = lx_host_font_advance(font, codepoint);
        if (advance < 0)
            return FALSE;
        width += advance;
    }
    POINTL box[5] = {{0, metrics.ascent},
                     {0, -metrics.descent},
                     {width, metrics.ascent},
                     {width, -metrics.descent},
                     {width, 0}};
    LONG copied = points < 5 ? points : 5;
    if (copied)
        memcpy(output, box, (size_t)copied * sizeof *box);
    return TRUE;
}
