/* Public domain. No rights reserved. Wholly AI-authored. */
/* Stateful GPI geometry and color attributes; matrices retain OS/2 mixed
   FIXED/LONG representation instead of copying host floating-point layouts. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "emulator/abi.h"
#include <math.h>
static const LONG identity[9] = {65536, 0, 0, 0, 65536, 0, 0, 0, 1};

/* The default aperture is square, with sides of the default character cell height. */
static void lx_gpi_default_pick(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    LxHostFontMetrics metrics;
    LONG side = 16;
    LxHostFont *font = lx_pm_font(h);
    if (font && lx_host_font_metrics(font, &metrics) && metrics.ascent + metrics.descent > 0)
        side = metrics.ascent + metrics.descent;
    POINTL corners[2] = {{0, 0}, {side, side}};
    if (!lx_pm_convert_points(h, 5, 4, 2, corners)) /* CVTC_DEVICE to CVTC_PAGE */
        corners[1] = (POINTL){side, side};
    p->gpi.pick_size = (SIZEL){corners[1].x - corners[0].x, corners[1].y - corners[0].y};
}

static PresentationSpace *space(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (p && !p->gpi.initialized)
    {
        memcpy(p->gpi.model, identity, sizeof identity);
        memcpy(p->gpi.view, identity, sizeof identity);
        p->gpi.display_enabled = TRUE;
        p->gpi.arc[0] = p->gpi.arc[1] = 1;
        p->gpi.line_width = 1;
        p->gpi.line_end = p->gpi.line_join = 1;
        p->gpi.initialized = TRUE;
        lx_gpi_default_pick(h);
    }
    return p;
}

static BOOL matrix_set(LONG *out, const LONG *in, LONG options)
{
    if (!in || options < 0 || options > 2 || in[2] || in[5] || in[8] != 1 ||
        (options && (out[2] || out[5] || out[8] != 1)))
        return FALSE;
    LONG result[9];
    if (options == 0)
        memcpy(result, in, sizeof result);
    else
    {
        /* Row vectors apply a first: TRANSFORM_ADD runs the existing matrix first. */
        const LONG *a = options == 1 ? out : in, *b = options == 1 ? in : out;
        double aa = a[0] / 65536.0, ab = a[1] / 65536.0, ac = a[3] / 65536.0, ad = a[4] / 65536.0;
        double ba = b[0] / 65536.0, bb = b[1] / 65536.0, bc = b[3] / 65536.0, bd = b[4] / 65536.0;
        double v[9] = {(aa * ba + ab * bc) * 65536,  (aa * bb + ab * bd) * 65536,  0,
                       (ac * ba + ad * bc) * 65536,  (ac * bb + ad * bd) * 65536,  0,
                       a[6] * ba + a[7] * bc + b[6], a[6] * bb + a[7] * bd + b[7], 1};
        for (unsigned i = 0; i < 9; i++)
        {
            if (!isfinite(v[i]) || v[i] < INT32_MIN || v[i] > INT32_MAX)
                return FALSE;
            result[i] = (LONG)llround(v[i]);
        }
    }
    memcpy(out, result, sizeof result);
    return TRUE;
}

BOOL GpiQueryDefaultViewMatrix(HPS h, LONG count, PVOID out)
{
    PresentationSpace *p = space(h);
    if (!p || !out || count < 0 || count > 9)
        return FALSE;
    if (count)
        memcpy(out, p->gpi.view, count * 4);
    return TRUE;
}

BOOL GpiQueryModelTransformMatrix(HPS h, LONG count, PVOID out)
{
    PresentationSpace *p = space(h);
    if (!p || !out || count < 0 || count > 9)
        return FALSE;
    if (count)
        memcpy(out, p->gpi.model, count * 4);
    return TRUE;
}

BOOL GpiSetModelTransformMatrix(HPS h, LONG count, PVOID in, LONG options)
{
    PresentationSpace *p = space(h);
    if (!p || count < 0 || count > 9 || (count && !in))
        return FALSE;
    LONG m[9];
    memcpy(m, identity, sizeof m);
    if (count)
        memcpy(m, in, count * 4);
    return matrix_set(p->gpi.model, m, options);
}

BOOL lx_gpi_default_matrix(HPS h, LONG count, PVOID in, LONG options)
{
    PresentationSpace *p = space(h);
    if (!p || count < 0 || count > 9 || (count && !in))
        return FALSE;
    LONG m[9];
    memcpy(m, identity, sizeof m);
    if (count)
        memcpy(m, in, count * 4);
    return matrix_set(p->gpi.view, m, options);
}

BOOL GpiTranslate(HPS h, PVOID matrix, LONG options, PPOINTL offset)
{
    /* The matrix helpers accept only TRANSFORM_REPLACE and TRANSFORM_ADD. */
    if (!space(h) || !matrix || !offset || options < 0 || options > 1)
        return FALSE;
    LONG m[9];
    memcpy(m, identity, sizeof m);
    m[6] = offset->x;
    m[7] = offset->y;
    return matrix_set(matrix, m, options);
}

BOOL GpiScale(HPS h, PVOID matrix, LONG options, PLONG factors, PPOINTL center)
{
    if (!space(h) || !matrix || !factors || !center || options < 0 || options > 1)
        return FALSE;
    LONG m[9];
    memcpy(m, identity, sizeof m);
    m[0] = factors[0];
    m[4] = factors[1];
    double x = center->x * (1 - factors[0] / 65536.0), y = center->y * (1 - factors[1] / 65536.0);
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return FALSE;
    m[6] = (LONG)llround(x);
    m[7] = (LONG)llround(y);
    return matrix_set(matrix, m, options);
}

BOOL GpiRotate(HPS h, PVOID matrix, LONG options, LONG angle, PPOINTL center)
{
    if (!space(h) || !matrix || !center || options < 0 || options > 1)
        return FALSE;
    double r = angle / 65536.0 * 3.14159265358979323846 / 180.0, c = cos(r), s = sin(r);
    LONG m[9];
    memcpy(m, identity, sizeof m);
    m[0] = m[4] = (LONG)llround(c * 65536);
    m[1] = (LONG)llround(s * 65536);
    m[3] = -m[1];
    double x = center->x * (1 - c) + center->y * s, y = center->y * (1 - c) - center->x * s;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return FALSE;
    m[6] = (LONG)llround(x);
    m[7] = (LONG)llround(y);
    return matrix_set(matrix, m, options);
}

BOOL GpiSetArcParams(HPS h, PLONG arc)
{
    PresentationSpace *p = space(h);
    if (!p || !arc)
        return FALSE;
    memcpy(p->gpi.arc, arc, 16);
    return TRUE;
}

#define ATTRIBUTE(Name, field, min, max, def)                                                      \
    BOOL GpiSet##Name(HPS h, LONG v)                                                               \
    {                                                                                              \
        PresentationSpace *p = space(h);                                                           \
        if (!p || v < min || v > max)                                                              \
            return FALSE;                                                                          \
        p->gpi.field = v ? v : def;                                                                \
        return TRUE;                                                                               \
    }                                                                                              \
    LONG GpiQuery##Name(HPS h)                                                                     \
    {                                                                                              \
        PresentationSpace *p = space(h);                                                           \
        return p ? p->gpi.field : -1;                                                              \
    }
ATTRIBUTE(LineEnd, line_end, 0, 3, 1)
ATTRIBUTE(LineJoin, line_join, 0, 3, 1)

BOOL GpiSetLineWidthGeom(HPS h, LONG v)
{
    PresentationSpace *p = space(h);
    if (!p || v < 0 || p->record_path)
        return FALSE;
    p->gpi.line_width = v;
    return TRUE;
}

LONG GpiQueryLineWidthGeom(HPS h)
{
    PresentationSpace *p = space(h);
    return p ? p->gpi.line_width : -1;
}

BOOL GpiSetCharExtra(HPS h, LONG v)
{
    PresentationSpace *p = space(h);
    if (!p || p->area_recording)
        return FALSE;
    p->gpi.char_extra = v;
    return TRUE;
}

LONG GpiQueryStopDraw(HPS h)
{
    PresentationSpace *p = space(h);
    return p ? p->gpi.stop_draw : -1;
}

BOOL GpiSetStopDraw(HPS h, LONG v)
{
    PresentationSpace *p = space(h);
    if (!p || v < 0 || v > 1)
        return FALSE;
    p->gpi.stop_draw = v;
    return TRUE;
}

static BOOL rect_valid(PRECTL r)
{
    return r && r->xLeft <= r->xRight && r->yBottom <= r->yTop;
}

/* GRE resets viewing limits to this rectangle, which clips nothing. */
static const RECTL no_limits = {(LONG)0xf8000000, (LONG)0xf8000000, 0x7ffffff, 0x7ffffff};

BOOL GpiSetGraphicsField(HPS h, PRECTL r)
{
    PresentationSpace *p = space(h);
    if (!p)
        return FALSE;
    if (!rect_valid(r))
    {
        WinSetErrorInfo(0x207A, 0); /* PMERR_INV_GRAPHICS_FIELD */
        return FALSE;
    }
    p->gpi.graphics_field = *r;
    p->gpi.graphics_set = TRUE;
    return TRUE;
}

BOOL GpiQueryGraphicsField(HPS h, PRECTL r)
{
    PresentationSpace *p = space(h);
    if (!p || !r)
        return FALSE;
    *r = p->gpi.graphics_set ? p->gpi.graphics_field : (RECTL){0, 0, 0, 0};
    return TRUE;
}

BOOL GpiSetViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = space(h);
    if (!p || !rect_valid(r))
        return FALSE;
    p->gpi.viewing_limits = *r;
    p->gpi.viewing_set = TRUE;
    p->gpi.viewing_custom = TRUE;
    return TRUE;
}

BOOL GpiQueryViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = space(h);
    if (!p || !r)
        return FALSE;
    *r = p->gpi.viewing_set ? p->gpi.viewing_limits : no_limits;
    return TRUE;
}

BOOL GpiSetDefViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = space(h);
    if (!p || !rect_valid(r))
        return FALSE;
    p->gpi.default_limits = *r;
    p->gpi.default_set = TRUE;
    /* A new default applies at once to limits still at their default value. */
    if (!p->gpi.viewing_custom)
    {
        p->gpi.viewing_limits = *r;
        p->gpi.viewing_set = TRUE;
    }
    return TRUE;
}

BOOL GpiQueryDefViewingLimits(HPS h, PRECTL r)
{
    PresentationSpace *p = space(h);
    if (!p || !r)
        return FALSE;
    *r = p->gpi.default_set ? p->gpi.default_limits : no_limits;
    return TRUE;
}

BOOL GpiSetPickAperturePosition(HPS h, PPOINTL v)
{
    PresentationSpace *p = space(h);
    if (!p || !v)
        return FALSE;
    p->gpi.pick_position = *v;
    return TRUE;
}

BOOL GpiQueryPickAperturePosition(HPS h, PPOINTL v)
{
    PresentationSpace *p = space(h);
    if (!p || !v)
        return FALSE;
    *v = p->gpi.pick_position;
    return TRUE;
}

BOOL GpiSetPickApertureSize(HPS h, LONG options, PSIZEL v)
{
    PresentationSpace *p = space(h);
    if (!p || (options != 0 && options != 2))
        return FALSE;
    if (!options)
    {
        lx_gpi_default_pick(h);
        return TRUE;
    }
    if (!v || v->cx < 0 || v->cy < 0)
        return FALSE;
    p->gpi.pick_size = *v;
    return TRUE;
}

BOOL GpiQueryPickApertureSize(HPS h, PSIZEL v)
{
    PresentationSpace *p = space(h);
    if (!p || !v)
        return FALSE;
    *v = p->gpi.pick_size;
    return TRUE;
}

BOOL GpiResetBoundaryData(HPS h)
{
    PresentationSpace *p = space(h);
    if (!p)
        return FALSE;
    p->gpi.boundary_set = FALSE;
    return TRUE;
}

ULONG GpiQueryPS(HPS h, PSIZEL size)
{
    PresentationSpace *p = space(h);
    if (!p || !size)
        return 0;
    *size = lx_pm_page_dimensions(p);
    return p->ps_options ? p->ps_options : p->page_units;
}

BOOL GpiSetPS(HPS h, PSIZEL size, ULONG options)
{
    PresentationSpace *p = space(h);
    if (!p || !size || size->cx < 0 || size->cy < 0 ||
        ((options & 0xf00) && (options & 0xf00) != 0x200) || ((options & 0xfc) > 0x1c))
        return FALSE;
    p->page_size = *size;
    p->page_units = options & 0x1c;
    p->ps_options = (p->ps_options & ~0xffcu) | (options & 0xffc);
    return (options & 0x8000) ? TRUE : GpiResetPS(h, 4);
}

static void discard_paths(PresentationSpace *p, BOOL all)
{
    PmPath *open = p->record_path;
    p->record_path = NULL;
    p->area_recording = FALSE;
    for (PmPath **link = &p->paths; *link;)
    {
        PmPath *x = *link;
        if (!all && x != open)
        {
            link = &x->next;
            continue;
        }
        *link = x->next;
        free(x->points);
        free(x);
    }
}

BOOL GpiResetPS(HPS h, ULONG options)
{
    PresentationSpace *p = space(h);
    if (!p || !options || options > 7)
        return FALSE;
    /* GRES_SEGMENTS includes GRES_ATTRS, and GRES_ALL includes both. */
    BOOL segments = (options & 6) != 0, all = (options & 4) != 0;
    discard_paths(p, segments);
    memcpy(p->gpi.model, identity, sizeof identity);
    p->gpi.arc[0] = p->gpi.arc[1] = 1;
    p->gpi.arc[2] = p->gpi.arc[3] = 0;
    p->gpi.line_end = p->gpi.line_join = 1;
    p->gpi.line_width = 1;
    p->gpi.char_extra = 0;
    p->position = (POINTL){0, 0};
    p->color = CLR_BLACK;
    p->back_color = CLR_WHITE;
    p->mix = 2;
    p->back_mix = 5;
    for (unsigned i = 0; i < 5; i++)
    {
        p->primitive_color[i] = CLR_BLACK;
        p->primitive_back[i] = CLR_WHITE;
        p->primitive_mix[i] = 2;
        p->primitive_back_mix[i] = 5;
    }
    p->line_type = 7;
    p->pattern_set = 0;
    p->pattern = 16;
    p->pattern_reference = (POINTL){0, 0};
    p->charset = p->char_mode = p->char_width = p->char_height = 0;
    p->char_angle = p->char_shear = (POINTL){0, 0};
    p->char_direction = p->text_horizontal = p->text_vertical = 0;
    if (segments)
    {
        memcpy(p->gpi.view, identity, sizeof identity);
        p->gpi.graphics_set = p->gpi.default_set = p->gpi.boundary_set = FALSE;
        p->gpi.boundary_enabled = p->gpi.correlate = p->gpi.pick_hit = FALSE;
        p->gpi.display_enabled = TRUE;
        ULONG clip = 0;
        if (GpiQueryClipRegion(h) && GpiSetClipRegion(h, 0, &clip) && clip)
            GpiDestroyRegion(h, clip);
    }
    p->gpi.viewing_limits = p->gpi.default_limits;
    p->gpi.viewing_set = p->gpi.default_set;
    p->gpi.viewing_custom = FALSE;
    if (all)
    {
        for (LONG id = 1; id < 255; id++)
            if (p->logical_fonts[id].defined || p->bitmap_ids[id].resource)
                GpiDeleteSetId(h, id);
        GpiSelectPalette(h, 0);
        memset(p->color_valid, 0, sizeof p->color_valid);
        p->rgb_colors = FALSE;
        p->color_format = 0;
        p->gpi.pick_position = (POINTL){0, 0};
        lx_gpi_default_pick(h);
    }
    return TRUE;
}

LONG GpiQueryPel(HPS h, PPOINTL point)
{
    PresentationSpace *p = space(h);
    if (!p || !point)
        return -1;
    POINTL v = *point;
    uint32_t rgb;
    if (!lx_pm_transform_point(p, &v, TRUE) || !lx_pm_clip_point(h, v.x, v.y) ||
        !lx_pm_raster_read(h, v.x, v.y, 1, 1, &rgb))
        return -1;
    rgb &= 0xffffff;
    if (p->rgb_colors)
        return (LONG)rgb;
    LONG index = GpiQueryColorIndex(h, 0, (LONG)rgb);
    return lx_pm_resolve_color(h, index) == (LONG)rgb ? index : -254; /* CLR_NOINDEX */
}

LONG GpiQueryNearestColor(HPS h, ULONG options, LONG rgb)
{
    return space(h) && !(options & ~1u) && rgb >= 0 && rgb <= 0xffffff ? rgb : -1;
}

LONG GpiQueryRealColors(HPS h, ULONG options, LONG first, LONG count, PLONG colors)
{
    if (!space(h) || (options & ~3u) || first < 0 || count < 0 || (count && !colors))
        return -1;
    BOOL pairs = (options & 2) != 0;
    LONG n = 0;
    for (LONG i = first; i < 256 && n + (pairs ? 2 : 1) <= count; i++)
    {
        LONG c = GpiQueryRGBColor(h, 0, i);
        if (c < 0)
            break;
        if (pairs)
            colors[n++] = i;
        colors[n++] = c;
    }
    return n;
}

LONG GpiQueryAttrs(HPS h, LONG primitive, ULONG mask, PVOID output)
{
    PresentationSpace *p = space(h);
    if (!p || primitive < 1 || primitive > 5 || (mask && !output))
        return -1;
    if (!mask)
        return 0;
    uint8 *b = output;
    unsigned k = primitive - 1;
    ULONG defaults = 0;
#define DEFAULTED(bit, test)                                                                       \
    if (test)                                                                                      \
    defaults |= (bit)
    if (mask & 1)
        lx_guest_store_u32(b, p->primitive_color[k]);
    DEFAULTED(1, p->primitive_color[k] == CLR_BLACK);
    if (mask & 2)
        lx_guest_store_u32(b + 4, p->primitive_back[k]);
    DEFAULTED(2, p->primitive_back[k] == CLR_WHITE);
    if (mask & 4)
        lx_guest_store_u16(b + 8, p->primitive_mix[k]);
    DEFAULTED(4, p->primitive_mix[k] == 2);
    if (mask & 8)
        lx_guest_store_u16(b + 10, p->primitive_back_mix[k]);
    DEFAULTED(8, p->primitive_back_mix[k] == 5);
    if (primitive == 1)
    {
        if (mask & 16)
            lx_guest_store_u32(b + 12, 65536);
        defaults |= 16;
        if (mask & 32)
            lx_guest_store_u32(b + 16, p->gpi.line_width);
        DEFAULTED(32, p->gpi.line_width == 1);
        if (mask & 64)
            lx_guest_store_u16(b + 20, p->line_type);
        DEFAULTED(64, p->line_type == 7);
        if (mask & 128)
            lx_guest_store_u16(b + 22, p->gpi.line_end);
        DEFAULTED(128, p->gpi.line_end == 1);
        if (mask & 256)
            lx_guest_store_u16(b + 24, p->gpi.line_join);
        DEFAULTED(256, p->gpi.line_join == 1);
    }
    if (primitive == 2)
    {
        if (mask & 16)
            lx_guest_store_u16(b + 12, p->charset);
        DEFAULTED(16, !p->charset);
        if (mask & 32)
            lx_guest_store_u16(b + 14, p->char_mode);
        DEFAULTED(32, p->char_mode <= 1);
        if (mask & 64)
        {
            lx_guest_store_u32(b + 16, p->char_width);
            lx_guest_store_u32(b + 20, p->char_height);
        }
        DEFAULTED(64, !p->char_width && !p->char_height);
        if (mask & 128)
            memcpy(b + 24, &p->char_angle, 8);
        DEFAULTED(128, !p->char_angle.x && !p->char_angle.y);
        if (mask & 256)
            memcpy(b + 32, &p->char_shear, 8);
        DEFAULTED(256, !p->char_shear.x && !p->char_shear.y);
        if (mask & 512)
            lx_guest_store_u16(b + 40, p->char_direction);
        DEFAULTED(512, p->char_direction <= 1);
        if (mask & 1024)
            lx_guest_store_u16(b + 42, p->text_horizontal | p->text_vertical);
        DEFAULTED(1024, !p->text_horizontal && !p->text_vertical);
        if (mask & 2048)
            lx_guest_store_u32(b + 44, p->gpi.char_extra);
        DEFAULTED(2048, !p->gpi.char_extra);
        if (mask & 4096)
            lx_guest_store_u32(b + 48, 0);
        defaults |= 4096;
    }
    if (primitive == 3)
    {
        /* Markers have no settable set, symbol or box in this PS. */
        if (mask & 16)
            lx_guest_store_u16(b + 12, 0);
        if (mask & 32)
            lx_guest_store_u16(b + 14, 0);
        if (mask & 64)
        {
            lx_guest_store_u32(b + 16, 0);
            lx_guest_store_u32(b + 20, 0);
        }
        defaults |= 16 | 32 | 64;
    }
    if (primitive == 4)
    {
        if (mask & 16)
            lx_guest_store_u16(b + 12, p->pattern_set);
        DEFAULTED(16, !p->pattern_set);
        if (mask & 32)
            lx_guest_store_u16(b + 14, p->pattern);
        DEFAULTED(32, p->pattern == 16);
        if (mask & 64)
            memcpy(b + 16, &p->pattern_reference, 8);
        DEFAULTED(64, !p->pattern_reference.x && !p->pattern_reference.y);
    }
#undef DEFAULTED
    return (LONG)(defaults & mask);
}

ULONG GpiCreatePalette(HAB hab, ULONG options, ULONG format, ULONG count, PULONG colors)
{
    /* Only LCOL_PURECOLOR and LCOL_OVERRIDE_DEFAULT_COLORS are defined, with LCOLF_CONSECRGB. */
    if (!lxHostThreadState()->anchor_block || (options & ~0xcu) || format != 2 || !count ||
        count > 16 * 1024 * 1024 || !colors)
        return 0;
    PmPalette *p = calloc(1, sizeof *p + (size_t)count * sizeof *p->colors);
    if (!p)
        return 0;
    p->count = count;
    for (ULONG i = 0; i < count; i++)
        p->colors[i] = colors[i];
    ULONG h = lx_handle_create(LX_HANDLE_PM_PALETTE, p, free);
    if (!h)
        free(p);
    return h;
}

BOOL GpiDeletePalette(ULONG h)
{
    int closed = lx_handle_close_guarded(h, LX_HANDLE_PM_PALETTE, 0, NULL);
    if (closed != 1)
        WinSetErrorInfo(closed < 0 ? 0x210F : 0x2111, 0); /* PALETTE_SELECTED, INV_HPAL */
    return closed == 1;
}

ULONG GpiSelectPalette(HPS h, ULONG palette)
{
    PresentationSpace *p = space(h);
    LxHandleBorrow b = {0};
    if (!p || (palette && !lx_handle_borrow(palette, LX_HANDLE_PM_PALETTE, &b)))
        return 0xffffffff;
    ULONG old = p->gpi.palette;
    lx_handle_release(&p->palette_lease);
    p->palette_lease = b;
    p->gpi.palette = palette;
    /* A palette overrides any loaded table; deselecting restores the default table. */
    if (palette || old)
    {
        memset(p->color_valid, 0, sizeof p->color_valid);
        p->rgb_colors = FALSE;
        p->color_format = palette ? 4 : 0;
    }
    return old;
}

ULONG GpiQueryPalette(HPS h)
{
    PresentationSpace *p = space(h);
    return p ? p->gpi.palette : 0xffffffff;
}

LONG GpiQueryPaletteInfo(ULONG handle, HPS h, ULONG options, ULONG start, ULONG count,
                         PULONG colors)
{
    LxHandleBorrow b = {0};
    if ((options & ~2u) || (count && !colors))
        return -1;
    PmPalette *p = NULL;
    if (handle)
    {
        if (!lx_handle_borrow(handle, LX_HANDLE_PM_PALETTE, &b))
            return -1;
        p = b.resource;
    }
    else
    {
        PresentationSpace *ps = space(h);
        if (!ps)
            return -1;
        p = ps->palette_lease.resource;
    }
    LONG n = 0;
    if (p)
    {
        BOOL pairs = (options & 2) != 0;
        ULONG available = start < p->count ? p->count - start : 0;
        if (!count)
            n = (LONG)(available * (pairs ? 2 : 1));
        else
            for (ULONG i = start; i < p->count && (ULONG)n + (pairs ? 2 : 1) <= count; i++)
            {
                if (pairs)
                    colors[n++] = i;
                colors[n++] = p->colors[i];
            }
    }
    lx_handle_release(&b);
    return n;
}
