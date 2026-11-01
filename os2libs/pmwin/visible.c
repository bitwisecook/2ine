/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window styles control sibling and child clipping. */
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
        if ((w->style | w->class_style) & WS_CLIPSIBLINGS)
            for (Window *above = w->parent->children; above && above != w; above = above->sibling)
                if (!exclude_window(visible, above, &origin))
                    goto fail;
    if ((window->style | window->class_style) & WS_CLIPCHILDREN)
        for (Window *child = window->children; child; child = child->sibling)
            if (!exclude_window(visible, child, &origin))
                goto fail;
    return TRUE;
fail:
    free(visible->rects);
    memset(visible, 0, sizeof *visible);
    return FALSE;
}

LONG lx_pm_visible_box(HPS handle, RECTL *rectangle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rectangle)
        return 0;
    PmRegion visible = {0};
    BOOL result;
    if (ps->window)
        result = lx_pm_window_clip(handle, NULL, &visible);
    else
    {
        PmBitmap *bitmap = ps->bitmap_lease.resource;
        RECTL bounds =
            bitmap ? (RECTL){0, 0, (LONG)bitmap->width, (LONG)bitmap->height} : ps->viewport;
        PmRegion source = {.rects = &bounds, .count = WinIsRectEmpty(0, &bounds) ? 0 : 1};
        result = regionBoolean(&visible, &source, ps->clip_lease.resource,
                               ps->clip_lease.resource ? 6 : 2);
    }
    if (!result)
    {
        free(visible.rects);
        return 0;
    }
    *rectangle = (RECTL){0};
    for (size_t i = 0; i < visible.count; i++)
        WinUnionRect(0, rectangle, rectangle, &visible.rects[i]);
    LONG type = !visible.count ? 1 : visible.count == 1 ? 2 : 3;
    free(visible.rects);
    return type;
}

void notifyVisibleRegions(AnchorBlock *anchor, Window *window)
{
    if (window->visible_region_notify)
    {
        BOOL visible = TRUE;
        for (Window *w = window; w && w != &desktop_window; w = w->parent)
            visible = visible && (w->style & WS_VISIBLE) != 0;
        postTimestampedMsg(anchor, window->hwnd, visible ? WM_VRNENABLED : WM_VRNDISABLED, 0, 0,
                           currentSystemTicks());
    }
    for (Window *child = window->children; child; child = child->sibling)
        notifyVisibleRegions(anchor, child);
}
BOOL WinSetVisibleRegionNotify(HWND handle, BOOL enable)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || window == &desktop_window)
        return FALSE;
    window->visible_region_notify = enable != 0;
    if (enable)
        notifyVisibleRegions(anchor, window);
    return TRUE;
}
ULONG WinQueryVisibleRegion(HWND handle, ULONG region)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    HPS ps = WinGetPS(handle);
    if (!ps)
        return FALSE;
    PmRegion visible = {0};
    BOOL shown = TRUE;
    for (Window *w = window; w && w != &desktop_window; w = w->parent)
        shown = shown && (w->style & WS_VISIBLE) != 0;
    ULONG result = 0;
    if (!shown || lx_pm_window_clip(ps, NULL, &visible))
        result = (ULONG)lx_pm_region_set(ps, region, (LONG)visible.count, visible.rects);
    free(visible.rects);
    WinReleasePS(ps);
    return result;
}
