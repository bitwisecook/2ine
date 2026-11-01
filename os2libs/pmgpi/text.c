/* Public domain. No rights reserved. Wholly AI-authored. */
/* Positioned byte strings use explicit character advances when CHS_VECTOR is set. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pm_text.h"
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
