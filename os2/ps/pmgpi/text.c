/* Public domain. No rights reserved. Wholly AI-authored. */
/* Character strings and their attributes: GpiCharString*, angle, shear,
   direction and alignment. Positioned strings use explicit character
   advances when CHS_VECTOR is set. */
#include "os2/include/private/export.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/common/pm_text.h"
#include "host/font.h"

LONG GpiCharStringPosAt(HPS handle, PPOINTL start, PRECTL rect, ULONG flags, LONG count, PCH text,
                        PLONG advances)
{
    TRACE_NATIVE("GpiCharStringPosAt(%u, %p, %p, %u, %d, %p, %p)", (uint)handle, start, rect,
                 (uint)flags, (int)count, text, advances);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !start || count < 0 || count > 65536 || (count && !text) || (flags & ~0x61bu) ||
        ((flags & 0x11) && !rect) || ((flags & 2) && count && !advances))
        return 0;
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
        return 0;
    POINTL saved = ps->position, point = *start;
    if ((flags & 1) && !WinFillRect(handle, rect, ps->primitive_back[1]))
        return 0;
    for (LONG i = 0; i < count; i++)
    {
        if (!lx_pm_text(handle, &point, 1, text + i, ps->primitive_color[1],
                        (flags & 16) ? rect : NULL))
        {
            ps->position = saved;
            return 0;
        }
        LONG step =
            (flags & 2) ? advances[i] : lx_host_font_advance(font, lx_pm_codepoint((uint8)text[i]));
        /* Character extra adds to font spacing and to an explicit increment vector. */
        if (ps->gpi.initialized)
            step += (LONG)((ps->gpi.char_extra + (ps->gpi.char_extra < 0 ? -32768LL : 32768LL)) /
                           65536);
        if (step < 0)
            step = 0;
        int64_t next = (int64_t)point.x + step;
        if (next > INT32_MAX)
        {
            ps->position = saved;
            return 0;
        }
        point.x = (LONG)next;
    }
    if (flags & 0x600)
    {
        if (flags & 0x200)
        {
            LONG y = start->y + metrics.underline_position;
            RECTL line = {start->x, y, point.x,
                          y + (metrics.underline_size > 0 ? metrics.underline_size : 1)};
            if (!WinFillRect(handle, &line, ps->primitive_color[1]))
                return 0;
        }
        if (flags & 0x400)
        {
            LONG y = start->y + metrics.strikeout_position;
            RECTL line = {start->x, y, point.x,
                          y + (metrics.strikeout_size > 0 ? metrics.strikeout_size : 1)};
            if (!WinFillRect(handle, &line, ps->primitive_color[1]))
                return 0;
        }
    }
    ps->position = (flags & 8) ? saved : point;
    return 1;
}

LONG GpiCharStringPos(HPS handle, PRECTL rect, ULONG flags, LONG count, PCH text, PLONG advances)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    POINTL start = ps ? ps->position : (POINTL){0, 0};
    return ps ? GpiCharStringPosAt(handle, &start, rect, flags, count, text, advances) : 0;
}

LONG GpiCharStringAt(HPS handle, PPOINTL point, LONG count, PCH text)
{
    TRACE_NATIVE("GpiCharStringAt(%u, %p, %d, %p)", (uint)handle, point, (int)count, text);
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
