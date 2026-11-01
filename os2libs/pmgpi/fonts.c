/* Public domain. No rights reserved. Wholly AI-authored. */
/* Public outline families are explicit substitutions backed by measured host faces. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/graphics.h"
#include "hal/font.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pm_text.h"

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
    output->lLowerCaseAscent = m.lowercase_ascent;
    output->lLowerCaseDescent = m.lowercase_descent;
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
        uint32 codepoint = lx_pm_codepoint((uint8)text[i]);
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

LONG GpiCreateLogFont(HPS handle, PVOID name, LONG id, PFATTRS attrs)
{
    TRACE_NATIVE("GpiCreateLogFont(%u, %p, %d, %p)", (uint)handle, name, (int)id, attrs);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !attrs || attrs->usRecordLength != sizeof *attrs || id < 0 || id > 254 ||
        !memchr(attrs->szFacename, 0, FACESIZE))
        return 0;
    if (ps->bitmap_ids[id].resource)
        return 0;
    if (name)
        memcpy(ps->font_names[id], name, 8);
    else
        memset(ps->font_names[id], 0, 8);
    unsigned index = 0;
    BOOL matched = FALSE;
    for (unsigned i = 0; i < 12; i++)
    {
        char face[64];
        snprintf(face, sizeof face, "%s%s", families[i / 4], styles[i % 4]);
        if (!strcasecmp(attrs->szFacename, face))
        {
            index = i;
            matched = TRUE;
            break;
        }
    }
    if (attrs->lMatch > 0 && attrs->lMatch <= 12)
    {
        unsigned candidate = (unsigned)attrs->lMatch - 1;
        char face[64];
        snprintf(face, sizeof face, "%s%s", families[candidate / 4], styles[candidate % 4]);
        if (!strcasecmp(attrs->szFacename, face))
            index = candidate;
    }
    index = (index & ~3u) | (index % 4) | ((attrs->fsSelection & 0x20) ? 1 : 0) |
            ((attrs->fsSelection & 1) ? 2 : 0);
    unsigned pixels = attrs->lMaxBaselineExt > 0 && attrs->lMaxBaselineExt <= 512
                          ? (unsigned)attrs->lMaxBaselineExt
                          : 16;
    ps->logical_fonts[id].index = index;
    ps->logical_fonts[id].pixels = pixels;
    ps->logical_fonts[id].defined = TRUE;
    return matched ? 2 : 1;
}
BOOL GpiSetCharSet(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < 0 || id > 254 || (id && !ps->logical_fonts[id].defined))
        return FALSE;
    ps->charset = id;
    return TRUE;
}
LONG GpiQueryCharSet(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->charset : -1;
}
BOOL GpiDeleteSetId(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < -1 || id > 254 || (id > 0 && ps->charset == id))
        return FALSE;
    if (id == -1)
    {
        for (unsigned i = 0; i < 255; i++)
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
    box->cx = ps->char_width ? ps->char_width : 16 * 65536;
    box->cy = ps->char_height ? ps->char_height : 16 * 65536;
    return TRUE;
}

LONG GpiQueryNumberSetIds(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return -1;
    LONG count = 0;
    for (unsigned i = 0; i < 255; i++)
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
    for (unsigned i = 0; i < 255 && copied < count; i++)
    {
        BOOL font = ps->logical_fonts[i].defined;
        if (!font && !ps->bitmap_ids[i].resource)
            continue;
        types[copied] = font ? 6 : 7;
        ids[copied] = (LONG)i;
        if (font)
            memcpy((uint8 *)names + copied * 8, ps->font_names[i], 8);
        else
            memset((uint8 *)names + copied * 8, 0, 8);
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
LONG GpiCharStringAt(HPS handle, PPOINTL point, LONG count, PCH text)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? lx_pm_text(handle, point, count, text, ps->primitive_color[1], NULL) : 0;
}
LONG GpiCharString(HPS handle, LONG count, PCH text)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    POINTL point = ps ? ps->position : (POINTL){0, 0};
    return ps ? GpiCharStringAt(handle, &point, count, text) : 0;
}

BOOL GpiQueryCharAngle(HPS handle, PPOINTL angle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!angle || !ps)
        return FALSE;
    /* The current text renderer advances horizontally in device space. */
    *angle = ps->char_angle;
    return TRUE;
}

BOOL GpiQueryCharShear(HPS handle, PPOINTL shear)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!shear || !ps)
        return FALSE;
    /* Glyph outlines have no additional geometric shear applied. */
    *shear = ps->char_shear;
    return TRUE;
}

BOOL GpiQueryTextAlignment(HPS handle, PLONG horizontal, PLONG vertical)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!horizontal || !vertical || !ps)
        return FALSE;
    *horizontal = ps->text_horizontal;
    *vertical = ps->text_vertical;
    return TRUE;
}

BOOL GpiSetCharAngle(HPS handle, PPOINTL angle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !angle || angle->x < 0 || angle->y)
        return FALSE;
    ps->char_angle = *angle;
    return TRUE;
}
BOOL GpiSetCharShear(HPS handle, PPOINTL shear)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !shear || shear->x || shear->y < 0)
        return FALSE;
    ps->char_shear = *shear;
    return TRUE;
}
BOOL GpiSetCharDirection(HPS handle, LONG direction)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || direction < 0 || direction > 1)
        return FALSE;
    ps->char_direction = direction;
    return TRUE;
}
LONG GpiQueryCharDirection(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->char_direction : -1;
}
BOOL GpiSetTextAlignment(HPS handle, LONG horizontal, LONG vertical)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    /* Default/normal/left/standard horizontal and baseline vertical modes
       share the renderer's left-to-right baseline origin. */
    if (!ps || (horizontal != 0 && horizontal != 1 && horizontal != 2 && horizontal != 5) ||
        (vertical != 0 && vertical != 0x100 && vertical != 0x400 && vertical != 0x600))
        return FALSE;
    ps->text_horizontal = horizontal;
    ps->text_vertical = vertical;
    return TRUE;
}
