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
    if (!getAnchorBlock(hab) || !name || !GProcessState.loadModule)
        return 0;
    LxModule *m = GProcessState.loadModule(name);
    return m ? m->handle : 0;
}

BOOL WinDeleteLibrary(HAB hab, ULONG handle)
{
    LxHandleBorrow b = {0};
    if (!getAnchorBlock(hab) || !GProcessState.freeModule ||
        !lx_handle_borrow(handle, LX_HANDLE_MODULE, &b))
        return FALSE;
    GProcessState.freeModule(b.resource);
    lx_handle_release(&b);
    return TRUE;
}

static LONG clamp_long(LONG x, LONG low, LONG high)
{
    return x < low ? low : x > high ? high : x;
}

BOOL WinFlashWindow(HWND handle, BOOL flash)
{
    return (BOOL)(uintptr_t)WinSendMsg(handle, WM_FLASHWINDOW, (MPARAM)(uintptr_t)(flash != 0), 0);
}

BOOL WinFocusChange(HWND desktop, HWND focus, ULONG flags)
{
    return changeFocus(desktop, focus, (flags & 0xffff) | FOCUS_NOTIFY);
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

static BOOL slot_taken(const Window *parent, const Window *self, const RECTL *slot, LONG cx,
                       LONG cy)
{
    for (Window *s = parent->children; s; s = s->sibling)
    {
        if (s == self || !(s->style & WS_MINIMIZED) || s->data_len < 0x18)
            continue;
        LONG x = (SHORT)WinQueryWindowUShort(s->hwnd, 0x14),
             y = (SHORT)WinQueryWindowUShort(s->hwnd, 0x16);
        if (x < slot->xRight && slot->xLeft < x + cx && y < slot->yTop && slot->yBottom < y + cy)
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
    LONG width = w->parent->w, height = w->parent->h;
    LONG icon_x = WinQuerySysValue(HWND_DESKTOP, 38), icon_y = WinQuerySysValue(HWND_DESKTOP, 39);
    LONG text_x = WinQuerySysValue(HWND_DESKTOP, 68), text_y = WinQuerySysValue(HWND_DESKTOP, 30);
    LONG pad_x = WinQuerySysValue(HWND_DESKTOP, 26), pad_y = WinQuerySysValue(HWND_DESKTOP, 27);
    /* Icon cells leave room for the text beneath, and widen to fill each row evenly. */
    LONG cell_x = (text_x > icon_x + border.x * 2 ? text_x : icon_x + border.x * 2) + pad_x * 4;
    LONG cell_y = icon_y + border.y * 2 + text_y;
    if (cell_x <= 0 || cell_y <= 0)
        return FALSE;
    if (cell_x <= width && width / (width / cell_x) > cell_x)
        cell_x = width / (width / cell_x);
    LONG columns = width / cell_x > 0 ? width / cell_x : 1;
    LONG rows = height / cell_y > 0 ? height / cell_y : 1;
    LONG slot_width = cell_x + (width - text_x * columns) / columns;
    LONG want_column = 0, want_row = 0;
    if (preferred)
    {
        want_column =
            clamp_long(width ? (LONG)((int64_t)preferred->x * columns / width) : 0, 0, columns - 1);
        want_row =
            clamp_long(height ? (LONG)((int64_t)preferred->y * rows / height) : 0, 0, rows - 1);
    }
    LONG best_column = want_column, best_row = want_row, bottom = 0;
    int64_t best = -1;
    for (LONG row = 0; row < rows; row++)
        for (LONG column = 0; column < columns; column++)
        {
            RECTL slot = {(LONG)(((int64_t)column * width + columns / 2) / columns),
                          (LONG)(((int64_t)row * height + rows / 2) / rows),
                          (LONG)(((int64_t)(column + 1) * width + columns / 2) / columns),
                          (LONG)(((int64_t)(row + 1) * height + rows / 2) / rows)};
            if (slot_taken(w->parent, w, &slot, icon_x, icon_y))
                continue;
            int64_t dx = column - want_column, dy = row - want_row, distance = dx * dx + dy * dy;
            if (best < 0 || distance < best)
            {
                best = distance;
                best_column = column;
                best_row = row;
                bottom = slot.yBottom;
            }
        }
    LONG x = best_column * cell_x + (slot_width - icon_x) / 2, y = text_y + pad_y * 2 + bottom;
    w->style |= WS_MINIMIZED;
    if (w->data_len >= 0x18)
    {
        WinSetWindowUShort(handle, 0x14, (USHORT)x);
        WinSetWindowUShort(handle, 0x16, (USHORT)y);
    }
    out->hwnd = handle;
    out->x = x - border.x;
    out->y = y - border.y;
    out->cx = icon_x + border.x * 2;
    out->cy = icon_y + border.y * 2;
    out->fl |= SWP_SIZE | SWP_MOVE;
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
    return loadStringTable(module, 10, id, capacity, output);
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
    LxTIB *t = GProcessState.getOs2Tib ? GProcessState.getOs2Tib() : NULL;
    ULONG tid = 0;
    if (t)
    {
        uint8 b[4];
        if (lx_execution_read(GProcessState.execution, t->tib_ptib2, b, 4))
            tid = lx_guest_load_u32(b);
    }
    lx_guest_store_u32(data, 20);
    lx_guest_store_u32(data + 4, GProcessState.pib.pib_ulpid);
    lx_guest_store_u32(data + 8, tid);
    lx_guest_store_u32(data + 12, count);
    memcpy(out, data, bytes < 20 ? bytes : 20);
    return TRUE;
}

BOOL WinSetCp(HMQ queue, ULONG cp)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!cp)
        cp = GProcessState.process_codepage ? GProcessState.process_codepage : 437;
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

/* Sending can destroy windows, so targets are gathered before any delivery. */
static BOOL collect_targets(const Window *parent, ULONG flags, HWND **list, size_t *count,
                            size_t *capacity)
{
    for (Window *child = parent->children; child; child = child->sibling)
    {
        if (!(flags & 8) || isFrameWindow(child)) /* BMSG_FRAMEONLY */
        {
            if (*count == *capacity)
            {
                size_t grown = *capacity ? *capacity * 2 : 32;
                HWND *larger = realloc(*list, grown * sizeof *larger);
                if (!larger)
                    return FALSE;
                *list = larger;
                *capacity = grown;
            }
            (*list)[(*count)++] = child->hwnd;
        }
        if ((flags & 4) && !collect_targets(child, flags, list, count, capacity)) /* DESCENDANTS */
            return FALSE;
    }
    return TRUE;
}

BOOL WinBroadcastMsg(HWND parent, ULONG message, MPARAM first, MPARAM second, ULONG flags)
{
    if (flags & ~0x1fu)
    {
        WinSetErrorInfo(PMERR_INVALID_FLAG, 0);
        return FALSE;
    }
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = !a ? NULL : parent == HWND_DESKTOP ? &desktop_window : getWindowFromHWND(a, parent);
    if (!w)
        return FALSE;
    /* BMSG_POSTQUEUE and 0x10 address message queues rather than windows. */
    if (flags & 0x12)
        return broadcastQueues(a, message, first, second, !(flags & 2));
    if (!w->children)
        return FALSE;
    HWND *targets = NULL;
    size_t count = 0, capacity = 0;
    BOOL ok = collect_targets(w, flags, &targets, &count, &capacity);
    for (size_t i = 0; i < count; i++)
    {
        if (flags & 1) /* BMSG_SEND */
        {
            if (!WinSendMsg(targets[i], message, first, second))
                ok = FALSE;
        }
        else if (!WinPostMsg(targets[i], message, first, second))
            ok = FALSE;
    }
    free(targets);
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
        lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!p->mapping)
    {
        free(p);
        return NULL;
    }
    uint8 *out = lx_execution_mapping_host(p->mapping);
    /* ERRINFO, one message offset at 24, no binary data at 20, and the message at 40. The
       severity is the id's high word; PM sites that record a bare code are errors. */
    memset(out, 0, 40);
    ULONG error = WinGetLastError(hab), code = error & 0xffff;
    ULONG severity = error >> 16 ? error >> 16 : 8; /* SEVERITY_ERROR */
    char prefix[16], letters[32], text[256] = {0};
    if (!loadStringTable((HMODULE)-1, 5, 0, sizeof prefix, prefix))
        snprintf(prefix, sizeof prefix, "PMWIN");
    size_t known = (size_t)loadStringTable((HMODULE)-1, 5, 0x1021, sizeof letters, letters);
    char letter = known            ? letters[severity < known ? severity : known - 1]
                  : severity == 4  ? 'W'
                  : severity == 12 ? 'S'
                  : severity == 16 ? 'U'
                                   : 'E';
    if (!loadStringTable((HMODULE)-1, 10, code, sizeof text, text))
        loadStringTable((HMODULE)-1, 5, code, sizeof text, text);
    lx_guest_store_u32(out, 20);
    lx_guest_store_u32(out + 4, severity << 16 | code);
    lx_guest_store_u32(out + 8, 1);
    lx_guest_store_u32(out + 12, 24);
    lx_guest_store_u32(out + 16, 20);
    lx_guest_store_u32(out + 24, 40);
    snprintf((char *)out + 40, 4096 - 40, "%s%06X%c %s\r\n", prefix, (unsigned)code, letter, text);
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
    lx_execution_unmap(GProcessState.execution, p->mapping);
    free(p);
    return TRUE;
}

void cleanupErrorInfo(void)
{
    while (error_allocations)
        WinFreeErrorInfo(
            (PVOID)(uintptr_t)lx_execution_mapping_address(error_allocations->mapping));
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
