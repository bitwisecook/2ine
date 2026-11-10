/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows messages retain native PM packets so native subclasses see dispatch. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "host/sync.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/private/pmp.h"

enum
{
    /* Retrieved messages are kept so TranslateMessage and DispatchMessage can find
       their native PM packets; the oldest is dropped beyond this many. */
    HISTORY_LIMIT = 128,
    /* WinGetKeyState sets this bit while the key or button is down. */
    KEY_DOWN = 0x8000
};

/* The guest QMSG that WinXTranslateMsg receives. */
enum
{
    GUEST_QMSG_HWND = 0,
    GUEST_QMSG_MSG = 4,
    GUEST_QMSG_MP1 = 8,
    GUEST_QMSG_MP2 = 12,
    GUEST_QMSG_TIME = 16,
    GUEST_QMSG_X = 20,
    GUEST_QMSG_Y = 24,
    GUEST_QMSG_RESERVED = 28
};

typedef struct QueuedMessage
{
    uint8 wire[WIN32_MSG_SIZE];
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

static _Thread_local ULONG thread_receiver;
static _Thread_local BOOL thread_receiver_failed;
static _Thread_local QueuedMessage *pending, *history;
static _Thread_local unsigned history_count;
static _Thread_local uint64 next_sequence;
static _Thread_local DispatchOverride *dispatch;
static _Thread_local uint8 last_message[WIN32_MSG_SIZE];

static ULONG mouse_flags(void)
{
    ULONG flags = 0;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & KEY_DOWN)
        flags |= WIN32_MK_LBUTTON;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & KEY_DOWN)
        flags |= WIN32_MK_RBUTTON;
    if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & KEY_DOWN)
        flags |= WIN32_MK_MBUTTON;
    if (WinGetKeyState(HWND_DESKTOP, VK_SHIFT) & KEY_DOWN)
        flags |= WIN32_MK_SHIFT;
    if (WinGetKeyState(HWND_DESKTOP, VK_CTRL) & KEY_DOWN)
        flags |= WIN32_MK_CONTROL;
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
            *message = WIN32_WM_PAINT;
            *first = *second = 0;
            return TRUE;
        case WM_CLOSE:
            *message = WIN32_WM_CLOSE;
            return TRUE;
        case WM_QUIT:
            *message = WIN32_WM_QUIT;
            return TRUE;
        case WM_TIMER:
            *message = WIN32_WM_TIMER;
            *second = 0;
            return TRUE;
        case WM_COMMAND:
            *message = WIN32_WM_COMMAND;
            *first &= 0xFFFF;
            *second = 0;
            return TRUE;
        case WM_SYSCOMMAND:
            *message = WIN32_WM_SYSCOMMAND;
            switch (*first & 0xFFFF)
            {
                case SC_SIZE:
                    *first = WIN32_SC_SIZE;
                    break;
                case SC_MOVE:
                    *first = WIN32_SC_MOVE;
                    break;
                case SC_MINIMIZE:
                    *first = WIN32_SC_MINIMIZE;
                    break;
                case SC_MAXIMIZE:
                    *first = WIN32_SC_MAXIMIZE;
                    break;
                case SC_CLOSE:
                    *first = WIN32_SC_CLOSE;
                    break;
                case SC_RESTORE:
                    *first = WIN32_SC_RESTORE;
                    break;
            }
            *second = 0;
            return TRUE;
        case WM_CONTROL:
        {
            ULONG control = WinWindowFromID(native->hwnd, *first & 0xffff);
            *first = open32ControlNotification(control, *first);
            *second = control;
            *message = WIN32_WM_COMMAND;
            return TRUE;
        }
        case WM_MOUSEMOVE:
            *message = WIN32_WM_MOUSEMOVE;
            break;
        /* Both systems order each button's messages down, up, double click. */
        case WM_BUTTON1DOWN:
        case WM_BUTTON1UP:
        case WM_BUTTON1DBLCLK:
            *message = WIN32_WM_LBUTTONDOWN + native->msg - WM_BUTTON1DOWN;
            break;
        case WM_BUTTON2DOWN:
        case WM_BUTTON2UP:
        case WM_BUTTON2DBLCLK:
            *message = WIN32_WM_RBUTTONDOWN + native->msg - WM_BUTTON2DOWN;
            break;
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
        case WM_BUTTON3DBLCLK:
            *message = WIN32_WM_MBUTTONDOWN + native->msg - WM_BUTTON3DOWN;
            break;
        default:
            if (native->msg < WM_USER)
                return FALSE;
            *message = native->msg - WM_USER + WIN32_WM_USER;
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    lx_guest_store_u32(node->wire + WIN32_MSG_HWND, window);
    lx_guest_store_u32(node->wire + WIN32_MSG_MESSAGE, message);
    lx_guest_store_u32(node->wire + WIN32_MSG_WPARAM, first);
    lx_guest_store_u32(node->wire + WIN32_MSG_LPARAM, second);
    lx_guest_store_u32(node->wire + WIN32_MSG_TIME, GetTickCount());
    node->sequence = ++next_sequence;
    return node;
}

BOOL open32PostTimer(ULONG window, ULONG id, ULONG callback)
{
    for (QueuedMessage *node = pending; node; node = node->next)
        if (lx_guest_load_u32(node->wire + WIN32_MSG_HWND) == window &&
            lx_guest_load_u32(node->wire + WIN32_MSG_MESSAGE) == WIN32_WM_TIMER &&
            lx_guest_load_u32(node->wire + WIN32_MSG_WPARAM) == id)
            return TRUE;
    QueuedMessage *node = new_message(window, WIN32_WM_TIMER, id, callback);
    if (!node)
        return FALSE;
    insert_message(node);
    /* Wakes a thread blocked in GetMessage so it sees the queued timer. */
    WinPostMsg(HWND_DESKTOP, WM_NULL, 0, 0);
    return TRUE;
}

static BOOL accept_native(const QMSG *native)
{
    ULONG message, first, second;
    Open32Key key = {0};
    Open32Window *window = open32FindWindow(native->hwnd);
    /* Frame commands and close requests belong to the Windows message loop.
       Dispatching them here would run the callback inside GetMessage and keep
       that call blocked, starving application work scheduled by the callback. */
    BOOL frame_message = window && native->hwnd == window->handle &&
                         (native->msg == WM_COMMAND || native->msg == WM_CLOSE ||
                          native->msg == WM_SYSCOMMAND || native->msg >= WM_USER);
    BOOL thread_packet = thread_receiver && native->hwnd == thread_receiver;
    if (!thread_packet && native->msg != WM_QUIT &&
        (!window || (native->hwnd != window->client && !frame_message)))
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
    if (thread_packet && message == OPEN32_DDE_PACKET)
    {
        open32DdeDeliver();
        return TRUE;
    }
    if (message == WIN32_WM_PAINT)
        for (QueuedMessage *node = pending; node; node = node->next)
            if (lx_guest_load_u32(node->wire + WIN32_MSG_HWND) ==
                    open32PublicWindow(native->hwnd) &&
                lx_guest_load_u32(node->wire + WIN32_MSG_MESSAGE) == WIN32_WM_PAINT)
                return TRUE;
    QueuedMessage *node =
        new_message(native->hwnd == thread_receiver ? 0 : open32PublicWindow(native->hwnd), message,
                    first, second);
    if (!node)
        return FALSE;
    node->native = *native;
    node->native_valid = native->msg != LX_PM_OPEN32_POST;
    node->key = key;
    node->keyboard = native->msg == WM_CHAR;
    lx_guest_store_u32(node->wire + WIN32_MSG_TIME, native->time);
    lx_guest_store_u32(node->wire + WIN32_MSG_X, (ULONG)native->ptl.x);
    /* Windows screen coordinates run down from the top edge. */
    lx_guest_store_u32(node->wire + WIN32_MSG_Y,
                       (ULONG)(WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN) - 1 - native->ptl.y));
    insert_message(node);
    return TRUE;
}

static BOOL matches(const QueuedMessage *node, ULONG filter, ULONG first, ULONG last)
{
    ULONG message = lx_guest_load_u32(node->wire + WIN32_MSG_MESSAGE),
          window = lx_guest_load_u32(node->wire + WIN32_MSG_HWND);
    /* WM_QUIT passes every filter. */
    if (message == WIN32_WM_QUIT)
        return TRUE;
    if ((first || last) && (message < first || message > last))
        return FALSE;
    if (!filter)
        return TRUE;
    /* A filter of -1 selects the thread messages, which have no window. */
    if (filter == UINT32_MAX)
        return !window;
    for (ULONG cursor = window; cursor; cursor = WinQueryWindow(cursor, QW_PARENT))
        if (cursor == filter)
            return TRUE;
    return FALSE;
}

typedef struct PaintQuery
{
    ULONG window, first, last;
} PaintQuery;

static BOOL offer_native_paint(HWND handle, void *context)
{
    if (open32WindowUpdateLocked(handle))
        return FALSE;
    Open32Window *window = open32FindWindow(handle);
    if (!window || handle != window->client)
        return TRUE; /* Native chrome and controls still need their normal painting. */
    for (QueuedMessage *node = pending; node; node = node->next)
        if (lx_guest_load_u32(node->wire + WIN32_MSG_HWND) == window->handle &&
            lx_guest_load_u32(node->wire + WIN32_MSG_MESSAGE) == WIN32_WM_PAINT)
            return FALSE;
    PaintQuery *query = context;
    QueuedMessage candidate = {0};
    lx_guest_store_u32(candidate.wire + WIN32_MSG_HWND, window->handle);
    lx_guest_store_u32(candidate.wire + WIN32_MSG_MESSAGE, WIN32_WM_PAINT);
    return matches(&candidate, query->window, query->first, query->last);
}

static void retain_history(QueuedMessage *node)
{
    node->next = history;
    history = node;
    if (++history_count > HISTORY_LIMIT)
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
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return -1;
    }
    if (!output || !hab || ((first || last) && first > last))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    PaintQuery paint_query = {filter, first, last};
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
                consumed = open32RunKeyboardHooks(remove ? WIN32_HC_ACTION : WIN32_HC_NOREMOVE,
                                                  node->key.key, node->key.parameter) != 0;
            }
            ULONG window = lx_guest_load_u32(node->wire + WIN32_MSG_HWND);
            if (consumed ||
                (window && !open32FindWindow(window) && !open32NativeDialogWindow(window)))
            {
                free(node);
                continue;
            }
            memcpy(output, node->wire, WIN32_MSG_SIZE);
            memcpy(last_message, node->wire, WIN32_MSG_SIZE);
            if (remove)
                retain_history(node);
            else
                insert_message(node);
            return 1;
        }
        QMSG native = {0};
        /* Damage stays invalid until BeginPaint. Do not repeatedly regenerate
           an unmatched or already queued WM_PAINT while looking for another
           message, which would make a filtered PeekMessage loop forever. */
        if (lx_pm_peek_open32_message(hab, &native, offer_native_paint, &paint_query))
        {
            if (!accept_native(&native))
                return -1;
            continue;
        }
        if (!wait)
            return 0;
        BOOL received = lx_pm_get_open32_message(hab, &native, offer_native_paint, &paint_query);
        if (!received && native.msg != WM_QUIT)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
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
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (!accept_native(&native))
            return FALSE;
    }
    return TRUE;
}

BOOL PeekMessage(PVOID output, ULONG filter, ULONG first, ULONG last, ULONG flags)
{
    if (flags & ~((ULONG)WIN32_PM_REMOVE | WIN32_PM_NOYIELD))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return retrieve(output, filter, first, last, (flags & WIN32_PM_REMOVE) != 0, FALSE) > 0;
}

LONG GetMessage(PVOID output, ULONG filter, ULONG first, ULONG last)
{
    LONG result = retrieve(output, filter, first, last, TRUE, TRUE);
    return result > 0 && lx_guest_load_u32((uint8 *)output + WIN32_MSG_MESSAGE) == WIN32_WM_QUIT
               ? 0
               : result;
}

static QueuedMessage *find_message(PVOID input)
{
    for (QueuedMessage *node = history; node; node = node->next)
        if (!memcmp(node->wire, input, WIN32_MSG_SIZE))
            return node;
    for (QueuedMessage *node = pending; node; node = node->next)
        if (!memcmp(node->wire, input, WIN32_MSG_SIZE))
            return node;
    return NULL;
}

BOOL TranslateMessage(PVOID input)
{
    if (!input)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    QueuedMessage *source = find_message(input);
    if (!source || !source->keyboard || !source->key.text)
        return FALSE;
    ULONG character = source->key.message == WIN32_WM_SYSKEYDOWN ? WIN32_WM_SYSCHAR : WIN32_WM_CHAR;
    QueuedMessage *node = new_message(lx_guest_load_u32(source->wire + WIN32_MSG_HWND), character,
                                      source->key.character, source->key.parameter);
    if (!node)
        return FALSE;
    /* The character message carries the key message's time and position. */
    memcpy(node->wire + WIN32_MSG_TIME, source->wire + WIN32_MSG_TIME,
           WIN32_MSG_SIZE - WIN32_MSG_TIME);
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const uint8 *wire = input;
    ULONG handle = lx_guest_load_u32(wire + WIN32_MSG_HWND),
          message = lx_guest_load_u32(wire + WIN32_MSG_MESSAGE);
    ULONG first = lx_guest_load_u32(wire + WIN32_MSG_WPARAM),
          second = lx_guest_load_u32(wire + WIN32_MSG_LPARAM);
    /* A WM_TIMER lParam is the TimerProc to call instead of a window procedure. */
    BOOL timer_callback = message == WIN32_WM_TIMER && second;
    if ((!handle && !timer_callback) || message == WIN32_WM_QUIT)
        return 0;
    Open32Window *window = open32FindWindow(handle);
    if (handle && !window && message == WIN32_WM_TIMER && open32NativeDialogWindow(handle) &&
        !second)
        return (LONG)(intptr_t)WinSendMsg(handle, WM_TIMER, (MPARAM)(uintptr_t)first, 0);
    if (handle && !window && !(timer_callback && open32NativeDialogWindow(handle)))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    QueuedMessage *node = find_message(input);
    LONG result;
    if (timer_callback)
    {
        if (!open32ValidProcedure(second))
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        lx_execution_callback32_4(GProcessState.execution, second, handle, message, first,
                                  lx_guest_load_u32(wire + WIN32_MSG_TIME));
        result = 0;
    }
    else if ((node && node->native_valid) || message == WIN32_WM_PAINT)
    {
        QMSG native = node && node->native_valid
                          ? node->native
                          : (QMSG){.hwnd = open32ClientWindow(handle), .msg = WM_PAINT};
        DispatchOverride current = {
            open32ClientWindow(native.hwnd), native.msg, message, first, second, FALSE, dispatch};
        dispatch = &current;
        result = (LONG)(intptr_t)WinDispatchMsg(open32Presentation(), &native);
        dispatch = current.previous;
    }
    else
        result = (LONG)open32WindowCallback(window, message, first, second);
    /* A callback can recursively retrieve or destroy its window and the cache. */
    QueuedMessage **slot = &history;
    while (*slot && memcmp((*slot)->wire, input, WIN32_MSG_SIZE))
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
    TRACE_NATIVE("PostMessage(%u, %x, %x, %x)", (uint)handle, (uint)message, (uint)first,
                 (uint)second);
    if (!open32Presentation())
        return FALSE;
    if (handle)
    {
        /* Mixed Open32 applications also run native WinGetMsg loops. Put
           window packets on the PM owner queue for either loop to consume. */
        if (lx_pm_post_open32_message(open32ClientWindow(handle), message, first, second))
            return TRUE;
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (message == WIN32_WM_QUIT)
        return WinPostMsg(0, WM_QUIT, (MPARAM)(uintptr_t)first, (MPARAM)(uintptr_t)second);
    QueuedMessage *node = new_message(handle, message, first, second);
    if (!node)
        return FALSE;
    insert_message(node);
    /* Wakes a thread blocked in GetMessage so it sees the thread message. */
    WinPostMsg(HWND_DESKTOP, WM_NULL, 0, 0);
    return TRUE;
}

VOID PostQuitMessage(LONG status)
{
    PostMessage(0, WIN32_WM_QUIT, (ULONG)status, 0);
}

LONG GetMessageTime(void)
{
    return (LONG)lx_guest_load_u32(last_message + WIN32_MSG_TIME);
}

ULONG GetMessagePos(void)
{
    return (ULONG)lx_guest_load_u16(last_message + WIN32_MSG_X) |
           (ULONG)lx_guest_load_u16(last_message + WIN32_MSG_Y) << 16;
}

static void remove_messages(QueuedMessage **head, ULONG window, BOOL paint_only, BOOL cached)
{
    while (*head)
    {
        QueuedMessage *node = *head;
        if (lx_guest_load_u32(node->wire + WIN32_MSG_HWND) == window &&
            (!paint_only || lx_guest_load_u32(node->wire + WIN32_MSG_MESSAGE) == WIN32_WM_PAINT))
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

/* Each Open32 PM queue has a private receiver for foreign thread packets. The
   public MSG retains hwnd==NULL, as required for PostThreadMessage. */
typedef struct Open32ThreadQueue
{
    ULONG thread, window;
    struct Open32ThreadQueue *next;
} Open32ThreadQueue;

static LxHostOnce thread_queue_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *thread_queue_lock;
static Open32ThreadQueue *thread_queues;

static void initialize_thread_queues(void)
{
    thread_queue_lock = lx_host_monitor_create();
    if (!thread_queue_lock)
        abort();
}

static void thread_exit(void)
{
    open32DialogsCleanup();
    open32DeferredCleanup();
    open32UnregisterThreadQueue();
    thread_receiver_failed = TRUE; /* Window destruction must not register this thread again. */
}

void open32RegisterThreadQueue(void)
{
    if (thread_receiver || thread_receiver_failed)
        return;
    lx_host_once(&thread_queue_once, initialize_thread_queues);
    lx_pm_set_open32_thread_exit(thread_exit);
    ULONG receiver = WinCreateWindow(HWND_OBJECT, (PSZ)(uintptr_t)WC_STATIC, "", 0, 0, 0, 0, 0, 0,
                                     HWND_BOTTOM, 0, NULL, NULL);
    Open32ThreadQueue *queue = receiver ? calloc(1, sizeof *queue) : NULL;
    if (!queue || !lx_pm_mark_open32_window(receiver))
    {
        if (receiver)
            WinDestroyWindow(receiver);
        free(queue);
        thread_receiver_failed = TRUE;
        return;
    }
    queue->thread = GetCurrentThreadId();
    queue->window = receiver;
    thread_receiver = receiver;
    lx_host_monitor_lock(thread_queue_lock);
    queue->next = thread_queues;
    thread_queues = queue;
    lx_host_monitor_unlock(thread_queue_lock);
}

void open32UnregisterThreadQueue(void)
{
    if (!thread_receiver)
        return;
    lx_host_monitor_lock(thread_queue_lock);
    Open32ThreadQueue **link = &thread_queues;
    while (*link && (*link)->window != thread_receiver)
        link = &(*link)->next;
    Open32ThreadQueue *queue = *link;
    if (queue)
        *link = queue->next;
    lx_host_monitor_unlock(thread_queue_lock);
    WinDestroyWindow(thread_receiver);
    thread_receiver = 0;
    free(queue);
}

BOOL open32PostThreadPacket(ULONG thread, ULONG message)
{
    if (!thread_queue_lock)
        return FALSE;
    lx_host_monitor_lock(thread_queue_lock);
    ULONG receiver = 0;
    for (Open32ThreadQueue *queue = thread_queues; queue; queue = queue->next)
        if (queue->thread == thread)
        {
            receiver = queue->window;
            break;
        }
    BOOL result = receiver && lx_pm_post_open32_message(receiver, message, 0, 0);
    lx_host_monitor_unlock(thread_queue_lock);
    return result;
}

BOOL open32ThreadQueueWindow(ULONG window)
{
    if (!thread_queue_lock)
        return FALSE;
    BOOL found = FALSE;
    lx_host_monitor_lock(thread_queue_lock);
    for (Open32ThreadQueue *queue = thread_queues; queue && !found; queue = queue->next)
        found = queue->window == window;
    lx_host_monitor_unlock(thread_queue_lock);
    return found;
}

BOOL PostThreadMessage(ULONG thread, ULONG message, ULONG first, ULONG second)
{
    if (thread == GetCurrentThreadId())
        return PostMessage(0, message, first, second);
    lx_host_once(&thread_queue_once, initialize_thread_queues);
    lx_host_monitor_lock(thread_queue_lock);
    ULONG receiver = 0;
    for (Open32ThreadQueue *queue = thread_queues; queue; queue = queue->next)
        if (queue->thread == thread)
        {
            receiver = queue->window;
            break;
        }
    BOOL found = receiver != 0;
    BOOL result = found && lx_pm_post_open32_message(receiver, message, first, second);
    lx_host_monitor_unlock(thread_queue_lock);
    if (!result)
        open32SetError(found ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER);
    return result;
}

BOOL ReplyMessage(LONG result)
{
    return lx_pm_reply_message((uintptr_t)(ULONG)result);
}

BOOL WinXTranslateMsg(ULONG anchor, PVOID input, PVOID output)
{
    (void)anchor;
    if (!input || !output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8 *wire = input;
    QMSG message = {0};
    message.hwnd = lx_guest_load_u32(wire + GUEST_QMSG_HWND);
    message.msg = lx_guest_load_u32(wire + GUEST_QMSG_MSG);
    message.mp1 = (MPARAM)(uintptr_t)lx_guest_load_u32(wire + GUEST_QMSG_MP1);
    message.mp2 = (MPARAM)(uintptr_t)lx_guest_load_u32(wire + GUEST_QMSG_MP2);
    message.time = lx_guest_load_u32(wire + GUEST_QMSG_TIME);
    message.ptl.x = lx_guest_load_u32(wire + GUEST_QMSG_X);
    message.ptl.y = lx_guest_load_u32(wire + GUEST_QMSG_Y);
    message.reserved = lx_guest_load_u32(wire + GUEST_QMSG_RESERVED);
    ULONG kind, first, second;
    Open32Key key = {0};
    if (message.msg == WM_CHAR)
    {
        open32DecodeKey((ULONG)(uintptr_t)message.mp1, (ULONG)(uintptr_t)message.mp2, &key);
        open32UpdateKey(&key);
        kind = key.message;
        first = key.key;
        second = key.parameter;
    }
    else if (!open32PmMessage(&message, &kind, &first, &second))
        return FALSE;
    if (message.hwnd == thread_receiver && kind == OPEN32_DDE_PACKET)
    {
        open32DdeDeliver();
        return FALSE;
    }
    QueuedMessage *node =
        new_message(message.hwnd == thread_receiver ? 0 : open32PublicWindow(message.hwnd), kind,
                    first, second);
    if (!node)
        return FALSE;
    node->native = message;
    node->native_valid = message.msg != LX_PM_OPEN32_POST;
    node->keyboard = message.msg == WM_CHAR;
    node->key = key;
    lx_guest_store_u32(node->wire + WIN32_MSG_TIME, message.time);
    lx_guest_store_u32(node->wire + WIN32_MSG_X, (ULONG)message.ptl.x);
    lx_guest_store_u32(node->wire + WIN32_MSG_Y,
                       (ULONG)(WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN) - 1 - message.ptl.y));
    memcpy(output, node->wire, WIN32_MSG_SIZE);
    retain_history(node);
    TranslateMessage(output);
    return TRUE;
}
