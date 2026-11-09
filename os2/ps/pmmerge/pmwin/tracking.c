/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinTrackRect: the tracking rectangle drawn with an inverting mix while the
   pointer or keyboard moves or sizes it. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>

/* Applies the size limits and boundary to a tracked rectangle. */
static RECTL constrain_track(const LXTRACKINFO *info, RECTL r)
{
    LONG width = clamp_long(r.xRight - r.xLeft, info->ptlMinTrackSize.x, info->ptlMaxTrackSize.x),
         height = clamp_long(r.yTop - r.yBottom, info->ptlMinTrackSize.y, info->ptlMaxTrackSize.y);
    if ((info->fs & 5) == 1)
        r.xLeft = r.xRight - width;
    else
        r.xRight = r.xLeft + width;
    if ((info->fs & 10) == 8)
        r.yBottom = r.yTop - height;
    else
        r.yTop = r.yBottom + height;
    if (info->fs & 0x80) /* TF_ALLINBOUNDARY */
    {
        LONG x = clamp_long(r.xLeft, info->rclBoundary.xLeft, info->rclBoundary.xRight - width),
             y = clamp_long(r.yBottom, info->rclBoundary.yBottom, info->rclBoundary.yTop - height);
        r = (RECTL){x, y, x + width, y + height};
    }
    else if (info->fs & 0x200) /* TF_PARTINBOUNDARY */
    {
        LONG x = clamp_long(r.xLeft, info->rclBoundary.xLeft - width + 1,
                            info->rclBoundary.xRight - 1),
             y = clamp_long(r.yBottom, info->rclBoundary.yBottom - height + 1,
                            info->rclBoundary.yTop - 1);
        r = (RECTL){x, y, x + width, y + height};
    }
    return r;
}

static RECTL tracked_rectangle(const LXTRACKINFO *info, RECTL r, LONG dx, LONG dy)
{
    if (info->fs & 0x20) /* TF_GRID */
    {
        if (info->cxGrid > 0)
            dx = dx / info->cxGrid * info->cxGrid;
        if (info->cyGrid > 0)
            dy = dy / info->cyGrid * info->cyGrid;
    }
    if (info->fs & 1)
        r.xLeft += dx;
    if (info->fs & 4)
        r.xRight += dx;
    if (info->fs & 8)
        r.yBottom += dy;
    if (info->fs & 2)
        r.yTop += dy;
    return constrain_track(info, r);
}

/* Inverting twice restores the screen, so one routine draws and erases. */
static void invert_track(HPS ps, const LXTRACKINFO *info, const RECTL *r)
{
    LONG bx = info->cxBorder > 0 ? info->cxBorder : 1, by = info->cyBorder > 0 ? info->cyBorder : 1;
    RECTL edges[4] = {{r->xLeft, r->yBottom, r->xRight, r->yBottom + by},
                      {r->xLeft, r->yTop - by, r->xRight, r->yTop},
                      {r->xLeft, r->yBottom + by, r->xLeft + bx, r->yTop - by},
                      {r->xRight - bx, r->yBottom + by, r->xRight, r->yTop - by}};
    for (unsigned i = 0; i < 4; i++)
        if (edges[i].xRight > edges[i].xLeft && edges[i].yTop > edges[i].yBottom)
            WinInvertRect(ps, &edges[i]);
}

static _Thread_local BOOL tracking;

BOOL WinTrackRect(HWND handle, HPS ps, LXTRACKINFO *info)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w || !info || (ps && !lx_pm_valid_ps(ps)) ||
        info->ptlMaxTrackSize.x < info->ptlMinTrackSize.x ||
        info->ptlMaxTrackSize.y < info->ptlMinTrackSize.y)
        return FALSE;
    if (info->fs & ~0x3ffu)
    {
        WinSetErrorInfo(PMERR_INVALID_FLAG, 0);
        return FALSE;
    }
    if (info->fs & 0x100) /* TF_VALIDATETRACKRECT */
    {
        info->rclTrack = constrain_track(info, info->rclTrack);
        return TRUE;
    }
    if (tracking || info->rclTrack.xRight < info->rclTrack.xLeft ||
        info->rclTrack.yTop < info->rclTrack.yBottom)
        return FALSE;
    RECTL original = info->rclTrack;
    POINTL start;
    if (info->fs & 0x10) /* TF_SETPOINTERPOS */
    {
        /* A single sized edge takes the pointer; otherwise it centres in the rectangle. */
        ULONG sides = info->fs & 15;
        POINTL corner = {sides == 1   ? original.xLeft
                         : sides == 4 ? original.xRight
                                      : (original.xLeft + original.xRight) / 2,
                         sides == 2   ? original.yTop
                         : sides == 8 ? original.yBottom
                                      : (original.yBottom + original.yTop) / 2};
        WinMapWindowPoints(handle, HWND_DESKTOP, &corner, 1);
        WinSetPointerPos(HWND_DESKTOP, corner.x, corner.y);
    }
    if (!WinQueryPointerPos(HWND_DESKTOP, &start))
        return FALSE;
    HPS space = ps ? ps : WinGetPS(handle);
    if (!space)
        return FALSE;
    tracking = TRUE;
    HWND capture = WinQueryCapture(HWND_DESKTOP);
    WinSetCapture(HWND_DESKTOP, handle);
    /* Boundary tracking confines the pointer; a moved rectangle keeps its grip offset. */
    RECTL clip_before;
    BOOL was_clipped = queryPointerClip(&clip_before), confined = FALSE;
    if (info->fs & 0x280) /* TF_ALLINBOUNDARY, TF_PARTINBOUNDARY */
    {
        POINTL low = {info->rclBoundary.xLeft, info->rclBoundary.yBottom},
               high = {info->rclBoundary.xRight - 1, info->rclBoundary.yTop - 1},
               track[2] = {{original.xLeft, original.yBottom}, {original.xRight, original.yTop}};
        WinMapWindowPoints(handle, HWND_DESKTOP, &low, 1);
        WinMapWindowPoints(handle, HWND_DESKTOP, &high, 1);
        WinMapWindowPoints(handle, HWND_DESKTOP, track, 2);
        RECTL bounds = {low.x, low.y, high.x, high.y};
        if ((info->fs & 0x80) && (info->fs & 15) == 15)
            bounds = (RECTL){low.x + start.x - track[0].x, low.y + start.y - track[0].y,
                             high.x - (track[1].x - start.x), high.y - (track[1].y - start.y)};
        if (bounds.xLeft <= bounds.xRight && bounds.yBottom <= bounds.yTop)
        {
            lx_pm_set_pointer_clip(&bounds);
            confined = TRUE;
        }
    }
    /* Keyboard tracking, with no button held, shows the move pointer. */
    HPOINTER pointer = 0;
    if (!(WinGetKeyState(HWND_DESKTOP, 1) & 0x8000) && !(WinGetKeyState(HWND_DESKTOP, 2) & 0x8000))
    {
        pointer = WinQueryPointer(HWND_DESKTOP);
        WinSetPointer(HWND_DESKTOP, WinQuerySysPointer(HWND_DESKTOP, 5, FALSE));
    }
    invert_track(space, info, &info->rclTrack);
    BOOL accepted = FALSE;
    LONG key_x = 0, key_y = 0;
    for (;;)
    {
        QMSG message;
        if (!WinGetMsg(a->hab, &message, 0, 0, 0))
        {
            WinPostQueueMsg(a->message_queue.hmq, WM_QUIT, 0, 0);
            break;
        }
        if (message.msg == WM_BUTTON1UP)
        {
            accepted = TRUE;
            break;
        }
        BOOL moved = FALSE;
        if (message.msg == WM_CHAR)
        {
            ULONG flags = (ULONG)(uintptr_t)message.mp1 & 0xffff,
                  key = (ULONG)(uintptr_t)message.mp2 >> 16;
            if ((flags & 0x40) || !(flags & 2)) /* KC_KEYUP, KC_VIRTUALKEY */
                continue;
            if (key == 0x0f) /* VK_ESC */
                break;
            if (key == 0x08 || key == 0x1e) /* VK_NEWLINE, VK_ENTER */
            {
                accepted = TRUE;
                break;
            }
            LONG step_x = info->cxKeyboard > 0 ? info->cxKeyboard : 1,
                 step_y = info->cyKeyboard > 0 ? info->cyKeyboard : 1;
            moved = key >= 0x15 && key <= 0x18;
            key_x += key == 0x15 ? -step_x : key == 0x17 ? step_x : 0;
            key_y += key == 0x18 ? -step_y : key == 0x16 ? step_y : 0;
        }
        else if (message.msg == WM_MOUSEMOVE)
            moved = TRUE;
        else
            WinDispatchMsg(a->hab, &message);
        if (moved)
        {
            POINTL point;
            if (!WinQueryPointerPos(HWND_DESKTOP, &point))
                break;
            invert_track(space, info, &info->rclTrack);
            info->rclTrack = tracked_rectangle(info, original, point.x - start.x + key_x,
                                               point.y - start.y + key_y);
            invert_track(space, info, &info->rclTrack);
        }
    }
    invert_track(space, info, &info->rclTrack);
    if (pointer)
        WinSetPointer(HWND_DESKTOP, pointer);
    if (confined)
        lx_pm_set_pointer_clip(was_clipped ? &clip_before : NULL);
    WinSetCapture(HWND_DESKTOP, capture);
    if (!ps)
        WinReleasePS(space);
    tracking = FALSE;
    if (!accepted)
        info->rclTrack = original;
    return accepted;
}
