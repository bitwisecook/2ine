/* Public domain. No rights reserved. Wholly AI-authored. */
/* GPI attributes and saved-state stack belong to each presentation space. */
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "guest/abi.h"
BOOL GpiSetMix(HPS handle, LONG mode)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || mode < 0 || mode > 17 || mode == 3)
        return FALSE;
    ps->mix = mode ? mode : 2;
    for (unsigned i = 0; i < 5; i++)
        ps->primitive_mix[i] = ps->mix;
    return TRUE;
}
BOOL GpiSetBackMix(HPS handle, LONG mode)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || mode < 0 || mode > 19 || mode == 3)
        return FALSE;
    ps->back_mix = mode ? mode : 5;
    for (unsigned i = 0; i < 5; i++)
        ps->primitive_back_mix[i] = ps->back_mix;
    return TRUE;
}
LONG GpiQueryMix(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->mix : -1;
}
LONG GpiQueryBackMix(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->back_mix : -1;
}
BOOL GpiSetBackColor(HPS handle, LONG color)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || lx_pm_resolve_color(handle, color) < 0)
        return FALSE;
    ps->back_color = color;
    for (unsigned i = 0; i < 5; i++)
        ps->primitive_back[i] = color;
    return TRUE;
}
LONG GpiQueryBackColor(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->back_color : -255;
}
BOOL GpiSetLineType(HPS handle, LONG type)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || type < 0 || type > 9)
        return FALSE;
    ps->line_type = type ? type : 7;
    return TRUE;
}
LONG GpiQueryLineType(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->line_type : -1;
}
LONG GpiSavePS(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || ps->save_id == INT32_MAX)
        return -1;
    PmSavedSpace *s = calloc(1, sizeof *s);
    if (!s)
        return -1;
    s->id = ++ps->save_id;
    s->position = ps->position;
    s->color = ps->color;
    s->back_color = ps->back_color;
    s->mix = ps->mix;
    s->back_mix = ps->back_mix;
    s->line_type = ps->line_type;
    s->pattern_set = ps->pattern_set;
    s->pattern = ps->pattern;
    memcpy(s->primitive_color, ps->primitive_color, sizeof s->primitive_color);
    memcpy(s->primitive_back, ps->primitive_back, sizeof s->primitive_back);
    memcpy(s->primitive_mix, ps->primitive_mix, sizeof s->primitive_mix);
    memcpy(s->primitive_back_mix, ps->primitive_back_mix, sizeof s->primitive_back_mix);
    s->charset = ps->charset;
    s->char_width = ps->char_width;
    s->char_height = ps->char_height;
    s->viewport = ps->viewport;
    s->rgb_colors = ps->rgb_colors;
    memcpy(s->colors, ps->colors, sizeof s->colors);
    memcpy(s->color_valid, ps->color_valid, sizeof s->color_valid);
    s->next = ps->saved;
    ps->saved = s;
    return s->id;
}
BOOL GpiRestorePS(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id == 0 || id < -1)
        return FALSE;
    PmSavedSpace *s = ps->saved;
    while (s && id != -1 && s->id != id)
        s = s->next;
    if (!s)
        return FALSE;
    ps->position = s->position;
    ps->color = s->color;
    ps->back_color = s->back_color;
    ps->mix = s->mix;
    ps->back_mix = s->back_mix;
    ps->line_type = s->line_type;
    ps->pattern_set = s->pattern_set;
    ps->pattern = s->pattern;
    memcpy(ps->primitive_color, s->primitive_color, sizeof s->primitive_color);
    memcpy(ps->primitive_back, s->primitive_back, sizeof s->primitive_back);
    memcpy(ps->primitive_mix, s->primitive_mix, sizeof s->primitive_mix);
    memcpy(ps->primitive_back_mix, s->primitive_back_mix, sizeof s->primitive_back_mix);
    ps->charset = s->charset;
    ps->char_width = s->char_width;
    ps->char_height = s->char_height;
    ps->viewport = s->viewport;
    ps->rgb_colors = s->rgb_colors;
    memcpy(ps->colors, s->colors, sizeof s->colors);
    memcpy(ps->color_valid, s->color_valid, sizeof s->color_valid);
    PmSavedSpace *end = s->next;
    while (ps->saved != end)
    {
        s = ps->saved;
        ps->saved = s->next;
        free(s);
    }
    return TRUE;
}
BOOL GpiSetPageViewport(HPS handle, PRECTL rect)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect)
        return FALSE;
    ps->viewport = *rect;
    return TRUE;
}
BOOL GpiQueryPageViewport(HPS handle, PRECTL rect)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect)
        return FALSE;
    *rect = ps->viewport;
    return TRUE;
}
BOOL GpiSetAttrs(HPS handle, LONG primitive, ULONG mask, ULONG defaults, PVOID attrs)
{
    TRACE_NATIVE("GpiSetAttrs(%u, %d, %u, %u, %p)", (uint)handle, (int)primitive, (uint)mask,
                 (uint)defaults, attrs);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || primitive < 1 || primitive > 5 || (mask & ~defaults && !attrs))
        return FALSE;
    uint8 zero[52] = {0};
    const uint8 *p = attrs ? attrs : zero;
    ULONG supported = primitive == 2 ? 0xff : primitive == 1 ? 0x1ff : primitive == 4 ? 0x7f : 15;
    if (mask & ~supported)
        return FALSE;
    /* Current renderer accepts horizontal character axes and unit-width lines. */
    if (primitive == 2)
    {
        if ((mask & 0x20) && !(defaults & 0x20))
        {
            unsigned mode = lx_guest_load_u16(p + 14);
            if (mode < 1 || mode > 3)
                return FALSE;
        }
        if ((mask & 0x80) && !(defaults & 0x80))
        {
            LONG x = (LONG)lx_guest_load_u32(p + 24), y = (LONG)lx_guest_load_u32(p + 28);
            if (x <= 0 || y != 0)
                return FALSE;
        }
        if ((mask & 0x10) &&
            !GpiSetCharSet(handle, (defaults & 0x10) ? 0 : lx_guest_load_u16(p + 12)))
            return FALSE;
        if (mask & 0x40)
        {
            SIZEF box = {16 * 65536, 16 * 65536};
            if (!(defaults & 0x40))
            {
                box.cx = (LONG)lx_guest_load_u32(p + 16);
                box.cy = (LONG)lx_guest_load_u32(p + 20);
            }
            if (!GpiSetCharBox(handle, &box))
                return FALSE;
        }
    }
    if (primitive == 1)
    {
        if ((mask & 0x10) && !(defaults & 0x10) && lx_guest_load_u32(p + 12) != 65536)
            return FALSE;
        if ((mask & 0x20) && !(defaults & 0x20) && lx_guest_load_u32(p + 16) != 1)
            return FALSE;
        if ((mask & 0x80) && !(defaults & 0x80) && lx_guest_load_u16(p + 22) > 3)
            return FALSE;
        if ((mask & 0x100) && !(defaults & 0x100) && lx_guest_load_u16(p + 24) > 3)
            return FALSE;
        if ((mask & 0x40) &&
            !GpiSetLineType(handle, (defaults & 0x40) ? 0 : lx_guest_load_u16(p + 20)))
            return FALSE;
    }
    if (primitive == 4)
    {
        if ((mask & 0x10) &&
            !GpiSetPatternSet(handle, (defaults & 0x10) ? 0 : lx_guest_load_u16(p + 12)))
            return FALSE;
        if ((mask & 0x20) &&
            !GpiSetPattern(handle, (defaults & 0x20) ? 0 : lx_guest_load_u16(p + 14)))
            return FALSE;
        if ((mask & 0x40) && !(defaults & 0x40) &&
            (lx_guest_load_u32(p + 16) || lx_guest_load_u32(p + 20)))
            return FALSE;
    }
    unsigned index = (unsigned)primitive - 1;
    LONG color = (defaults & 1) ? CLR_BLACK : (LONG)lx_guest_load_u32(p);
    LONG back = (defaults & 2) ? CLR_WHITE : (LONG)lx_guest_load_u32(p + 4);
    LONG mix = (defaults & 4) ? 2 : lx_guest_load_u16(p + 8);
    LONG back_mix = (defaults & 8) ? 5 : lx_guest_load_u16(p + 10);
    if ((mask & 1) && lx_pm_resolve_color(handle, color) < 0)
        return FALSE;
    if ((mask & 2) && lx_pm_resolve_color(handle, back) < 0)
        return FALSE;
    if ((mask & 4) && (mix < 1 || mix > 17 || mix == 3))
        return FALSE;
    if ((mask & 8) && (back_mix < 1 || back_mix > 19 || back_mix == 3))
        return FALSE;
    if (mask & 1)
        ps->primitive_color[index] = color;
    if (mask & 2)
        ps->primitive_back[index] = back;
    if (mask & 4)
        ps->primitive_mix[index] = mix;
    if (mask & 8)
        ps->primitive_back_mix[index] = back_mix;
    return TRUE;
}

LONG GpiQueryPatternSet(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->pattern_set : -1;
}
BOOL GpiSetPatternSet(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < 0 || id > 254 || (id && !ps->bitmap_ids[id].resource))
        return FALSE;
    ps->pattern_set = id;
    return TRUE;
}
LONG GpiQueryPattern(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->pattern : -1;
}
BOOL GpiSetPattern(HPS handle, LONG symbol)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || symbol < 0 || (symbol > 19 && symbol != 64))
        return FALSE;
    ps->pattern = symbol ? symbol : 16;
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
