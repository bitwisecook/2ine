/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window drawing excludes visible siblings above it and clip-children areas. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
static BOOL exclude_window(PmRegion *visible, Window *window, const LxHostRect *origin)
{
    if (!(window->style & WS_VISIBLE) || window->w <= 0 || window->h <= 0)
        return TRUE;
    LxHostRect other;
    calcHeavyweightRect(window, &other);
    RECTL rect = {other.x - origin->x, origin->y + origin->h - other.y - other.h,
                  other.x - origin->x + other.w, origin->y + origin->h - other.y};
    PmRegion cut = {0};
    cut.rects = &rect;
    cut.count = 1;
    return regionBoolean(visible, visible, &cut, 7);
}
BOOL lx_pm_window_clip(HPS handle, const RECTL *bounds, PmRegion *visible)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    Window *window = ps ? ps->window : NULL;
    if (!window)
        return FALSE;
    RECTL rect = {0, 0, window->w, window->h};
    if (bounds)
        WinIntersectRect(0, &rect, &rect, (PRECTL)bounds);
    LxHostRect origin;
    calcHeavyweightRect(window, &origin);
    for (Window *w = window->parent; w && w != &desktop_window; w = w->parent)
    {
        LxHostRect parent;
        calcHeavyweightRect(w, &parent);
        RECTL limit = {parent.x - origin.x, origin.y + origin.h - parent.y - parent.h,
                       parent.x - origin.x + parent.w, origin.y + origin.h - parent.y};
        WinIntersectRect(0, &rect, &rect, &limit);
    }
    PmRegion initial = {0};
    initial.rects = &rect;
    initial.count = WinIsRectEmpty(0, &rect) ? 0 : 1;
    if (!regionBoolean(visible, &initial, ps->clip_lease.resource, ps->clip_lease.resource ? 6 : 2))
        return FALSE;
    for (Window *w = window; w->parent && w->parent != &desktop_window; w = w->parent)
        for (Window *above = w->parent->children; above && above != w; above = above->sibling)
            if (!exclude_window(visible, above, &origin))
                goto fail;
    if (window->style & 0x20000000) /* WS_CLIPCHILDREN */
        for (Window *child = window->children; child; child = child->sibling)
            if (!exclude_window(visible, child, &origin))
                goto fail;
    return TRUE;
fail:
    free(visible->rects);
    memset(visible, 0, sizeof *visible);
    return FALSE;
}
