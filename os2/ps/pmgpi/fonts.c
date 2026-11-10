/* Public domain. No rights reserved. Wholly AI-authored. */
/* Public outline families are explicit substitutions backed by measured host faces. */
#include "os2/include/private/export.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmgrep.h"
#include "host/font.h"
#include "os2/include/private/pmwinp.h"
#include "os2/ps/common/pm_text.h"
#include "os2/ps/pmgpi/internal.h"

/* Host font indexes below BUILTIN_FACES are the public families in each style: index / 4 is the
   family and the low bits select bold and italic. Later indexes are loaded host faces. */
static const char *families[] = {"Helv", "Tms Rmn", "Courier"};
static const char *styles[] = {"", " Bold", " Italic", " Bold Italic"};

enum
{
    STYLE_BOLD = 1,
    STYLE_ITALIC = 2,
    FONT_STYLES = 4,
    BUILTIN_FACES = 12,
    DECIPOINTS_PER_INCH = 720,
    /* The largest lMaxBaselineExt, in pels, honoured as the font size. */
    MAX_FONT_PIXELS = 512,
    /* GpiQuerySetIds returns STR8 names. */
    SETID_NAME_SIZE = 8
};

static int font_metrics(unsigned index, LxHostFont *font, unsigned dpi, FONTMETRICS *output)
{
    LxHostFontMetrics m;
    if (!lx_host_font_metrics(font, &m))
        return 0;
    const unsigned bold = index < BUILTIN_FACES ? index & STYLE_BOLD : m.bold != 0;
    const unsigned italic = index < BUILTIN_FACES ? (index & STYLE_ITALIC) != 0 : m.italic != 0;
    memset(output, 0, sizeof *output);
    if (index < BUILTIN_FACES)
    {
        snprintf(output->szFamilyname, FACESIZE, "%s", families[index / FONT_STYLES]);
        snprintf(output->szFacename, FACESIZE, "%s%s", families[index / FONT_STYLES],
                 styles[index % FONT_STYLES]);
    }
    else
    {
        snprintf(output->szFamilyname, FACESIZE, "%s", m.family);
        snprintf(output->szFacename, FACESIZE, "%s", m.face);
    }
    output->usCodePage = 437;
    output->lEmHeight = m.em;
    output->lXHeight = m.x_height;
    output->lMaxAscender = m.ascent;
    output->lMaxDescender = m.descent;
    output->lLowerCaseAscent = m.lowercase_ascent;
    output->lLowerCaseDescent = m.lowercase_descent;
    output->lInternalLeading = m.ascent + m.descent > m.em ? m.ascent + m.descent - m.em : 0;
    output->lExternalLeading = m.leading;
    output->lAveCharWidth = m.average_width;
    output->lMaxCharInc = m.maximum_width;
    output->lEmInc = m.em;
    output->lMaxBaselineExt = m.ascent + m.descent;
    output->usWeightClass = bold ? FWEIGHT_BOLD : FWEIGHT_NORMAL;
    output->usWidthClass = FWIDTH_NORMAL;
    output->sXDeviceRes = output->sYDeviceRes = (SHORT)dpi;
    output->sLastChar = 255;
    output->sDefaultChar = '?';
    output->sBreakChar = ' ';
    output->sNominalPointSize = (SHORT)(m.em * DECIPOINTS_PER_INCH / (int)dpi);
    output->sMinimumPointSize = 10;
    output->sMaximumPointSize = 3840;
    output->fsType = m.fixed ? FM_TYPE_FIXED : 0;
    output->fsDefn = FM_DEFN_OUTLINE;
    output->fsSelection = (bold ? FM_SEL_BOLD : 0) | (italic ? FM_SEL_ITALIC : 0);
    output->lUnderscoreSize = m.underline_size;
    output->lUnderscorePosition = m.underline_position;
    output->lStrikeoutSize = m.strikeout_size;
    output->lStrikeoutPosition = m.strikeout_position;
    output->lSubscriptXSize = m.subscript_x_size;
    output->lSubscriptYSize = m.subscript_y_size;
    output->lSubscriptXOffset = m.subscript_x_offset;
    output->lSubscriptYOffset = m.subscript_y_offset;
    output->lSuperscriptXSize = m.superscript_x_size;
    output->lSuperscriptYSize = m.superscript_y_size;
    output->lSuperscriptXOffset = m.superscript_x_offset;
    output->lSuperscriptYOffset = m.superscript_y_offset;
    output->lMatch = (LONG)index + 1;
    return 1;
}

LONG GpiQueryFonts(HPS hps, ULONG options, PSZ face, PLONG requested, LONG record_size,
                   PFONTMETRICS output)
{
    TRACE_NATIVE("GpiQueryFonts(%u, %u, '%s', %p, %d, %p)", (uint)hps, (uint)options, face,
                 requested, (int)record_size, output);
    if (!lx_pm_valid_ps(hps) || !requested || *requested < 0 || record_size < 0 ||
        (options & ~(QF_PUBLIC | QF_PRIVATE | QF_NO_GENERIC | QF_NO_DEVICE)) ||
        (*requested && (!record_size || !output)) ||
        (uint64)(uint32)*requested * (uint32)record_size > SIZE_MAX)
        return -255;
    unsigned dpi = lx_pm_dc_dpi(GpiQueryDevice(hps));
    LONG found = 0, copied = 0, capacity = *requested;
    if ((options & QF_PUBLIC) && !(options & QF_NO_DEVICE))
        for (unsigned index = 0; index < lx_host_font_count(); index++)
        {
            LxHostFont *font = lx_host_font_open(
                index, (GPI_DEFAULT_FONT_PIXELS * dpi + GPI_REFERENCE_DPI / 2) / GPI_REFERENCE_DPI);
            FONTMETRICS metrics;
            LxHostFontMetrics host;
            int valid = font && font_metrics(index, font, dpi, &metrics) &&
                        lx_host_font_metrics(font, &host);
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
    if (!font_metrics(lx_pm_font_index(hps), font, lx_pm_dc_dpi(GpiQueryDevice(hps)), &metrics))
        return FALSE;
    size_t bytes = (size_t)record_size < sizeof metrics ? (size_t)record_size : sizeof metrics;
    if (bytes)
        memcpy(output, &metrics, bytes);
    return TRUE;
}

BOOL GpiQueryTextBox(HPS hps, LONG count, PCH text, LONG points, PPOINTL output)
{
    TRACE_NATIVE("GpiQueryTextBox(%u, %d, %p, %d, %p)", (uint)hps, (int)count, text, (int)points,
                 output);
    if (count < 0 || count > GPI_TEXTBOX_MAX_CHARS || (count && !text) || points < 0 ||
        (points && !output))
        return FALSE;
    LxHostFont *font = lx_pm_font(hps);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
        return FALSE;
    LONG width = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint32 codepoint = lx_pm_codepoint((uint8)text[i]);
        int advance = lx_host_font_advance(font, codepoint);
        if (advance < 0)
            return FALSE;
        width += advance + lx_pm_char_extra(lx_pm_font_space(hps));
    }
    POINTL box[TXTBOX_COUNT] = {{0, metrics.ascent},
                                {0, -metrics.descent},
                                {width, metrics.ascent},
                                {width, -metrics.descent},
                                {width, 0}};
    LONG copied = points < TXTBOX_COUNT ? points : TXTBOX_COUNT;
    if (copied)
        memcpy(output, box, (size_t)copied * sizeof *box);
    return TRUE;
}

static BOOL loaded_face_matches(unsigned index, const char *name)
{
    LxHostFont *font = lx_host_font_open(index, GPI_DEFAULT_FONT_PIXELS);
    LxHostFontMetrics metrics;
    BOOL same = font && lx_host_font_metrics(font, &metrics) &&
                (!strcasecmp(name, metrics.face) || !strcasecmp(name, metrics.family));
    lx_host_font_close(font);
    return same;
}

LONG GpiCreateLogFont(HPS handle, PVOID name, LONG id, PFATTRS attrs)
{
    TRACE_NATIVE("GpiCreateLogFont(%u, %p, %d, %p)", (uint)handle, name, (int)id, attrs);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !attrs || attrs->usRecordLength != sizeof *attrs || id < 0 || id > GPI_LCID_MAX ||
        !memchr(attrs->szFacename, 0, FACESIZE))
        return GPI_ERROR;
    if (ps->bitmap_ids[id].resource)
        return GPI_ERROR;
    if (name)
        memcpy(ps->font_names[id], name, sizeof ps->font_names[id]);
    else
        memset(ps->font_names[id], 0, sizeof ps->font_names[id]);
    TRACE_NATIVE(
        "Logical font id=%d face='%s' select=%X match=%d baseline=%d average=%d codepage=%u type=%X use=%X",
        (int)id, attrs->szFacename, attrs->fsSelection, (int)attrs->lMatch,
        (int)attrs->lMaxBaselineExt, (int)attrs->lAveCharWidth, attrs->usCodePage, attrs->fsType,
        attrs->fsFontUse);
    unsigned index = 0;
    BOOL matched = FALSE;
    for (unsigned i = 0; i < BUILTIN_FACES; i++)
    {
        char face[64];
        snprintf(face, sizeof face, "%s%s", families[i / FONT_STYLES], styles[i % FONT_STYLES]);
        if (!strcasecmp(attrs->szFacename, face))
        {
            index = i;
            matched = TRUE;
            break;
        }
    }
    const unsigned font_count = lx_host_font_count();
    for (unsigned i = BUILTIN_FACES; i < font_count && !matched; i++)
        if (loaded_face_matches(i, attrs->szFacename))
        {
            index = i;
            matched = TRUE;
        }
    BOOL forced_match = FALSE;
    /* lMatch is the host font index + 1, as GpiQueryFonts reports it. */
    if (attrs->lMatch > BUILTIN_FACES && (ULONG)attrs->lMatch <= font_count &&
        loaded_face_matches((unsigned)attrs->lMatch - 1, attrs->szFacename))
    {
        index = (unsigned)attrs->lMatch - 1;
        forced_match = TRUE;
    }
    if (attrs->lMatch > 0 && attrs->lMatch <= BUILTIN_FACES)
    {
        unsigned candidate = (unsigned)attrs->lMatch - 1;
        char face[64];
        snprintf(face, sizeof face, "%s%s", families[candidate / FONT_STYLES],
                 styles[candidate % FONT_STYLES]);
        if (!strcasecmp(attrs->szFacename, face))
        {
            index = candidate;
            forced_match = TRUE;
        }
    }
    /* A valid lMatch fixes the physical face. Requested style can be simulated
       without changing the match number/design attributes returned by queries. */
    unsigned physical_index = index;
    if (index < BUILTIN_FACES)
        index = (index & ~(FONT_STYLES - 1u)) | (index % FONT_STYLES) |
                ((attrs->fsSelection & FATTR_SEL_BOLD) ? STYLE_BOLD : 0) |
                ((attrs->fsSelection & FATTR_SEL_ITALIC) ? STYLE_ITALIC : 0);
    unsigned pixels =
        !(attrs->fsFontUse & 2) && attrs->lMaxBaselineExt > 0 &&
                attrs->lMaxBaselineExt <= MAX_FONT_PIXELS
            ? (unsigned)attrs->lMaxBaselineExt
            : (GPI_DEFAULT_FONT_PIXELS * lx_pm_dc_dpi(ps->dc) + GPI_REFERENCE_DPI / 2) /
                  GPI_REFERENCE_DPI;
    ps->logical_fonts[id].index = index;
    ps->logical_fonts[id].metric_index = forced_match ? physical_index : index;
    ps->logical_fonts[id].pixels = pixels;
    ps->logical_fonts[id].defined = TRUE;
    return matched ? FONT_MATCH : FONT_DEFAULT;
}

BOOL GpiSetCharSet(HPS handle, LONG id)
{
    TRACE_NATIVE("GpiSetCharSet(%u, %d)", (uint)handle, (int)id);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < 0 || id > GPI_LCID_MAX || (id && !ps->logical_fonts[id].defined))
        return FALSE;
    ps->charset = id;
    return TRUE;
}

LONG GpiQueryCharSet(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->charset : LCID_ERROR;
}

BOOL GpiDeleteSetId(HPS handle, LONG id)
{
    TRACE_NATIVE("GpiDeleteSetId(%u, %d)", (uint)handle, (int)id);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < LCID_ALL || id > GPI_LCID_MAX || (id > 0 && ps->charset == id))
        return FALSE;
    if (id == LCID_ALL)
    {
        for (unsigned i = 0; i <= GPI_LCID_MAX; i++)
            lx_handle_release(&ps->bitmap_ids[i]);
        memset(ps->logical_fonts, 0, sizeof ps->logical_fonts);
        ps->charset = 0;
    }
    else
    {
        lx_handle_release(&ps->bitmap_ids[id]);
        ps->logical_fonts[id].defined = FALSE;
    }
    return TRUE;
}

BOOL GpiSetCharBox(HPS handle, PSIZEF box)
{
    TRACE_NATIVE("GpiSetCharBox(%u, %d, %d)", (uint)handle, box ? box->cx : 0, box ? box->cy : 0);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !box)
        return FALSE;
    ps->char_width = box->cx;
    ps->char_height = box->cy;
    return TRUE;
}

BOOL GpiQueryCharBox(HPS handle, PSIZEF box)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !box)
        return FALSE;
    LONG pixels = (LONG)((GPI_DEFAULT_FONT_PIXELS * lx_pm_dc_dpi(ps->dc) + GPI_REFERENCE_DPI / 2) /
                         GPI_REFERENCE_DPI);
    box->cx = ps->char_width ? ps->char_width : pixels * GPI_FIXED_ONE;
    box->cy = ps->char_height ? ps->char_height : pixels * GPI_FIXED_ONE;
    return TRUE;
}

LONG GpiQueryNumberSetIds(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return GPI_ALTERROR;
    LONG count = 0;
    for (unsigned i = 0; i <= GPI_LCID_MAX; i++)
        if (ps->logical_fonts[i].defined || ps->bitmap_ids[i].resource)
            count++;
    return count;
}

BOOL GpiQuerySetIds(HPS handle, LONG count, PLONG types, PVOID names, PLONG ids)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || count < 0 || (count && (!types || !names || !ids)))
        return FALSE;
    LONG copied = 0;
    for (unsigned i = 0; i <= GPI_LCID_MAX && copied < count; i++)
    {
        BOOL font = ps->logical_fonts[i].defined;
        if (!font && !ps->bitmap_ids[i].resource)
            continue;
        types[copied] = font ? LCIDT_FONT : LCIDT_BITMAP;
        ids[copied] = (LONG)i;
        if (font)
            memcpy((uint8 *)names + copied * SETID_NAME_SIZE, ps->font_names[i], SETID_NAME_SIZE);
        else
            memset((uint8 *)names + copied * SETID_NAME_SIZE, 0, SETID_NAME_SIZE);
        copied++;
    }
    return TRUE;
}

BOOL GpiQueryWidthTable(HPS handle, LONG first, LONG count, PLONG output)
{
    if (first < 0 || first > 255 || count < 0 || count > 256 - first || (count && !output))
        return FALSE;
    LxHostFont *font = lx_pm_font(handle);
    if (!font)
        return FALSE;
    for (LONG i = 0; i < count; i++)
        output[i] = lx_host_font_advance(font, lx_pm_codepoint((uint8)(first + i)));
    return TRUE;
}
