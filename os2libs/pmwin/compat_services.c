/* Public domain. No rights reserved. Wholly AI-authored. */
/* Additional PM services use the same thread queue, window tree, resources and
   execution memory as their existing counterparts. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/paint.h"
#include "guest/abi.h"
#include <ctype.h>
BOOL WinCancelShutdown(HMQ queue, BOOL cancel)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || a->message_queue.hmq != queue)
        return FALSE;
    /* TRUE keeps later shutdowns from this queue; either way pending quits are withdrawn. */
    MessageQueue *q = &a->message_queue;
    q->cancel_shutdown = cancel != 0;
    MessageQueueItem *previous = NULL, *next;
    for (MessageQueueItem *item = q->head; item; item = next)
    {
        next = item->next;
        if (item->qmsg.msg != WM_QUIT)
        {
            previous = item;
            continue;
        }
        if (previous)
            previous->next = next;
        else
            q->head = next;
        if (q->tail == item)
            q->tail = previous;
        item->next = q->free_pool;
        q->free_pool = item;
    }
    return TRUE;
}
BOOL WinCpTranslateString(HAB hab, ULONG from, PSZ input, ULONG to, ULONG capacity, PSZ output)
{
    if (!getAnchorBlock(hab) || !input || !output)
        return FALSE;
    /* An invalid code page leaves its own error from the character translator. */
    if (from != to && !WinCpTranslateChar(hab, from, ' ', to))
        return FALSE;
    for (ULONG i = 0; i < capacity; i++)
    {
        UCHAR c = (UCHAR)input[i];
        if (!c)
        {
            output[i] = 0;
            return TRUE;
        }
        output[i] = from == to ? c : WinCpTranslateChar(hab, from, c, to);
        if (!output[i])
            return FALSE;
    }
    if (capacity)
        output[capacity - 1] = 0;
    WinSetErrorInfo(PMERR_PARAMETER_OUT_OF_RANGE, 0);
    return FALSE;
}
ULONG WinLoadLibrary(HAB hab, PSZ name)
{
    if (!getAnchorBlock(hab) || !name || !GLoaderState.loadModule)
        return 0;
    LxModule *m = GLoaderState.loadModule(name);
    return m ? m->handle : 0;
}
BOOL WinDeleteLibrary(HAB hab, ULONG handle)
{
    LxHandleBorrow b = {0};
    if (!getAnchorBlock(hab) || !GLoaderState.freeModule ||
        !lx_handle_borrow(handle, LX_HANDLE_MODULE, &b))
        return FALSE;
    GLoaderState.freeModule(b.resource);
    lx_handle_release(&b);
    return TRUE;
}
BOOL WinFlashWindow(HWND handle, BOOL flash)
{
    return (BOOL)(uintptr_t)WinSendMsg(handle, WM_FLASHWINDOW, (MPARAM)(uintptr_t)(flash != 0), 0);
}
BOOL WinFocusChange(HWND desktop, HWND focus, ULONG flags)
{
    return changeFocus(desktop, focus, flags);
}
BOOL WinGetMaxPosition(HWND handle, PSWP out)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w || !out || !w->parent)
        return FALSE;
    /* A maximized frame's border hangs outside its parent. */
    POINTL border = isFrameWindow(w) ? frameBorderSize(w) : (POINTL){0, 0};
    out->hwnd = handle;
    out->x = -border.x;
    out->y = -border.y;
    out->cx = w->parent->w + border.x * 2;
    out->cy = w->parent->h + border.y * 2;
    out->fl |= SWP_SIZE | SWP_MOVE;
    return TRUE;
}
static BOOL minimized_at(const Window *parent, const Window *self, LONG x, LONG y, LONG cx, LONG cy)
{
    for (Window *s = parent->children; s; s = s->sibling)
    {
        if (s == self || !(s->style & WS_MINIMIZED) || s->data_len < 0x18)
            continue;
        LONG sx = (SHORT)WinQueryWindowUShort(s->hwnd, 0x14),
             sy = (SHORT)WinQueryWindowUShort(s->hwnd, 0x16);
        if (sx >= x && sx < x + cx && sy >= y && sy < y + cy)
            return TRUE;
    }
    return FALSE;
}
BOOL WinGetMinPosition(HWND handle, PSWP out, PPOINTL preferred)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w || !out || !w->parent)
        return FALSE;
    POINTL border = isFrameWindow(w) ? frameBorderSize(w) : (POINTL){0, 0};
    LONG cx = WinQuerySysValue(HWND_DESKTOP, 38) + border.x * 2,
         cy = WinQuerySysValue(HWND_DESKTOP, 39) + border.y * 2;
    if (cx <= 0 || cy <= 0)
        return FALSE;
    LONG columns = w->parent->w / cx, rows = w->parent->h / cy;
    if (columns < 1)
        columns = 1;
    if (rows < 1)
        rows = 1;
    /* Icon slots fill from the bottom left; a preference selects the nearest free slot. */
    LONG best_x = 0, best_y = 0;
    int64_t best = -1;
    for (LONG row = 0; row < rows; row++)
        for (LONG column = 0; column < columns; column++)
        {
            LONG x = column * cx, y = row * cy;
            if (minimized_at(w->parent, w, x, y, cx, cy))
                continue;
            int64_t distance = 0;
            if (preferred)
            {
                int64_t dx = (int64_t)x - preferred->x, dy = (int64_t)y - preferred->y;
                distance = dx * dx + dy * dy;
            }
            if (best < 0 || distance < best)
            {
                best = distance;
                best_x = x;
                best_y = y;
            }
            if (!preferred)
                goto found;
        }
found:
    out->hwnd = handle;
    out->x = best_x;
    out->y = best_y;
    out->cx = cx;
    out->cy = cy;
    out->fl |= SWP_SIZE | SWP_MOVE;
    if (w->data_len >= 0x18)
    {
        WinSetWindowUShort(handle, 0x14, (USHORT)best_x);
        WinSetWindowUShort(handle, 0x16, (USHORT)best_y);
    }
    return TRUE;
}
BOOL WinIsWindowShowing(HWND handle)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w)
        return FALSE;
    for (Window *p = w; p && p != &desktop_window; p = p->parent)
        if (!(p->style & WS_VISIBLE) || (p->style & WS_MINIMIZED))
            return FALSE;
    /* Showing means the window's visible region is not empty. */
    HPS space = acquirePresentationSpace(handle, 0, NULL, FALSE);
    PresentationSpace *ps = space ? getPresentationSpaceFromHPS(a, space) : NULL;
    if (!ps)
        return TRUE;
    PmRegion visible = {0};
    BOOL known = lx_pm_window_clip(space, NULL, &visible), showing = !known || visible.count;
    free(visible.rects);
    destroyPresentationSpace(a, ps);
    return showing;
}
LONG WinLoadMessage(HAB hab, HMODULE module, ULONG id, LONG capacity, PSZ output)
{
    (void)hab;
    return loadStringTable(module == (HMODULE)-1 ? 0 : module, 10, id, capacity, output);
}
PSZ WinNextChar(HAB hab, ULONG cp, ULONG country, PSZ text)
{
    /* Single-byte code pages only; an unknown page falls back to the queue page. */
    (void)hab;
    (void)cp;
    (void)country;
    return text ? text + (*text != 0) : NULL;
}
BOOL WinQueryDlgItemShort(HWND parent, ULONG id, PSHORT output, BOOL signed_value)
{
    if (output)
        *output = 0;
    if (!WinWindowFromID(parent, id))
        return FALSE;
    char text[32] = {0};
    WinQueryDlgItemText(parent, id, sizeof text, text);
    size_t end = strlen(text);
    while (end && text[end - 1] == ' ')
        end--;
    text[end] = 0;
    const char *p = text;
    while (*p == ' ')
        p++;
    BOOL negative = *p == '-';
    if (negative)
    {
        if (!signed_value)
            return FALSE;
        p++;
    }
    if (*p < '0' || *p > '9')
        return FALSE;
    ULONG value = 0;
    for (; *p >= '0' && *p <= '9'; p++)
    {
        ULONG digit = (ULONG)(*p - '0');
        if (value > 6553 || (value == 6553 && digit > 5))
            return FALSE;
        value = value * 10 + digit;
        if (signed_value && value > 32767)
            return FALSE;
    }
    if (*p)
        return FALSE;
    if (output)
        *output = (SHORT)(negative ? -(LONG)value : (LONG)value);
    return TRUE;
}
BOOL WinSetDlgItemShort(HWND parent, ULONG id, USHORT value, BOOL signed_value)
{
    char text[24];
    if (signed_value)
        snprintf(text, sizeof text, "%d", (int)(SHORT)value);
    else
        snprintf(text, sizeof text, "%u", (unsigned)value);
    return WinSetDlgItemText(parent, id, text);
}
BOOL WinQueryMsgPos(HAB hab, PPOINTL out)
{
    AnchorBlock *a = getAnchorBlock(hab);
    if (!a || !out)
        return FALSE;
    *out = a->message_queue.last_position;
    return TRUE;
}
HWND WinQueryObjectWindow(HWND desktop)
{
    return desktop == HWND_DESKTOP && getAnchorBlockNoHAB() ? HWND_OBJECT : 0;
}
BOOL WinQueryQueueInfo(HMQ queue, PVOID out, ULONG bytes)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || (a->message_queue.hmq != queue && queue != 1)) /* HMQ_CURRENT */
        return FALSE;
    /* Whole fields are copied, and a missing or short buffer is not an error. */
    bytes &= ~3u;
    if (!out || !bytes)
        return TRUE;
    uint8 data[20] = {0};
    ULONG count = 0;
    for (MessageQueueItem *i = a->message_queue.head; i; i = i->next)
        count++;
    LxTIB *t = GLoaderState.getOs2Tib ? GLoaderState.getOs2Tib() : NULL;
    ULONG tid = 0;
    if (t)
    {
        uint8 b[4];
        if (lx_execution_read(GLoaderState.execution, t->tib_ptib2, b, 4))
            tid = lx_guest_load_u32(b);
    }
    lx_guest_store_u32(data, 20);
    lx_guest_store_u32(data + 4, GLoaderState.pib.pib_ulpid);
    lx_guest_store_u32(data + 8, tid);
    lx_guest_store_u32(data + 12, count);
    memcpy(out, data, bytes < 20 ? bytes : 20);
    return TRUE;
}
BOOL WinSetCp(HMQ queue, ULONG cp)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!cp)
        cp = GLoaderState.process_codepage ? GLoaderState.process_codepage : 437;
    if (!a || a->message_queue.hmq != queue || (cp != 437 && cp != 850 && cp != 1004))
        return FALSE;
    a->message_queue.codepage = cp;
    return TRUE;
}
BOOL WinSetMultWindowPos(HAB hab, PSWP positions, ULONG count)
{
    AnchorBlock *a = getAnchorBlock(hab);
    if (!a || count > 65536 || (count && !positions))
        return FALSE;
    for (ULONG i = 0; i < count; i++)
    {
        if (positions[i].fl > 0xffff)
        {
            WinSetErrorInfo(PMERR_INVALID_FLAG, 0);
            return FALSE;
        }
        if (!getWindowFromHWND(a, positions[i].hwnd))
            return FALSE;
    }
    for (ULONG i = 0; i < count; i++)
    {
        SWP *p = &positions[i];
        if (!WinSetWindowPos(p->hwnd, p->hwndInsertBehind, p->x, p->y, p->cx, p->cy, p->fl))
            return FALSE;
    }
    return TRUE;
}
BOOL WinSetWindowBits(HWND handle, LONG index, ULONG data, ULONG mask)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w)
        return FALSE;
    if (index >= 0 && ((size_t)index + 4 > w->data_len))
        return FALSE;
    ULONG old = WinQueryWindowULong(handle, index);
    return WinSetWindowULong(handle, index, (old & ~mask) | (data & mask));
}
BOOL WinBroadcastMsg(HWND parent, ULONG message, MPARAM first, MPARAM second, ULONG flags)
{
    if (flags & ~15u)
        return FALSE;
    HENUM e = WinBeginEnumWindows(parent);
    if (!e)
        return FALSE;
    BOOL ok = TRUE;
    HWND h;
    while ((h = WinGetNextWindow(e)))
    {
        AnchorBlock *a = getAnchorBlockNoHAB();
        Window *w = a ? getWindowFromHWND(a, h) : NULL;
        if (!w)
            continue;
        if (!(flags & 8) || isFrameWindow(w))
        {
            if (flags & 1)
                WinSendMsg(h, message, first, second);
            else
                ok = WinPostMsg(h, message, first, second) && ok;
        }
        if (flags & 4)
            ok = WinBroadcastMsg(h, message, first, second, flags) && ok;
    }
    WinEndEnumWindows(e);
    return ok;
}
BOOL WinSetKeyboardStateTable(HWND desktop, PBYTE table, BOOL set)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || desktop != HWND_DESKTOP || !table)
        return FALSE;
    for (unsigned i = 0; i < 256; i++)
    {
        if (set)
        {
            a->key_down[i] = (table[i] & 0x80) != 0;
            a->key_toggled[i] = (table[i] & 1) != 0;
        }
        else
            table[i] = (a->key_down[i] ? 0x80 : 0) | (a->key_toggled[i] ? 1 : 0);
    }
    return TRUE;
}
LONG WinRealizePalette(HWND handle, HPS ps, PULONG changed)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || !getWindowFromHWND(a, handle) || !lx_pm_valid_ps(ps))
        return -1;
    if (changed)
        *changed =
            0; /* True-color surfaces realize palette RGB values without changing display entries. */
    return 0;
}

typedef struct ErrorAllocation
{
    LxGuestMapping *mapping;
    struct ErrorAllocation *next;
} ErrorAllocation;
static _Thread_local ErrorAllocation *error_allocations;
PVOID WinGetErrorInfo(HAB hab)
{
    AnchorBlock *a = getAnchorBlock(hab);
    if (!a || !lxHostThreadState()->pm_error)
        return NULL;
    ErrorAllocation *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->mapping =
        lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!p->mapping)
    {
        free(p);
        return NULL;
    }
    uint8 *out = lx_execution_mapping_host(p->mapping);
    /* ERRINFO, an empty message offset array at 24 and no binary data at 20. */
    memset(out, 0, 40);
    lx_guest_store_u32(out, 20);
    lx_guest_store_u32(out + 4, WinGetLastError(hab));
    lx_guest_store_u32(out + 12, 24);
    lx_guest_store_u32(out + 16, 20);
    p->next = error_allocations;
    error_allocations = p;
    return (PVOID)(uintptr_t)lx_execution_mapping_address(p->mapping);
}
BOOL WinFreeErrorInfo(PVOID address)
{
    ErrorAllocation **link = &error_allocations;
    while (*link && (uintptr_t)address != lx_execution_mapping_address((*link)->mapping))
        link = &(*link)->next;
    if (!*link)
        return FALSE;
    ErrorAllocation *p = *link;
    *link = p->next;
    lx_execution_unmap(GLoaderState.execution, p->mapping);
    free(p);
    return TRUE;
}
void cleanupErrorInfo(void)
{
    while (error_allocations)
        WinFreeErrorInfo(
            (PVOID)(uintptr_t)lx_execution_mapping_address(error_allocations->mapping));
}
static LONG clamp_long(LONG x, LONG low, LONG high)
{
    return x < low ? low : x > high ? high : x;
}
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
        POINTL corner = {(original.xLeft + original.xRight) / 2,
                         (original.yBottom + original.yTop) / 2};
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
            info->rclTrack =
                tracked_rectangle(info, original, point.x - start.x + key_x, point.y - start.y + key_y);
            invert_track(space, info, &info->rclTrack);
        }
    }
    invert_track(space, info, &info->rclTrack);
    WinSetCapture(HWND_DESKTOP, capture);
    if (!ps)
        WinReleasePS(space);
    tracking = FALSE;
    if (!accepted)
        info->rclTrack = original;
    return accepted;
}
