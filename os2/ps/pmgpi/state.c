/* Public domain. No rights reserved. Wholly AI-authored. */
/* GPI attributes and saved-state stack belong to each presentation space. */
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include <math.h>
#include "os2/ps/pmgpi/internal.h"

/* Guest attribute bundle layouts. Every bundle starts with lColor, lBackColor, usMixMode and
   usBackMixMode, and every bundle mask uses the same four low bits for them. */
enum
{
    BUNDLE_COLOR_OFFSET = 0,
    BUNDLE_BACK_COLOR_OFFSET = 4,
    BUNDLE_MIX_MODE_OFFSET = 8,
    BUNDLE_BACK_MIX_MODE_OFFSET = 10,
    BUNDLE_COLOR = LBB_COLOR,
    BUNDLE_BACK_COLOR = LBB_BACK_COLOR,
    BUNDLE_MIX_MODE = LBB_MIX_MODE,
    BUNDLE_BACK_MIX_MODE = LBB_BACK_MIX_MODE,
    BUNDLE_COMMON = BUNDLE_COLOR | BUNDLE_BACK_COLOR | BUNDLE_MIX_MODE | BUNDLE_BACK_MIX_MODE,
    LINEBUNDLE_WIDTH_OFFSET = 12,
    LINEBUNDLE_GEOM_WIDTH_OFFSET = 16,
    LINEBUNDLE_TYPE_OFFSET = 20,
    LINEBUNDLE_END_OFFSET = 22,
    LINEBUNDLE_JOIN_OFFSET = 24,
    CHARBUNDLE_SET_OFFSET = 12,
    CHARBUNDLE_PRECISION_OFFSET = 14,
    CHARBUNDLE_CELL_OFFSET = 16,
    CHARBUNDLE_ANGLE_OFFSET = 24,
    CHARBUNDLE_SHEAR_OFFSET = 32,
    CHARBUNDLE_DIRECTION_OFFSET = 40,
    CHARBUNDLE_TEXT_ALIGN_OFFSET = 42,
    CHARBUNDLE_EXTRA_OFFSET = 44,
    CHARBUNDLE_BREAK_EXTRA_OFFSET = 48,
    MARKERBUNDLE_SET_OFFSET = 12,
    MARKERBUNDLE_SYMBOL_OFFSET = 14,
    MARKERBUNDLE_CELL_OFFSET = 16,
    AREABUNDLE_SET_OFFSET = 12,
    AREABUNDLE_SYMBOL_OFFSET = 14,
    AREABUNDLE_REF_POINT_OFFSET = 16,
    /* The default logical colour table holds CLR_BACKGROUND to CLR_PALEGRAY. */
    DEFAULT_COLOR_COUNT = CLR_PALEGRAY + 1,
    /* GpiRestorePS: restore the most recently saved state. */
    RESTORE_LAST = -1,
    /* FM_* and BM_* leave the value 3 unused. */
    MIX_UNUSED = 3
};

BOOL GpiSetMix(HPS handle, LONG mode)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || mode < FM_DEFAULT || mode > FM_ONE || mode == MIX_UNUSED)
        return FALSE;
    ps->mix = mode ? mode : FM_OVERPAINT;
    for (unsigned i = 0; i < sizeof ps->primitive_mix / sizeof *ps->primitive_mix; i++)
        ps->primitive_mix[i] = ps->mix;
    return TRUE;
}

BOOL GpiSetBackMix(HPS handle, LONG mode)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || mode < BM_DEFAULT || mode > BM_DESTTRANSPARENT || mode == MIX_UNUSED)
        return FALSE;
    ps->back_mix = mode ? mode : BM_LEAVEALONE;
    for (unsigned i = 0; i < sizeof ps->primitive_back_mix / sizeof *ps->primitive_back_mix; i++)
        ps->primitive_back_mix[i] = ps->back_mix;
    return TRUE;
}

LONG GpiQueryMix(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->mix : FM_ERROR;
}

LONG GpiQueryBackMix(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->back_mix : BM_ERROR;
}

BOOL GpiSetBackColor(HPS handle, LONG color)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || lx_pm_resolve_color(handle, color) < 0)
        return FALSE;
    ps->back_color = color;
    for (unsigned i = 0; i < sizeof ps->primitive_back / sizeof *ps->primitive_back; i++)
        ps->primitive_back[i] = color;
    return TRUE;
}

LONG GpiQueryBackColor(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->back_color : CLR_ERROR;
}

BOOL GpiSetLineType(HPS handle, LONG type)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || type < LINETYPE_DEFAULT || type > LINETYPE_ALTERNATE)
        return FALSE;
    ps->line_type = type ? type : LINETYPE_SOLID;
    return TRUE;
}

LONG GpiQueryLineType(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->line_type : LINETYPE_ERROR;
}

LONG GpiSavePS(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || ps->save_id == INT32_MAX)
        return GPI_ALTERROR;
    PmSavedSpace *s = calloc(1, sizeof *s);
    if (!s)
        return GPI_ALTERROR;
    s->id = ++ps->save_id;
    if (ps->gpi.palette &&
        !lx_handle_borrow(ps->gpi.palette, LX_HANDLE_PM_PALETTE, &s->palette_lease))
    {
        free(s);
        return GPI_ALTERROR;
    }
    s->gpi = ps->gpi;
    s->position = ps->position;
    s->color = ps->color;
    s->back_color = ps->back_color;
    s->mix = ps->mix;
    s->back_mix = ps->back_mix;
    s->line_type = ps->line_type;
    s->pattern_set = ps->pattern_set;
    s->pattern = ps->pattern;
    s->pattern_reference = ps->pattern_reference;
    memcpy(s->primitive_color, ps->primitive_color, sizeof s->primitive_color);
    memcpy(s->primitive_back, ps->primitive_back, sizeof s->primitive_back);
    memcpy(s->primitive_mix, ps->primitive_mix, sizeof s->primitive_mix);
    memcpy(s->primitive_back_mix, ps->primitive_back_mix, sizeof s->primitive_back_mix);
    s->char_angle = ps->char_angle;
    s->char_shear = ps->char_shear;
    s->char_direction = ps->char_direction;
    s->text_horizontal = ps->text_horizontal;
    s->text_vertical = ps->text_vertical;
    s->charset = ps->charset;
    s->char_mode = ps->char_mode;
    s->char_width = ps->char_width;
    s->char_height = ps->char_height;
    s->viewport = ps->viewport;
    s->viewport_set = ps->viewport_set;
    s->page_size = ps->page_size;
    s->rgb_colors = ps->rgb_colors;
    s->color_format = ps->color_format;
    memcpy(s->colors, ps->colors, sizeof s->colors);
    memcpy(s->color_valid, ps->color_valid, sizeof s->color_valid);
    s->next = ps->saved;
    ps->saved = s;
    return s->id;
}

BOOL GpiRestorePS(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id == 0 || id < RESTORE_LAST)
        return FALSE;
    PmSavedSpace *s = ps->saved;
    while (s && id != RESTORE_LAST && s->id != id)
        s = s->next;
    if (!s)
        return FALSE;
    lx_handle_release(&ps->palette_lease);
    ps->palette_lease = s->palette_lease;
    memset(&s->palette_lease, 0, sizeof s->palette_lease);
    ps->gpi = s->gpi;
    ps->position = s->position;
    ps->color = s->color;
    ps->back_color = s->back_color;
    ps->mix = s->mix;
    ps->back_mix = s->back_mix;
    ps->line_type = s->line_type;
    ps->pattern_set = s->pattern_set;
    ps->pattern = s->pattern;
    ps->pattern_reference = s->pattern_reference;
    memcpy(ps->primitive_color, s->primitive_color, sizeof s->primitive_color);
    memcpy(ps->primitive_back, s->primitive_back, sizeof s->primitive_back);
    memcpy(ps->primitive_mix, s->primitive_mix, sizeof s->primitive_mix);
    memcpy(ps->primitive_back_mix, s->primitive_back_mix, sizeof s->primitive_back_mix);
    ps->char_angle = s->char_angle;
    ps->char_shear = s->char_shear;
    ps->char_direction = s->char_direction;
    ps->text_horizontal = s->text_horizontal;
    ps->text_vertical = s->text_vertical;
    ps->charset = s->charset;
    ps->char_mode = s->char_mode;
    ps->char_width = s->char_width;
    ps->char_height = s->char_height;
    ps->viewport = s->viewport;
    ps->viewport_set = s->viewport_set;
    ps->page_size = s->page_size;
    ps->rgb_colors = s->rgb_colors;
    ps->color_format = s->color_format;
    memcpy(ps->colors, s->colors, sizeof s->colors);
    memcpy(ps->color_valid, s->color_valid, sizeof s->color_valid);
    PmSavedSpace *end = s->next;
    while (ps->saved != end)
    {
        s = ps->saved;
        ps->saved = s->next;
        lx_handle_release(&s->palette_lease);
        free(s);
    }
    return TRUE;
}

BOOL GpiSetPageViewport(HPS handle, PRECTL rect)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect || rect->xRight <= rect->xLeft || rect->yTop <= rect->yBottom ||
        (int64_t)rect->xRight - rect->xLeft > INT32_MAX ||
        (int64_t)rect->yTop - rect->yBottom > INT32_MAX)
        return FALSE;
    ps->viewport = *rect;
    ps->viewport_set = TRUE;
    return TRUE;
}

BOOL GpiQueryPageViewport(HPS handle, PRECTL rect)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect)
        return FALSE;
    SIZEL page = lx_pm_page_dimensions(ps);
    POINTL corner = {page.cx, page.cy};
    if (!ps->viewport_set && !lx_pm_transform_point(ps, &corner, TRUE))
        return FALSE;
    *rect = ps->viewport_set ? ps->viewport : (RECTL){0, 0, corner.x, corner.y};
    return TRUE;
}

BOOL GpiConvert(HPS handle, LONG source, LONG target, LONG count, PPOINTL points)
{
    return lx_pm_convert_points(handle, source, target, count, points);
}

BOOL GpiSetAttrs(HPS handle, LONG primitive, ULONG mask, ULONG defaults, PVOID attrs)
{
    TRACE_NATIVE("GpiSetAttrs(%u, %d, %u, %u, %p)", (uint)handle, (int)primitive, (uint)mask,
                 (uint)defaults, attrs);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || primitive < PRIM_LINE || primitive > PRIM_IMAGE || (mask & ~defaults && !attrs))
        return FALSE;
    uint8 zero[CHARBUNDLE_SIZE] = {0};
    const uint8 *p = attrs ? attrs : zero;
    TRACE_NATIVE("GpiSetAttrs color=%X back=%X mix=%u back_mix=%u set=%u symbol=%u",
                 (uint)lx_guest_load_u32(p + BUNDLE_COLOR_OFFSET),
                 (uint)lx_guest_load_u32(p + BUNDLE_BACK_COLOR_OFFSET),
                 (uint)lx_guest_load_u16(p + BUNDLE_MIX_MODE_OFFSET),
                 (uint)lx_guest_load_u16(p + BUNDLE_BACK_MIX_MODE_OFFSET),
                 (uint)lx_guest_load_u16(p + AREABUNDLE_SET_OFFSET),
                 (uint)lx_guest_load_u16(p + AREABUNDLE_SYMBOL_OFFSET));
    /* Markers and images take only the common attributes. */
    ULONG supported =
        primitive == PRIM_CHAR ? CBB_COLOR | CBB_BACK_COLOR | CBB_MIX_MODE | CBB_BACK_MIX_MODE |
                                     CBB_SET | CBB_MODE | CBB_BOX | CBB_ANGLE | CBB_SHEAR |
                                     CBB_DIRECTION | CBB_TEXT_ALIGN | CBB_EXTRA | CBB_BREAK_EXTRA
        : primitive == PRIM_LINE ? LBB_COLOR | LBB_BACK_COLOR | LBB_MIX_MODE | LBB_BACK_MIX_MODE |
                                       LBB_WIDTH | LBB_GEOM_WIDTH | LBB_TYPE | LBB_END | LBB_JOIN
        : primitive == PRIM_AREA ? ABB_COLOR | ABB_BACK_COLOR | ABB_MIX_MODE | ABB_BACK_MIX_MODE |
                                       ABB_SET | ABB_SYMBOL | ABB_REF_POINT
                                 : BUNDLE_COMMON;
    if (mask & ~supported)
        return FALSE;
    /* Current renderer accepts horizontal character axes and unit-width lines. */
    if (primitive == PRIM_CHAR)
    {
        if ((mask & CBB_MODE) && !(defaults & CBB_MODE))
        {
            unsigned mode = lx_guest_load_u16(p + CHARBUNDLE_PRECISION_OFFSET);
            if (mode < CM_MODE1 || mode > CM_MODE3)
                return FALSE;
        }
        if ((mask & CBB_ANGLE) && !(defaults & CBB_ANGLE))
        {
            LONG x = (LONG)lx_guest_load_u32(p + CHARBUNDLE_ANGLE_OFFSET),
                 y = (LONG)lx_guest_load_u32(p + CHARBUNDLE_ANGLE_OFFSET + 4);
            if (x <= 0 || y != 0)
                return FALSE;
        }
        if ((mask & CBB_SET) &&
            !GpiSetCharSet(handle,
                           (defaults & CBB_SET) ? 0 : lx_guest_load_u16(p + CHARBUNDLE_SET_OFFSET)))
            return FALSE;
        if (mask & CBB_BOX)
        {
            SIZEF box = {GPI_DEFAULT_FONT_PIXELS * GPI_FIXED_ONE,
                         GPI_DEFAULT_FONT_PIXELS * GPI_FIXED_ONE};
            if (!(defaults & CBB_BOX))
            {
                box.cx = (LONG)lx_guest_load_u32(p + CHARBUNDLE_CELL_OFFSET);
                box.cy = (LONG)lx_guest_load_u32(p + CHARBUNDLE_CELL_OFFSET + 4);
            }
            if (!GpiSetCharBox(handle, &box))
                return FALSE;
        }
    }
    if (primitive == PRIM_LINE)
    {
        if ((mask & LBB_WIDTH) && !(defaults & LBB_WIDTH) &&
            lx_guest_load_u32(p + LINEBUNDLE_WIDTH_OFFSET) != LINEWIDTH_NORMAL)
            return FALSE;
        if ((mask & LBB_GEOM_WIDTH) && !(defaults & LBB_GEOM_WIDTH) &&
            (LONG)lx_guest_load_u32(p + LINEBUNDLE_GEOM_WIDTH_OFFSET) < 0)
            return FALSE;
        if ((mask & LBB_END) && !(defaults & LBB_END) &&
            lx_guest_load_u16(p + LINEBUNDLE_END_OFFSET) > LINEEND_ROUND)
            return FALSE;
        if ((mask & LBB_JOIN) && !(defaults & LBB_JOIN) &&
            lx_guest_load_u16(p + LINEBUNDLE_JOIN_OFFSET) > LINEJOIN_MITRE)
            return FALSE;
        if ((mask & LBB_TYPE) &&
            !GpiSetLineType(
                handle, (defaults & LBB_TYPE) ? 0 : lx_guest_load_u16(p + LINEBUNDLE_TYPE_OFFSET)))
            return FALSE;
    }
    if (primitive == PRIM_AREA)
    {
        if ((mask & ABB_SET) &&
            !GpiSetPatternSet(
                handle, (defaults & ABB_SET) ? 0 : lx_guest_load_u16(p + AREABUNDLE_SET_OFFSET)))
            return FALSE;
        if ((mask & ABB_SYMBOL) &&
            !GpiSetPattern(handle, (defaults & ABB_SYMBOL)
                                       ? 0
                                       : lx_guest_load_u16(p + AREABUNDLE_SYMBOL_OFFSET)))
            return FALSE;
        if (mask & ABB_REF_POINT)
            ps->pattern_reference =
                (defaults & ABB_REF_POINT)
                    ? (POINTL){0, 0}
                    : (POINTL){(LONG)lx_guest_load_u32(p + AREABUNDLE_REF_POINT_OFFSET),
                               (LONG)lx_guest_load_u32(p + AREABUNDLE_REF_POINT_OFFSET + 4)};
    }
    if (primitive == PRIM_LINE)
    {
        if ((mask & LBB_GEOM_WIDTH) &&
            !GpiSetLineWidthGeom(handle,
                                 defaults & LBB_GEOM_WIDTH
                                     ? 1
                                     : (LONG)lx_guest_load_u32(p + LINEBUNDLE_GEOM_WIDTH_OFFSET)))
            return FALSE;
        if ((mask & LBB_END) &&
            !GpiSetLineEnd(handle,
                           defaults & LBB_END ? 0 : lx_guest_load_u16(p + LINEBUNDLE_END_OFFSET)))
            return FALSE;
        if ((mask & LBB_JOIN) &&
            !GpiSetLineJoin(
                handle, defaults & LBB_JOIN ? 0 : lx_guest_load_u16(p + LINEBUNDLE_JOIN_OFFSET)))
            return FALSE;
    }
    if (primitive == PRIM_CHAR && (mask & CBB_EXTRA) &&
        !GpiSetCharExtra(handle, defaults & CBB_EXTRA
                                     ? 0
                                     : (LONG)lx_guest_load_u32(p + CHARBUNDLE_EXTRA_OFFSET)))
        return FALSE;
    unsigned index = GPI_PRIM_INDEX((unsigned)primitive);
    LONG color =
        (defaults & BUNDLE_COLOR) ? CLR_BLACK : (LONG)lx_guest_load_u32(p + BUNDLE_COLOR_OFFSET);
    LONG back = (defaults & BUNDLE_BACK_COLOR)
                    ? CLR_WHITE
                    : (LONG)lx_guest_load_u32(p + BUNDLE_BACK_COLOR_OFFSET);
    LONG mix =
        (defaults & BUNDLE_MIX_MODE) ? FM_OVERPAINT : lx_guest_load_u16(p + BUNDLE_MIX_MODE_OFFSET);
    LONG back_mix = (defaults & BUNDLE_BACK_MIX_MODE)
                        ? BM_LEAVEALONE
                        : lx_guest_load_u16(p + BUNDLE_BACK_MIX_MODE_OFFSET);
    if ((mask & BUNDLE_COLOR) && lx_pm_resolve_color(handle, color) < 0)
        return FALSE;
    if ((mask & BUNDLE_BACK_COLOR) && lx_pm_resolve_color(handle, back) < 0)
        return FALSE;
    if ((mask & BUNDLE_MIX_MODE) && (mix < FM_OR || mix > FM_ONE || mix == MIX_UNUSED))
        return FALSE;
    if ((mask & BUNDLE_BACK_MIX_MODE) &&
        (back_mix < BM_OR || back_mix > BM_DESTTRANSPARENT || back_mix == MIX_UNUSED))
        return FALSE;
    if (mask & BUNDLE_COLOR)
        ps->primitive_color[index] = color;
    if (mask & BUNDLE_BACK_COLOR)
        ps->primitive_back[index] = back;
    if (mask & BUNDLE_MIX_MODE)
        ps->primitive_mix[index] = mix;
    if (mask & BUNDLE_BACK_MIX_MODE)
        ps->primitive_back_mix[index] = back_mix;
    return TRUE;
}

LONG GpiQueryPatternSet(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->pattern_set : LCID_ERROR;
}

BOOL GpiSetPatternSet(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < 0 || id > GPI_LCID_MAX || (id && !ps->bitmap_ids[id].resource))
        return FALSE;
    ps->pattern_set = id;
    return TRUE;
}

LONG GpiQueryPattern(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->pattern : PATSYM_ERROR;
}

BOOL GpiSetPattern(HPS handle, LONG symbol)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || symbol < PATSYM_DEFAULT || (symbol > PATSYM_DIAGHATCH && symbol != PATSYM_BLANK))
        return FALSE;
    ps->pattern = symbol ? symbol : PATSYM_SOLID;
    return TRUE;
}

BOOL GpiQueryCurrentPosition(HPS handle, PPOINTL point)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point)
        return FALSE;
    *point = ps->position;
    return TRUE;
}

BOOL GpiSetCurrentPosition(HPS handle, PPOINTL point)
{
    return GpiMove(handle, point);
}

LONG GpiQueryColor(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->color : CLR_ERROR;
}

LONG GpiQueryLogColorTable(HPS handle, ULONG options, LONG start, LONG count, PLONG output)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || (options & ~LCOLOPT_INDEX) || start < 0 || count < 0 || (count && !output))
        return QLCT_ERROR;
    if (ps->rgb_colors)
        return QLCT_RGB;
    LONG last = DEFAULT_COLOR_COUNT - 1;
    for (LONG i = DEFAULT_COLOR_COUNT; i < GPI_MAX_PALETTE_COLORS; i++)
        if (ps->color_valid[i])
            last = i;
    LONG returned = 0;
    for (LONG i = start; i <= last && returned < count; i++)
    {
        BOOL loaded = i < DEFAULT_COLOR_COUNT || ps->color_valid[i];
        if (options & LCOLOPT_INDEX)
        {
            if (!loaded)
                continue;
            if (count - returned < 2)
                break;
            output[returned++] = i;
        }
        output[returned++] = loaded ? lx_pm_resolve_color(handle, i) : QLCT_NOTLOADED;
    }
    return returned;
}

BOOL GpiQueryColorData(HPS handle, LONG count, PLONG output)
{
    TRACE_NATIVE("GpiQueryColorData(%u, %d, %p)", (uint)handle, (int)count, output);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || count < 0 || count > QCD_LCT_OPTIONS + 1 || (count && !output))
        return FALSE;
    LONG last = ps->rgb_colors ? RGB_WHITE : DEFAULT_COLOR_COUNT - 1;
    if (!ps->rgb_colors)
        for (LONG i = DEFAULT_COLOR_COUNT; i < GPI_MAX_PALETTE_COLORS; i++)
            if (ps->color_valid[i])
                last = i;
    /* Indexed by QCD_LCT_FORMAT, QCD_LCT_LOINDEX, QCD_LCT_HIINDEX and QCD_LCT_OPTIONS. */
    const LONG data[QCD_LCT_OPTIONS + 1] = {ps->color_format, 0, last, 0};
    if (count)
        memcpy(output, data, (size_t)count * sizeof *output);
    return TRUE;
}

BOOL GpiSetCharMode(HPS handle, LONG mode)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || mode < CM_DEFAULT || mode > CM_MODE3)
        return FALSE;
    ps->char_mode = mode ? mode : CM_MODE1;
    return TRUE;
}

LONG GpiQueryCharMode(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? (ps->char_mode ? ps->char_mode : CM_MODE1) : CM_ERROR;
}

BOOL GpiSetPatternRefPoint(HPS handle, PPOINTL point)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point)
        return FALSE;
    ps->pattern_reference = *point;
    return TRUE;
}

BOOL GpiQueryPatternRefPoint(HPS handle, PPOINTL point)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point)
        return FALSE;
    *point = ps->pattern_reference;
    return TRUE;
}

BOOL GpiSetArcParams(HPS h, PLONG arc)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !arc)
        return FALSE;
    memcpy(p->gpi.arc, arc, sizeof p->gpi.arc);
    return TRUE;
}

#define ATTRIBUTE(Name, field, min, max, def, error)                                               \
    BOOL GpiSet##Name(HPS h, LONG v)                                                               \
    {                                                                                              \
        PresentationSpace *p = gpi_space(h);                                                       \
        if (!p || v < min || v > max)                                                              \
            return FALSE;                                                                          \
        p->gpi.field = v ? v : def;                                                                \
        return TRUE;                                                                               \
    }                                                                                              \
    LONG GpiQuery##Name(HPS h)                                                                     \
    {                                                                                              \
        PresentationSpace *p = gpi_space(h);                                                       \
        return p ? p->gpi.field : error;                                                           \
    }
ATTRIBUTE(LineEnd, line_end, LINEEND_DEFAULT, LINEEND_ROUND, LINEEND_FLAT, LINEEND_ERROR)
ATTRIBUTE(LineJoin, line_join, LINEJOIN_DEFAULT, LINEJOIN_MITRE, LINEJOIN_BEVEL, LINEJOIN_ERROR)

BOOL GpiSetLineWidthGeom(HPS h, LONG v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || v < 0 || p->record_path)
        return FALSE;
    p->gpi.line_width = v;
    return TRUE;
}

LONG GpiQueryLineWidthGeom(HPS h)
{
    PresentationSpace *p = gpi_space(h);
    return p ? p->gpi.line_width : LINEWIDTHGEOM_ERROR;
}

BOOL GpiSetCharExtra(HPS h, LONG v)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || p->area_recording)
        return FALSE;
    p->gpi.char_extra = v;
    return TRUE;
}

LONG GpiQueryAttrs(HPS h, LONG primitive, ULONG mask, PVOID output)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || primitive < PRIM_LINE || primitive > PRIM_IMAGE || (mask && !output))
        return GPI_ALTERROR;
    if (!mask)
        return 0;
    uint8 *b = output;
    unsigned k = GPI_PRIM_INDEX(primitive);
    ULONG defaults = 0;
#define DEFAULTED(bit, test)                                                                       \
    if (test)                                                                                      \
    defaults |= (bit)
    if (mask & BUNDLE_COLOR)
        lx_guest_store_u32(b + BUNDLE_COLOR_OFFSET, p->primitive_color[k]);
    DEFAULTED(BUNDLE_COLOR, p->primitive_color[k] == CLR_BLACK);
    if (mask & BUNDLE_BACK_COLOR)
        lx_guest_store_u32(b + BUNDLE_BACK_COLOR_OFFSET, p->primitive_back[k]);
    DEFAULTED(BUNDLE_BACK_COLOR, p->primitive_back[k] == CLR_WHITE);
    if (mask & BUNDLE_MIX_MODE)
        lx_guest_store_u16(b + BUNDLE_MIX_MODE_OFFSET, p->primitive_mix[k]);
    DEFAULTED(BUNDLE_MIX_MODE, p->primitive_mix[k] == FM_OVERPAINT);
    if (mask & BUNDLE_BACK_MIX_MODE)
        lx_guest_store_u16(b + BUNDLE_BACK_MIX_MODE_OFFSET, p->primitive_back_mix[k]);
    DEFAULTED(BUNDLE_BACK_MIX_MODE, p->primitive_back_mix[k] == BM_LEAVEALONE);
    if (primitive == PRIM_LINE)
    {
        if (mask & LBB_WIDTH)
            lx_guest_store_u32(b + LINEBUNDLE_WIDTH_OFFSET, LINEWIDTH_NORMAL);
        defaults |= LBB_WIDTH;
        if (mask & LBB_GEOM_WIDTH)
            lx_guest_store_u32(b + LINEBUNDLE_GEOM_WIDTH_OFFSET, p->gpi.line_width);
        DEFAULTED(LBB_GEOM_WIDTH, p->gpi.line_width == 1);
        if (mask & LBB_TYPE)
            lx_guest_store_u16(b + LINEBUNDLE_TYPE_OFFSET, p->line_type);
        DEFAULTED(LBB_TYPE, p->line_type == LINETYPE_SOLID);
        if (mask & LBB_END)
            lx_guest_store_u16(b + LINEBUNDLE_END_OFFSET, p->gpi.line_end);
        DEFAULTED(LBB_END, p->gpi.line_end == LINEEND_FLAT);
        if (mask & LBB_JOIN)
            lx_guest_store_u16(b + LINEBUNDLE_JOIN_OFFSET, p->gpi.line_join);
        DEFAULTED(LBB_JOIN, p->gpi.line_join == LINEJOIN_BEVEL);
    }
    if (primitive == PRIM_CHAR)
    {
        if (mask & CBB_SET)
            lx_guest_store_u16(b + CHARBUNDLE_SET_OFFSET, p->charset);
        DEFAULTED(CBB_SET, !p->charset);
        if (mask & CBB_MODE)
            lx_guest_store_u16(b + CHARBUNDLE_PRECISION_OFFSET, p->char_mode);
        DEFAULTED(CBB_MODE, p->char_mode <= CM_MODE1);
        if (mask & CBB_BOX)
        {
            lx_guest_store_u32(b + CHARBUNDLE_CELL_OFFSET, p->char_width);
            lx_guest_store_u32(b + CHARBUNDLE_CELL_OFFSET + 4, p->char_height);
        }
        DEFAULTED(CBB_BOX, !p->char_width && !p->char_height);
        if (mask & CBB_ANGLE)
            memcpy(b + CHARBUNDLE_ANGLE_OFFSET, &p->char_angle, sizeof p->char_angle);
        DEFAULTED(CBB_ANGLE, !p->char_angle.x && !p->char_angle.y);
        if (mask & CBB_SHEAR)
            memcpy(b + CHARBUNDLE_SHEAR_OFFSET, &p->char_shear, sizeof p->char_shear);
        DEFAULTED(CBB_SHEAR, !p->char_shear.x && !p->char_shear.y);
        if (mask & CBB_DIRECTION)
            lx_guest_store_u16(b + CHARBUNDLE_DIRECTION_OFFSET, p->char_direction);
        DEFAULTED(CBB_DIRECTION, p->char_direction <= CHDIRN_LEFTRIGHT);
        if (mask & CBB_TEXT_ALIGN)
            lx_guest_store_u16(b + CHARBUNDLE_TEXT_ALIGN_OFFSET,
                               p->text_horizontal | p->text_vertical);
        DEFAULTED(CBB_TEXT_ALIGN, !p->text_horizontal && !p->text_vertical);
        if (mask & CBB_EXTRA)
            lx_guest_store_u32(b + CHARBUNDLE_EXTRA_OFFSET, p->gpi.char_extra);
        DEFAULTED(CBB_EXTRA, !p->gpi.char_extra);
        if (mask & CBB_BREAK_EXTRA)
            lx_guest_store_u32(b + CHARBUNDLE_BREAK_EXTRA_OFFSET, 0);
        defaults |= CBB_BREAK_EXTRA;
    }
    if (primitive == PRIM_MARKER)
    {
        /* Markers have no settable set, symbol or box in this PS. */
        if (mask & MBB_SET)
            lx_guest_store_u16(b + MARKERBUNDLE_SET_OFFSET, 0);
        if (mask & MBB_SYMBOL)
            lx_guest_store_u16(b + MARKERBUNDLE_SYMBOL_OFFSET, 0);
        if (mask & MBB_BOX)
        {
            lx_guest_store_u32(b + MARKERBUNDLE_CELL_OFFSET, 0);
            lx_guest_store_u32(b + MARKERBUNDLE_CELL_OFFSET + 4, 0);
        }
        defaults |= MBB_SET | MBB_SYMBOL | MBB_BOX;
    }
    if (primitive == PRIM_AREA)
    {
        if (mask & ABB_SET)
            lx_guest_store_u16(b + AREABUNDLE_SET_OFFSET, p->pattern_set);
        DEFAULTED(ABB_SET, !p->pattern_set);
        if (mask & ABB_SYMBOL)
            lx_guest_store_u16(b + AREABUNDLE_SYMBOL_OFFSET, p->pattern);
        DEFAULTED(ABB_SYMBOL, p->pattern == PATSYM_SOLID);
        if (mask & ABB_REF_POINT)
            memcpy(b + AREABUNDLE_REF_POINT_OFFSET, &p->pattern_reference,
                   sizeof p->pattern_reference);
        DEFAULTED(ABB_REF_POINT, !p->pattern_reference.x && !p->pattern_reference.y);
    }
#undef DEFAULTED
    return (LONG)(defaults & mask);
}
