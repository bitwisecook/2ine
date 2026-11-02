/* Public domain. No rights reserved. Wholly AI-authored. */
/* Repaint requests use the same retained PM update rectangles as BeginPaint. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/paint.h"
#include "os2libs/pmwin/graphics.h"
enum
{
    RDW_INVALIDATE = 1,
    RDW_INTERNALPAINT = 2,
    RDW_ERASE = 4,
    RDW_VALIDATE = 8,
    RDW_NOINTERNALPAINT = 16,
    RDW_NOERASE = 32,
    RDW_NOCHILDREN = 64,
    RDW_ALLCHILDREN = 128,
    RDW_UPDATENOW = 256,
    RDW_ERASENOW = 512
};
static LONG clamped_coordinate(int64_t value)
{
    return value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : (LONG)value;
}
static BOOL redraw_rect(ULONG handle, const RECTL *rect, ULONG flags)
{
    handle = open32ClientWindow(handle);
    RECTL bounds;
    if (!WinQueryWindowRect(handle, &bounds))
        return FALSE;
    RECTL area = rect ? *rect : bounds;
    Open32Window *w = open32FindWindow(handle);
    if (w)
    {
        if (flags & RDW_ERASE)
            w->erase_pending = TRUE;
        if (flags & RDW_NOERASE)
            w->erase_pending = FALSE;
    }
    if (flags & RDW_VALIDATE)
        lx_pm_validate_rect(handle, rect, FALSE);
    if (flags & RDW_NOINTERNALPAINT)
        lx_pm_validate_rect(handle, NULL, TRUE);
    if ((flags & RDW_INVALIDATE) && !WinInvalidateRect(handle, &area, FALSE))
        return FALSE;
    if ((flags & RDW_INTERNALPAINT) && !WinPostMsg(handle, WM_PAINT, 0, 0))
        return FALSE;
    if ((flags & RDW_ALLCHILDREN) && !(flags & RDW_NOCHILDREN))
        for (ULONG child = WinQueryWindow(handle, 2); child; child = WinQueryWindow(child, 0))
        {
            SWP position;
            if (!WinQueryWindowPos(child, &position))
                return FALSE;
            RECTL local = area;
            local.xLeft = clamped_coordinate((int64_t)local.xLeft - position.x);
            local.xRight = clamped_coordinate((int64_t)local.xRight - position.x);
            local.yBottom = clamped_coordinate((int64_t)local.yBottom - position.y);
            local.yTop = clamped_coordinate((int64_t)local.yTop - position.y);
            if (!redraw_rect(child, &local, flags & ~(RDW_UPDATENOW | RDW_ERASENOW)))
                return FALSE;
        }
    return TRUE;
}
static BOOL update_tree(ULONG handle, ULONG flags)
{
    handle = open32ClientWindow(handle);
    if ((flags & RDW_ALLCHILDREN) && !(flags & RDW_NOCHILDREN))
    {
        ULONG child = WinQueryWindow(handle, 2);
        while (child)
        {
            ULONG next = WinQueryWindow(child, 0);
            update_tree(child, flags);
            child = next;
        }
    }
    Open32Window *w = open32FindWindow(handle);
    if (w && w->erase_pending && (flags & (RDW_ERASENOW | RDW_UPDATENOW)))
    {
        w->erase_pending = FALSE;
        ULONG dc = GetDC(handle);
        if (dc)
        {
            open32WindowCallback(w, 0x14, dc, 0);
            ReleaseDC(handle, dc);
        }
    }
    return !(flags & RDW_UPDATENOW) || WinUpdateWindow(handle);
}
BOOL RedrawWindow(ULONG handle, PVOID rectangle, ULONG region_handle, ULONG flags)
{
    TRACE_NATIVE("RedrawWindow(%u, %p, %u, %X)", handle, rectangle, region_handle, flags);
    if (!open32Presentation())
        return FALSE;
    if (!handle)
        handle = HWND_DESKTOP;
    handle = open32ClientWindow(handle);
    RECTL bounds;
    if (!WinQueryWindowRect(handle, &bounds))
    {
        open32SetError(1400);
        return FALSE;
    }
    LxHandleBorrow borrow = {0};
    BOOL result = TRUE;
    if (region_handle)
    {
        if (!lx_handle_borrow(region_handle, LX_HANDLE_OPEN32_REGION, &borrow))
        {
            open32SetError(6);
            return FALSE;
        }
        PmRegion *region = borrow.resource;
        for (size_t i = 0; i < region->count && result; i++)
        {
            RECTL *r = &region->rects[i];
            RECTL local = {r->xLeft, clamped_coordinate((int64_t)bounds.yTop - r->yTop), r->xRight,
                           clamped_coordinate((int64_t)bounds.yTop - r->yBottom)};
            result = redraw_rect(handle, &local, flags);
        }
        lx_handle_release(&borrow);
    }
    else
    {
        RECTL local;
        if (rectangle)
            local = (RECTL){(LONG)lx_guest_load_u32(rectangle),
                            clamped_coordinate((int64_t)bounds.yTop -
                                               (LONG)lx_guest_load_u32((uint8 *)rectangle + 12)),
                            (LONG)lx_guest_load_u32((uint8 *)rectangle + 8),
                            clamped_coordinate((int64_t)bounds.yTop -
                                               (LONG)lx_guest_load_u32((uint8 *)rectangle + 4))};
        result = redraw_rect(handle, rectangle ? &local : NULL, flags);
    }
    if (result && (flags & (RDW_UPDATENOW | RDW_ERASENOW)))
        result = update_tree(handle, flags);
    return result;
}

BOOL InvalidateRect(ULONG window, PVOID rectangle, BOOL erase)
{
    return RedrawWindow(window, rectangle, 0, 1 | (erase ? 4 : 0) | (window ? 0 : 128));
}
BOOL ValidateRect(ULONG window, PVOID rectangle)
{
    return RedrawWindow(window, rectangle, 0, 8 | (window ? 0 : 128));
}
BOOL InvalidateRgn(ULONG window, ULONG region, BOOL erase)
{
    return RedrawWindow(window, NULL, region, 1 | (erase ? 4 : 0) | (window ? 0 : 128));
}
BOOL ValidateRgn(ULONG window, ULONG region)
{
    return RedrawWindow(window, NULL, region, 8 | (window ? 0 : 128));
}
BOOL GetUpdateRect(ULONG window, PVOID output, BOOL erase)
{
    window = open32ClientWindow(window);
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), window))
    {
        open32SetError(1400);
        return FALSE;
    }
    RECTL rectangle = {0}, bounds;
    BOOL damaged = WinQueryUpdateRect(window, &rectangle);
    if (output)
    {
        uint8 *wire = output;
        memset(wire, 0, 16);
        if (damaged && WinQueryWindowRect(window, &bounds))
        {
            lx_guest_store_u32(wire, (ULONG)rectangle.xLeft);
            lx_guest_store_u32(wire + 4, (ULONG)(bounds.yTop - rectangle.yTop));
            lx_guest_store_u32(wire + 8, (ULONG)rectangle.xRight);
            lx_guest_store_u32(wire + 12, (ULONG)(bounds.yTop - rectangle.yBottom));
        }
    }
    if (damaged && erase)
        update_tree(window, RDW_ERASENOW);
    return damaged;
}
