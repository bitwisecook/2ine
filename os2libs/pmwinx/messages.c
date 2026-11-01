/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows messages retain native PM packets so native subclasses see dispatch. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwin/open32_post.h"
typedef struct QueuedMessage
{
    uint8 wire[28];
    QMSG native;
    Open32Key key;
    BOOL native_valid, keyboard;
    uint64 sequence;
    struct QueuedMessage *next;
} QueuedMessage;
typedef struct DispatchOverride
{
    ULONG window, native_message, message, first, second;
    BOOL consumed;
    struct DispatchOverride *previous;
} DispatchOverride;
static _Thread_local QueuedMessage *pending, *history;
static _Thread_local unsigned history_count;
static _Thread_local uint64 next_sequence;
static _Thread_local DispatchOverride *dispatch;
static _Thread_local uint8 last_message[28];
static ULONG mouse_flags(void)
{
    ULONG flags = 0;
    if (WinGetKeyState(HWND_DESKTOP, 1) & 0x8000)
        flags |= 1;
    if (WinGetKeyState(HWND_DESKTOP, 2) & 0x8000)
        flags |= 2;
    if (WinGetKeyState(HWND_DESKTOP, 3) & 0x8000)
        flags |= 16;
    if (WinGetKeyState(HWND_DESKTOP, 9) & 0x8000)
        flags |= 4;
    if (WinGetKeyState(HWND_DESKTOP, 10) & 0x8000)
        flags |= 8;
    return flags;
}
BOOL open32PmMessage(const QMSG *native, ULONG *message, ULONG *first, ULONG *second)
{
    *first = (ULONG)(uintptr_t)native->mp1;
    *second = (ULONG)(uintptr_t)native->mp2;
    switch (native->msg)
    {
        case LX_PM_OPEN32_POST:
            *message = native->reserved;
            return TRUE;
        case WM_PAINT:
            *message = 15;
            *first = *second = 0;
            return TRUE;
        case WM_CLOSE:
            *message = 16;
            return TRUE;
        case WM_QUIT:
            *message = 18;
            return TRUE;
        case WM_TIMER:
            *message = 0x113;
            *second = 0;
            return TRUE;
        case WM_COMMAND:
            *message = 0x111;
            *first &= 0xFFFF;
            *second = 0;
            return TRUE;
        case WM_CONTROL:
            *message = 0x111;
            return TRUE;
        case WM_MOUSEMOVE:
            *message = 0x200;
            break;
        case WM_BUTTON1DOWN:
        case WM_BUTTON1UP:
        case WM_BUTTON1DBLCLK:
            *message = 0x201 + native->msg - WM_BUTTON1DOWN;
            break;
        case WM_BUTTON2DOWN:
        case WM_BUTTON2UP:
        case WM_BUTTON2DBLCLK:
            *message = 0x204 + native->msg - WM_BUTTON2DOWN;
            break;
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
        case WM_BUTTON3DBLCLK:
            *message = 0x207 + native->msg - WM_BUTTON3DOWN;
            break;
        default:
            if (native->msg < WM_USER)
                return FALSE;
            *message = native->msg - WM_USER + 0x400;
            return TRUE;
    }
    RECTL bounds;
    if (!WinQueryWindowRect(native->hwnd, &bounds))
        return FALSE;
    LONG x = (SHORT)*first, y = bounds.yTop - 1 - (SHORT)(*first >> 16);
    *second = (ULONG)(USHORT)x | (ULONG)(USHORT)y << 16;
    *first = mouse_flags();
    return TRUE;
}
static void insert_message(QueuedMessage *message)
{
    QueuedMessage **slot = &pending;
    while (*slot && (*slot)->sequence < message->sequence)
        slot = &(*slot)->next;
    message->next = *slot;
    *slot = message;
}
static QueuedMessage *new_message(ULONG window, ULONG message, ULONG first, ULONG second)
{
    QueuedMessage *node = calloc(1, sizeof *node);
    if (!node)
    {
        open32SetError(8);
        return NULL;
    }
    lx_guest_store_u32(node->wire, window);
    lx_guest_store_u32(node->wire + 4, message);
    lx_guest_store_u32(node->wire + 8, first);
    lx_guest_store_u32(node->wire + 12, second);
    lx_guest_store_u32(node->wire + 16, GetTickCount());
    node->sequence = ++next_sequence;
    return node;
}
static BOOL accept_native(const QMSG *native)
{
    ULONG message, first, second;
    Open32Key key = {0};
    if (native->msg != WM_QUIT && !open32FindWindow(native->hwnd))
    {
        WinDispatchMsg(open32Presentation(), (PQMSG)native);
        return TRUE;
    }
    if (native->msg == WM_CHAR)
    {
        open32DecodeKey((ULONG)(uintptr_t)native->mp1, (ULONG)(uintptr_t)native->mp2, &key);
        message = key.message;
        first = key.key;
        second = key.parameter;
    }
    else if (!open32PmMessage(native, &message, &first, &second))
    {
        WinDispatchMsg(open32Presentation(), (PQMSG)native);
        return TRUE;
    }
    if (message == 15)
        for (QueuedMessage *node = pending; node; node = node->next)
            if (lx_guest_load_u32(node->wire) == native->hwnd &&
                lx_guest_load_u32(node->wire + 4) == 15)
                return TRUE;
    QueuedMessage *node = new_message(native->hwnd, message, first, second);
    if (!node)
        return FALSE;
    node->native = *native;
    node->native_valid = native->msg != LX_PM_OPEN32_POST;
    node->key = key;
    node->keyboard = native->msg == WM_CHAR;
    lx_guest_store_u32(node->wire + 16, native->time);
    lx_guest_store_u32(node->wire + 20, (ULONG)native->ptl.x);
    lx_guest_store_u32(node->wire + 24,
                       (ULONG)(WinQuerySysValue(HWND_DESKTOP, 21) - 1 - native->ptl.y));
    insert_message(node);
    return TRUE;
}
static BOOL matches(const QueuedMessage *node, ULONG filter, ULONG first, ULONG last)
{
    ULONG message = lx_guest_load_u32(node->wire + 4), window = lx_guest_load_u32(node->wire);
    if (message == 18)
        return TRUE;
    if ((first || last) && (message < first || message > last))
        return FALSE;
    if (!filter)
        return TRUE;
    if (filter == UINT32_MAX)
        return !window;
    for (ULONG cursor = window; cursor; cursor = WinQueryWindow(cursor, 5))
        if (cursor == filter)
            return TRUE;
    return FALSE;
}
static void retain_history(QueuedMessage *node)
{
    node->next = history;
    history = node;
    if (++history_count > 128)
    {
        QueuedMessage **slot = &history;
        while ((*slot)->next)
            slot = &(*slot)->next;
        free(*slot);
        *slot = NULL;
        history_count--;
    }
}
static LONG retrieve(PVOID output, ULONG filter, ULONG first, ULONG last, BOOL remove, BOOL wait)
{
    HAB hab = open32Presentation();
    if (filter && filter != UINT32_MAX && hab && !WinIsWindow(hab, filter))
    {
        open32SetError(1400);
        return -1;
    }
    if (!output || !hab || ((first || last) && first > last))
    {
        open32SetError(87);
        return -1;
    }
    for (;;)
    {
        QueuedMessage **slot = &pending;
        while (*slot && !matches(*slot, filter, first, last))
            slot = &(*slot)->next;
        QueuedMessage *node = *slot;
        if (node)
        {
            *slot = node->next;
            node->next = NULL;
            BOOL consumed = FALSE;
            if (node->keyboard)
            {
                if (remove)
                    open32UpdateKey(&node->key);
                consumed =
                    open32RunKeyboardHooks(remove ? 0 : 3, node->key.key, node->key.parameter) != 0;
            }
            ULONG window = lx_guest_load_u32(node->wire);
            if (consumed || (window && !open32FindWindow(window)))
            {
                free(node);
                continue;
            }
            memcpy(output, node->wire, 28);
            memcpy(last_message, node->wire, 28);
            if (remove)
                retain_history(node);
            else
                insert_message(node);
            return 1;
        }
        QMSG native = {0};
        if (WinPeekMsg(hab, &native, 0, 0, 0, 1))
        {
            if (!accept_native(&native))
                return -1;
            continue;
        }
        if (!wait)
            return 0;
        BOOL received = WinGetMsg(hab, &native, 0, 0, 0);
        if (!received && native.msg != WM_QUIT)
        {
            open32SetError(87);
            return -1;
        }
        if (!accept_native(&native))
            return -1;
    }
}
BOOL WaitMessage(void)
{
    HAB hab = open32Presentation();
    if (!hab)
        return FALSE;
    while (!pending)
    {
        QMSG native = {0};
        BOOL received = WinGetMsg(hab, &native, 0, 0, 0);
        if (!received && native.msg != WM_QUIT)
        {
            open32SetError(87);
            return FALSE;
        }
        if (!accept_native(&native))
            return FALSE;
    }
    return TRUE;
}
BOOL PeekMessage(PVOID output, ULONG filter, ULONG first, ULONG last, ULONG flags)
{
    if (flags & ~3u)
    {
        open32SetError(87);
        return FALSE;
    }
    return retrieve(output, filter, first, last, (flags & 1) != 0, FALSE) > 0;
}
LONG GetMessage(PVOID output, ULONG filter, ULONG first, ULONG last)
{
    LONG result = retrieve(output, filter, first, last, TRUE, TRUE);
    return result > 0 && lx_guest_load_u32((uint8 *)output + 4) == 18 ? 0 : result;
}
static QueuedMessage *find_message(PVOID input)
{
    for (QueuedMessage *node = history; node; node = node->next)
        if (!memcmp(node->wire, input, 28))
            return node;
    for (QueuedMessage *node = pending; node; node = node->next)
        if (!memcmp(node->wire, input, 28))
            return node;
    return NULL;
}
BOOL TranslateMessage(PVOID input)
{
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    QueuedMessage *source = find_message(input);
    if (!source || !source->keyboard || !source->key.text)
        return FALSE;
    ULONG character = source->key.message == 0x104 ? 0x106 : 0x102;
    QueuedMessage *node = new_message(lx_guest_load_u32(source->wire), character,
                                      source->key.character, source->key.parameter);
    if (!node)
        return FALSE;
    memcpy(node->wire + 16, source->wire + 16, 12);
    insert_message(node);
    return TRUE;
}
BOOL open32QueuedCallback(ULONG handle, ULONG native_message, ULONG *result)
{
    if (!dispatch || dispatch->consumed || dispatch->window != handle ||
        dispatch->native_message != native_message)
        return FALSE;
    dispatch->consumed = TRUE;
    Open32Window *window = open32FindWindow(handle);
    *result =
        window ? open32WindowCallback(window, dispatch->message, dispatch->first, dispatch->second)
               : 0;
    return TRUE;
}
LONG DispatchMessage(PVOID input)
{
    if (!input)
    {
        open32SetError(87);
        return 0;
    }
    const uint8 *wire = input;
    ULONG handle = lx_guest_load_u32(wire), message = lx_guest_load_u32(wire + 4);
    ULONG first = lx_guest_load_u32(wire + 8), second = lx_guest_load_u32(wire + 12);
    if (!handle || message == 18)
        return 0;
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return 0;
    }
    QueuedMessage *node = find_message(input);
    LONG result;
    if ((node && node->native_valid) || message == 15)
    {
        QMSG native =
            node && node->native_valid ? node->native : (QMSG){.hwnd = handle, .msg = WM_PAINT};
        DispatchOverride current = {handle, native.msg, message, first, second, FALSE, dispatch};
        dispatch = &current;
        result = (LONG)(intptr_t)WinDispatchMsg(open32Presentation(), &native);
        dispatch = current.previous;
    }
    else
        result = (LONG)open32WindowCallback(window, message, first, second);
    /* A callback can recursively retrieve or destroy its window and the cache. */
    QueuedMessage **slot = &history;
    while (*slot && memcmp((*slot)->wire, input, 28))
        slot = &(*slot)->next;
    if (*slot)
    {
        QueuedMessage *old = *slot;
        *slot = old->next;
        free(old);
        history_count--;
    }
    return result;
}
BOOL PostMessage(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    if (!open32Presentation())
        return FALSE;
    if (handle && !open32FindWindow(handle))
    {
        if (lx_pm_post_open32_message(handle, message, first, second))
            return TRUE;
        open32SetError(1400);
        return FALSE;
    }
    QueuedMessage *node = new_message(handle, message, first, second);
    if (!node)
        return FALSE;
    insert_message(node);
    WinPostMsg(HWND_DESKTOP, 0, 0, 0);
    return TRUE;
}
VOID PostQuitMessage(LONG status)
{
    PostMessage(0, 18, (ULONG)status, 0);
}
LONG GetMessageTime(void)
{
    return (LONG)lx_guest_load_u32(last_message + 16);
}
ULONG GetMessagePos(void)
{
    return (ULONG)lx_guest_load_u16(last_message + 20) | (ULONG)lx_guest_load_u16(last_message + 24)
                                                             << 16;
}
static void remove_messages(QueuedMessage **head, ULONG window, BOOL paint_only, BOOL cached)
{
    while (*head)
    {
        QueuedMessage *node = *head;
        if (lx_guest_load_u32(node->wire) == window &&
            (!paint_only || lx_guest_load_u32(node->wire + 4) == 15))
        {
            *head = node->next;
            free(node);
            if (cached)
                history_count--;
        }
        else
            head = &node->next;
    }
}
void open32ValidateQueuedPaint(ULONG window)
{
    remove_messages(&pending, window, TRUE, FALSE);
}
void open32QueueWindowDestroyed(ULONG window)
{
    remove_messages(&pending, window, FALSE, FALSE);
    remove_messages(&history, window, FALSE, TRUE);
}
void open32QueueCleanup(void)
{
    while (pending)
    {
        QueuedMessage *node = pending;
        pending = node->next;
        free(node);
    }
    while (history)
    {
        QueuedMessage *node = history;
        history = node->next;
        free(node);
    }
    history_count = 0;
    next_sequence = 0;
    memset(last_message, 0, sizeof last_message);
}
