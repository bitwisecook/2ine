/* Public domain. No rights reserved. Wholly AI-authored. */
/* The GPI state of a presentation space: its lazy initialisation and
   GpiQueryPS, GpiSetPS and GpiResetPS. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include <math.h>
#include "os2/ps/pmgpi/internal.h"

const LONG gpi_identity[9] = {65536, 0, 0, 0, 65536, 0, 0, 0, 1};

/* The default aperture is square, with sides of the default character cell height. */
void lx_gpi_default_pick(HPS h)
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

PresentationSpace *gpi_space(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (p && !p->gpi.initialized)
    {
        memcpy(p->gpi.model, gpi_identity, sizeof gpi_identity);
        memcpy(p->gpi.view, gpi_identity, sizeof gpi_identity);
        p->gpi.display_enabled = TRUE;
        p->gpi.arc[0] = p->gpi.arc[1] = 1;
        p->gpi.line_width = 1;
        p->gpi.line_end = p->gpi.line_join = 1;
        p->gpi.initialized = TRUE;
        lx_gpi_default_pick(h);
    }
    return p;
}

ULONG GpiQueryPS(HPS h, PSIZEL size)
{
    PresentationSpace *p = gpi_space(h);
    if (!p || !size)
        return 0;
    *size = lx_pm_page_dimensions(p);
    return p->ps_options ? p->ps_options : p->page_units;
}

BOOL GpiSetPS(HPS h, PSIZEL size, ULONG options)
{
    PresentationSpace *p = gpi_space(h);
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
    PresentationSpace *p = gpi_space(h);
    if (!p || !options || options > 7)
        return FALSE;
    /* GRES_SEGMENTS includes GRES_ATTRS, and GRES_ALL includes both. */
    BOOL segments = (options & 6) != 0, all = (options & 4) != 0;
    discard_paths(p, segments);
    memcpy(p->gpi.model, gpi_identity, sizeof gpi_identity);
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
        memcpy(p->gpi.view, gpi_identity, sizeof gpi_identity);
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
