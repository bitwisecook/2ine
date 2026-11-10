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
#include "os2/ps/pmgpi/internal.h"

/* Bounds the host work for one string. */
enum
{
    MAX_STRING_CHARS = 65536
};

LONG GpiCharStringPosAt(HPS handle, PPOINTL start, PRECTL rect, ULONG flags, LONG count, PCH text,
                        PLONG advances)
{
    TRACE_NATIVE("GpiCharStringPosAt(%u, %p, %p, %u, %d, %p, %p)", (uint)handle, start, rect,
                 (uint)flags, (int)count, text, advances);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !start || count < 0 || count > MAX_STRING_CHARS || (count && !text) ||
        (flags &
         ~(CHS_OPAQUE | CHS_VECTOR | CHS_LEAVEPOS | CHS_CLIP | CHS_UNDERSCORE | CHS_STRIKEOUT)) ||
        ((flags & (CHS_OPAQUE | CHS_CLIP)) && !rect) ||
        ((flags & CHS_VECTOR) && count && !advances))
        return GPI_ERROR;
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
        return GPI_ERROR;
    const unsigned chars = GPI_PRIM_INDEX(PRIM_CHAR);
    POINTL saved = ps->position, point = *start;
    if ((flags & CHS_OPAQUE) && !WinFillRect(handle, rect, ps->primitive_back[chars]))
        return GPI_ERROR;
    for (LONG i = 0; i < count; i++)
    {
        if (!lx_pm_text(handle, &point, 1, text + i, ps->primitive_color[chars],
                        (flags & CHS_CLIP) ? rect : NULL))
        {
            ps->position = saved;
            return GPI_ERROR;
        }
        LONG step = (flags & CHS_VECTOR)
                        ? advances[i]
                        : lx_host_font_advance(font, lx_pm_codepoint((uint8)text[i]));
        /* Character extra adds to font spacing and to an explicit increment vector. It is a
           FIXED value, rounded half away from zero. */
        if (ps->gpi.initialized)
            step += (LONG)((ps->gpi.char_extra + (ps->gpi.char_extra < 0 ? -(GPI_FIXED_ONE / 2LL)
                                                                         : GPI_FIXED_ONE / 2LL)) /
                           GPI_FIXED_ONE);
        if (step < 0)
            step = 0;
        int64_t next = (int64_t)point.x + step;
        if (next > INT32_MAX)
        {
            ps->position = saved;
            return GPI_ERROR;
        }
        point.x = (LONG)next;
    }
    if (flags & (CHS_UNDERSCORE | CHS_STRIKEOUT))
    {
        if (flags & CHS_UNDERSCORE)
        {
            LONG y = start->y + metrics.underline_position;
            RECTL line = {start->x, y, point.x,
                          y + (metrics.underline_size > 0 ? metrics.underline_size : 1)};
            if (!WinFillRect(handle, &line, ps->primitive_color[chars]))
                return GPI_ERROR;
        }
        if (flags & CHS_STRIKEOUT)
        {
            LONG y = start->y + metrics.strikeout_position;
            RECTL line = {start->x, y, point.x,
                          y + (metrics.strikeout_size > 0 ? metrics.strikeout_size : 1)};
            if (!WinFillRect(handle, &line, ps->primitive_color[chars]))
                return GPI_ERROR;
        }
    }
    ps->position = (flags & CHS_LEAVEPOS) ? saved : point;
    return GPI_OK;
}

LONG GpiCharStringPos(HPS handle, PRECTL rect, ULONG flags, LONG count, PCH text, PLONG advances)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    POINTL start = ps ? ps->position : (POINTL){0, 0};
    return ps ? GpiCharStringPosAt(handle, &start, rect, flags, count, text, advances) : GPI_ERROR;
}

LONG GpiCharStringAt(HPS handle, PPOINTL point, LONG count, PCH text)
{
    TRACE_NATIVE("GpiCharStringAt(%u, %p, %d, %p)", (uint)handle, point, (int)count, text);
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? lx_pm_text(handle, point, count, text,
                           ps->primitive_color[GPI_PRIM_INDEX(PRIM_CHAR)], NULL)
              : GPI_ERROR;
}

LONG GpiCharString(HPS handle, LONG count, PCH text)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    POINTL point = ps ? ps->position : (POINTL){0, 0};
    return ps ? GpiCharStringAt(handle, &point, count, text) : GPI_ERROR;
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
    if (!ps || direction < CHDIRN_DEFAULT || direction > CHDIRN_LEFTRIGHT)
        return FALSE;
    ps->char_direction = direction;
    return TRUE;
}

LONG GpiQueryCharDirection(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    return ps ? ps->char_direction : CHDIRN_ERROR;
}

BOOL GpiSetTextAlignment(HPS handle, LONG horizontal, LONG vertical)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    /* Default/normal/left/standard horizontal and baseline vertical modes
       share the renderer's left-to-right baseline origin. */
    if (!ps ||
        (horizontal != 0 && horizontal != TA_NORMAL_HORIZ && horizontal != TA_LEFT &&
         horizontal != TA_STANDARD_HORIZ) ||
        (vertical != 0 && vertical != TA_NORMAL_VERT && vertical != TA_BASE &&
         vertical != TA_STANDARD_VERT))
        return FALSE;
    ps->text_horizontal = horizontal;
    ps->text_vertical = vertical;
    return TRUE;
}
