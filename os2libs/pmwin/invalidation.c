/* Public domain. No rights reserved. Wholly AI-authored. */
/* Damage persists until validated; the message loop synthesizes paint requests. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/paint.h"
#include "os2libs/pmwin/graphics.h"

static void remove_paint(AnchorBlock *anchor, HWND handle)
{
    MessageQueueItem *previous = NULL, *item = anchor->message_queue.head;
    while (item)
    {
        MessageQueueItem *next = item->next;
        if (item->qmsg.hwnd == handle && item->qmsg.msg == WM_PAINT)
        {
            if (previous)
                previous->next = next;
            else
                anchor->message_queue.head = next;
            if (anchor->message_queue.tail == item)
                anchor->message_queue.tail = previous;
            item->next = anchor->message_queue.free_pool;
            anchor->message_queue.free_pool = item;
        }
        else
            previous = item;
        item = next;
    }
}

void validateWindowPaint(AnchorBlock *anchor, Window *window)
{
    free(window->damage_rects);
    window->damage_rects = NULL;
    window->damage_count = 0;
    window->invalid = (RECTL){0, 0, 0, 0};
    window->paint_pending = FALSE;
    window->paint_offered = FALSE;
    remove_paint(anchor, window->hwnd);
}

BOOL windowAcceptsUpdates(const Window *window)
{
    for (; window && window != &desktop_window; window = window->parent)
        if (!(window->style & WS_VISIBLE))
            return FALSE;
    return TRUE;
}

static void suspend_paint(AnchorBlock *anchor, Window *window)
{
    window->paint_pending = FALSE;
    window->paint_offered = FALSE;
    remove_paint(anchor, window->hwnd);
    for (Window *child = window->children; child; child = child->sibling)
        suspend_paint(anchor, child);
}

BOOL WinEnableWindowUpdate(HWND handle, BOOL enable)
{
    TRACE_NATIVE("WinEnableWindowUpdate(%u, %u)", (uint)handle, (uint)enable);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    if (enable != FALSE && enable != TRUE)
    {
        lxHostThreadState()->pm_error = PMERR_INVALID_FLAG;
        return FALSE;
    }
    BOOL was_visible = (window->style & WS_VISIBLE) != 0;
    if (enable)
        window->style |= WS_VISIBLE;
    else
        window->style &= ~WS_VISIBLE;
    if (was_visible != enable)
        sendMessage(window, WM_SHOW, (MPARAM)(uintptr_t)enable, 0);
    if (enable)
        return TRUE;
    /* Descendants retain their damage until updates are enabled again. */
    suspend_paint(anchor, window);
    return TRUE;
}

BOOL WinInvalidateRect(HWND handle, PRECTL rectangle, BOOL children)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    RECTL bounds = {0, 0, window->w, window->h}, damaged;
    if (!WinIntersectRect(0, &damaged, &bounds, rectangle ? rectangle : &bounds))
        return TRUE;
    PmRegion prior = {.rects = window->damage_rects, .count = window->damage_count};
    if (!prior.count && !WinIsRectEmpty(0, &window->invalid))
    {
        prior.rects = &window->invalid;
        prior.count = 1;
    }
    PmRegion input = {.rects = &damaged, .count = 1}, combined = {0};
    if (!regionBoolean(&combined, &prior, &input, 1))
        return FALSE;
    free(window->damage_rects);
    window->damage_rects = combined.rects;
    window->damage_count = combined.count;
    BOOL expanded = WinIsRectEmpty(0, &window->invalid) || damaged.xLeft < window->invalid.xLeft ||
                    damaged.yBottom < window->invalid.yBottom ||
                    damaged.xRight > window->invalid.xRight || damaged.yTop > window->invalid.yTop;
    BOOL was_pending = window->paint_pending;
    WinUnionRect(0, &window->invalid, &window->invalid, &damaged);
    if (windowAcceptsUpdates(window))
    {
        if (!window->paint_pending)
            window->paint_offered = FALSE;
        window->paint_pending = TRUE;
    }
    /* Lightweight siblings share one retained canvas. A repaint that is not
       sibling-clipped can overwrite an overlapping upper sibling, including
       a transparent frame. Schedule its affected pixels for later restoration. */
    if ((expanded || !was_pending) && window->parent && window->parent != &desktop_window &&
        windowAcceptsUpdates(window) && !((window->style | window->class_style) & WS_CLIPSIBLINGS))
        for (Window *above = window->parent->children; above && above != window;
             above = above->sibling)
        {
            RECTL local = window->invalid;
            WinOffsetRect(0, &local, window->x - above->x, window->y - above->y);
            WinInvalidateRect(above->hwnd, &local, TRUE);
        }
    if (children)
        for (Window *child = window->children; child; child = child->sibling)
        {
            /* The update area is conservatively stored as a bounding box.
               Propagate that same area: combining disjoint damage can make
               the parent repaint a previously clean gap over a child. */
            RECTL local = window->invalid;
            WinOffsetRect(0, &local, -child->x, -child->y);
            WinInvalidateRect(child->hwnd, &local, TRUE);
        }
    return TRUE;
}

BOOL WinQueryUpdateRect(HWND handle, PRECTL rect)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || !rect)
        return FALSE;
    *rect = window->invalid;
    return !WinIsRectEmpty(0, rect);
}

BOOL WinUpdateWindow(HWND handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    if (!WinIsRectEmpty(0, &window->invalid) && WinIsWindowVisible(handle) &&
        windowAcceptsUpdates(window))
    {
        remove_paint(anchor, handle);
        sendMessage(window, WM_PAINT, 0, 0);
    }
    return TRUE;
}

HWND WinWindowFromDC(HDC dc)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getWindowFromHWND(anchor, dc) ? dc : 0;
}

BOOL lx_pm_validate_rect(HWND handle, const RECTL *rectangle, BOOL internal_only)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    RECTL *old = &window->invalid;
    if (internal_only)
    {
        if (WinIsRectEmpty(0, old))
            validateWindowPaint(anchor, window);
    }
    else if (!rectangle || (rectangle->xLeft <= old->xLeft && rectangle->yBottom <= old->yBottom &&
                            rectangle->xRight >= old->xRight && rectangle->yTop >= old->yTop))
        validateWindowPaint(anchor, window);
    return TRUE;
}

static BOOL validate_region_window(HWND handle, const PmRegion *region, BOOL children)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w)
        return FALSE;
    PmRegion before = {.rects = w->damage_rects, .count = w->damage_count}, after = {0};
    if (!before.count && !WinIsRectEmpty(0, &w->invalid))
    {
        before.rects = &w->invalid;
        before.count = 1;
    }
    if (!region)
    {
        validateWindowPaint(a, w);
    }
    else
    {
        if (!regionBoolean(&after, &before, region, 7))
            return FALSE;
        free(w->damage_rects);
        w->damage_rects = after.rects;
        w->damage_count = after.count;
        w->invalid = (RECTL){0, 0, 0, 0};
        for (size_t i = 0; i < after.count; i++)
            WinUnionRect(0, &w->invalid, &w->invalid, &after.rects[i]);
        if (!after.count)
            validateWindowPaint(a, w);
    }
    if (children)
        for (Window *c = w->children; c; c = c->sibling)
        {
            PmRegion local = {0};
            if (region && region->count)
            {
                local.rects = malloc(region->count * sizeof *local.rects);
                if (!local.rects)
                    return FALSE;
                local.count = region->count;
                for (size_t i = 0; i < local.count; i++)
                {
                    local.rects[i] = region->rects[i];
                    WinOffsetRect(0, &local.rects[i], -c->x, -c->y);
                }
            }
            BOOL ok = validate_region_window(c->hwnd, region ? &local : NULL, TRUE);
            free(local.rects);
            if (!ok)
                return FALSE;
        }
    return TRUE;
}

BOOL WinValidateRect(HWND handle, PRECTL rectangle, BOOL children)
{
    PmRegion r = {.rects = rectangle, .count = rectangle ? 1 : 0};
    return validate_region_window(handle, rectangle ? &r : NULL, children);
}

BOOL WinValidateRegion(HWND handle, ULONG region, BOOL children)
{
    if (!region)
        return validate_region_window(handle, NULL, children);
    LxHandleBorrow b = {0};
    if (!lx_handle_borrow(region, LX_HANDLE_PM_REGION, &b))
        return FALSE;
    PmRegion *r = b.resource;
    BOOL ok = r->anchor == getAnchorBlockNoHAB() && validate_region_window(handle, r, children);
    lx_handle_release(&b);
    return ok;
}

BOOL WinInvalidateRegion(HWND handle, ULONG region, BOOL children)
{
    if (!region)
        return WinInvalidateRect(handle, NULL, children);
    LxHandleBorrow b = {0};
    if (!lx_handle_borrow(region, LX_HANDLE_PM_REGION, &b))
        return FALSE;
    PmRegion *r = b.resource;
    BOOL ok = r->anchor == getAnchorBlockNoHAB();
    for (size_t i = 0; ok && i < r->count; i++)
        ok = WinInvalidateRect(handle, &r->rects[i], children);
    lx_handle_release(&b);
    return ok;
}

BOOL lx_pm_window_damage(HWND window, RECTL **rectangles, size_t *count, LONG *height)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, window) : NULL;
    if (!w)
        return FALSE;
    const RECTL *damage = w->damage_rects;
    size_t n = w->damage_count;
    if (!n && !WinIsRectEmpty(0, &w->invalid))
    {
        damage = &w->invalid;
        n = 1;
    }
    RECTL *copy = n ? malloc(n * sizeof *copy) : NULL;
    if (copy)
        memcpy(copy, damage, n * sizeof *copy);
    *rectangles = copy;
    *count = n;
    *height = w->h;
    return TRUE;
}

BOOL lx_pm_window_scroll_damage(HWND window, const RECTL *area, LONG dx, LONG dy)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, window) : NULL;
    if (!w)
        return FALSE;
    PmRegion before = {.rects = w->damage_rects, .count = w->damage_count};
    if (!before.count && !WinIsRectEmpty(0, &w->invalid))
    {
        before.rects = &w->invalid;
        before.count = 1;
    }
    if (!before.count)
        return TRUE;
    RECTL box = *area;
    PmRegion clip = {.rects = &box, .count = 1}, inside = {0}, outside = {0}, kept = {0},
             moved = {0};
    BOOL ok =
        regionBoolean(&inside, &before, &clip, 6) && regionBoolean(&outside, &before, &clip, 7);
    for (size_t i = 0; ok && i < inside.count; i++)
        WinOffsetRect(0, &inside.rects[i], dx, dy);
    ok = ok && regionBoolean(&kept, &inside, &clip, 6) && regionBoolean(&moved, &outside, &kept, 1);
    free(inside.rects);
    free(outside.rects);
    free(kept.rects);
    if (!ok)
    {
        free(moved.rects);
        return FALSE;
    }
    free(w->damage_rects);
    w->damage_rects = moved.rects;
    w->damage_count = moved.count;
    w->invalid = (RECTL){0};
    for (size_t i = 0; i < moved.count; i++)
        WinUnionRect(0, &w->invalid, &w->invalid, &moved.rects[i]);
    return TRUE;
}

LONG WinQueryUpdateRegion(HWND handle, ULONG region)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    LxHandleBorrow b = {0};
    if (!w || !lx_handle_borrow(region, LX_HANDLE_PM_REGION, &b))
        return 0;
    PmRegion *r = b.resource;
    PmRegion before = {.rects = w->damage_rects, .count = w->damage_count}, out = {0};
    if (!before.count && !WinIsRectEmpty(0, &w->invalid))
    {
        before.rects = &w->invalid;
        before.count = 1;
    }
    BOOL ok = r->anchor == a && !r->selected && regionBoolean(&out, &before, NULL, 2);
    if (ok)
    {
        free(r->rects);
        r->rects = out.rects;
        r->count = out.count;
    }
    LONG n = ok ? (r->count > 1 ? 3 : r->count ? 2 : 1) : 0;
    lx_handle_release(&b);
    return n;
}
