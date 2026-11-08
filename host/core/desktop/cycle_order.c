/* Public domain. No rights reserved. Wholly AI-authored. */
#include "host/core/desktop/cycle_order.h"

static int before(const LxDesktopCycleWindow *a, const LxDesktopCycleWindow *b)
{
    return a->order != b->order     ? a->order < b->order
           : a->member != b->member ? a->member < b->member
                                    : a->window < b->window;
}

int lx_desktop_cycle_next(const LxDesktopCycleWindow *windows, size_t count, uint64_t member,
                          uint32_t window, int reverse, LxDesktopCycleWindow *output)
{
    const LxDesktopCycleWindow *origin = NULL, *edge = NULL, *next = NULL;
    for (size_t i = 0; i < count; i++)
        if (windows[i].member == member && windows[i].window == window)
            origin = &windows[i];
    for (size_t i = 0; i < count; i++)
    {
        const LxDesktopCycleWindow *candidate = &windows[i];
        if (!candidate->member || !candidate->window)
            continue;
        if (!edge || (reverse ? before(edge, candidate) : before(candidate, edge)))
            edge = candidate;
        if (origin && (reverse ? before(candidate, origin) : before(origin, candidate)) &&
            (!next || (reverse ? before(next, candidate) : before(candidate, next))))
            next = candidate;
    }
    if (!next)
        next = edge;
    if (!next || (origin && next == origin))
        return 0;
    *output = *next;
    return 1;
}
