/* Public domain. No rights reserved. Wholly AI-authored. */
/* Foreign posts are staged under a lock and consumed by the owning PM thread. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "host/sync.h"
#include "os2/ps/pmmerge/pmwin/open32_post.h"
#include "os2/include/private/pmp.h"
#include "host/mailbox.h"
#include "host/shared_memory.h"
#include "emulator/abi.h"

typedef struct Target
{
    struct Target *next;
    HWND window;
    AnchorBlock *anchor;
    Window *object;
    uint32 thread;
    BOOL open32;
} Target;

/* A function run on another queue's thread. The sender and the queue each hold a reference,
   so a reply can release the sender while the function is still running. The argument block
   is a private copy for the same reason, and is copied back only when the function finishes. */
enum
{
    CALL_WAITING,
    CALL_REPLIED,
    CALL_FINISHED,
    CALL_CANCELLED
};

typedef struct QueuedCall
{
    LxPmWindowCall function;
    uintptr_t result, reply;
    uint32 references, state;
    size_t size;
    uint8 data[];
} QueuedCall;

typedef struct Pending
{
    struct Pending *next;
    QueuedCall *call;
    AnchorBlock *anchor;
    HWND window;
    BOOL host_event;
    LxHostEvent event;
    QMSG message;
} Pending;

static LxHostOnce initialized = LX_HOST_ONCE_INIT;
static LxHostMutex *lock;
static Target *targets;
static Pending *pending, *tail;

/* Every message queue of this process, including those that own no window. */
typedef struct QueueEntry
{
    struct QueueEntry *next;
    AnchorBlock *anchor;
} QueueEntry;

static QueueEntry *queues;
static uint32 next_window = FIRST_HWND_VALUE - 1;
static uint32 window_prefix;
static LxHostMailbox *inbox;
static LxHostShared *live;
static uint32 *live_windows;
static _Thread_local QueuedCall *current_call;

enum
{
    LIVE_SIZE = 8192,
    MESSAGE_SIZE = 20,
    SESSION_BROADCAST = 0x0009 /* Send to frame windows only. */
};

static void mailboxName(char name[32], uint32 prefix)
{
    snprintf(name, 32, "pm-post-%04x", (unsigned)prefix);
}

static void liveName(char name[64], uint32 owner, uint32 prefix)
{
    snprintf(name, 64, "/2ine-pm-live-%u-%04x", (unsigned)owner, (unsigned)prefix);
}

static void initialize(void)
{
    lock = lx_host_mutex_create(0, 0);
    uint32 pid, parent;
    lx_host_process_ids(&pid, &parent);
    for (uint32 i = 0; lock && i < 0xfffe; i++)
    {
        uint32 prefix = (pid + i) % 0xfffe + 1, owner;
        char name[64];
        mailboxName(name, prefix);
        int result = lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 1, 0, &inbox, &owner);
        if (result == 2)
            continue;
        if (result)
            break;
        liveName(name, pid, prefix);
        if (lx_host_shared_open(name, LIVE_SIZE, &live) == LX_HOST_SHARED_OK &&
            (live_windows = lx_host_shared_map(live, NULL, 0)))
            window_prefix = prefix;
        if (!window_prefix)
        {
            if (live)
                lx_host_shared_close(live);
            lx_host_mailbox_close(inbox);
            inbox = NULL;
        }
        break;
    }
}

/* An anchor with no target left can no longer take its queue messages. */
static BOOL anchor_listed(const AnchorBlock *anchor)
{
    for (Target *target = targets; target; target = target->next)
        if (target->anchor == anchor)
            return TRUE;
    return FALSE;
}

HWND registerWindowMessages(AnchorBlock *anchor)
{
    lx_host_once(&initialized, initialize);
    Target *target = window_prefix ? calloc(1, sizeof *target) : NULL;
    if (!target)
        return 0;
    uint32 index = lx_host_atomic_increment(&next_window);
    if (index >= 0x10000)
    {
        free(target);
        return 0;
    }
    target->window = (window_prefix << 16) | index;
    target->anchor = anchor;
    target->thread = LX_GETTIB() ? lx_guest_load_u32(&LX_GETTIB2()->thread_id) : 0;
    lx_host_mutex_lock(lock, UINT32_MAX);
    target->next = targets;
    targets = target;
    __atomic_fetch_or(&live_windows[index / 32], 1u << (index % 32), __ATOMIC_RELEASE);
    lx_host_mutex_unlock(lock);
    return target->window;
}

static void release_call(QueuedCall *call)
{
    if (!__atomic_sub_fetch(&call->references, 1, __ATOMIC_ACQ_REL))
        free(call);
}

static void cancel_call(QueuedCall *call)
{
    uint32 expected = CALL_WAITING;
    __atomic_compare_exchange_n(&call->state, &expected, CALL_CANCELLED, 0, __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    release_call(call);
}

static void run_call(QueuedCall *call)
{
    QueuedCall *outer = current_call;
    current_call = call;
    call->result = call->function(call->data);
    current_call = outer;
    uint32 expected = CALL_WAITING;
    __atomic_compare_exchange_n(&call->state, &expected, CALL_FINISHED, 0, __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    release_call(call);
}

void bindWindowTarget(Window *window)
{
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target; target = target->next)
        if (target->window == window->hwnd)
        {
            target->object = window;
            break;
        }
    lx_host_mutex_unlock(lock);
}

/* Only pointers still registered are followed, so a window being destroyed is never read. */
static Target *object_target(const Window *window)
{
    for (Target *target = targets; target; target = target->next)
        if (target->object == window)
            return target;
    return NULL;
}

static HWND public_window(const Target *from, const Window *window)
{
    if (window == &desktop_window)
        return HWND_DESKTOP;
    if (from && window == (const Window *)&from->anchor->object_window)
        return HWND_OBJECT;
    Target *target = window ? object_target(window) : NULL;
    return target ? target->window : 0;
}

HWND foreignWindowQuery(HWND window, LONG code)
{
    if (!lock)
        return 0;
    HWND result = 0;
    lx_host_mutex_lock(lock, UINT32_MAX);
    Target *from = NULL;
    for (Target *target = targets; target; target = target->next)
        if (target->window == window)
        {
            from = target;
            break;
        }
    Window *object = from ? from->object : NULL;
    if (object)
    {
        Window *found = NULL;
        if (code == 0)
            found = object->sibling;
        else if (code == 2)
            found = object->children;
        else if (code == 4)
            found = object->owner;
        else if (code == 5)
            found = object->parent;
        else if (code == 1)
        {
            Window *parent = object->parent;
            for (Window *i = parent ? parent->children : NULL; i && i != object; i = i->sibling)
            {
                found = i;
                if (i != &desktop_window && !object_target(i))
                    break;
            }
        }
        else if (code == 3)
            for (Window *i = object->children; i && object_target(i); i = i->sibling)
                found = i;
        result = public_window(from, found);
    }
    lx_host_mutex_unlock(lock);
    return result;
}

BOOL windowOwnerThread(HWND window, ULONG *thread)
{
    if (!lock)
        return FALSE;
    BOOL found = FALSE;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target && !found; target = target->next)
        if (target->window == window)
        {
            *thread = target->thread;
            found = TRUE;
        }
    lx_host_mutex_unlock(lock);
    return found;
}

size_t lx_pm_object_windows(HWND *output, size_t capacity)
{
    lx_host_once(&initialized, initialize);
    if (!lock)
        return 0;
    size_t count = 0;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target; target = target->next)
        if (target->object && target->object->parent == (Window *)&target->anchor->object_window)
        {
            if (count < capacity)
                output[count] = target->window;
            count++;
        }
    lx_host_mutex_unlock(lock);
    return count;
}

static void append_pending(Pending *item)
{
    if (tail)
        tail->next = item;
    else
        pending = item;
    tail = item;
}

static QueuedCall *new_call(LxPmWindowCall function, const void *context, size_t size)
{
    QueuedCall *call = calloc(1, sizeof *call + size);
    if (call)
    {
        call->function = function;
        call->references = 2;
        call->size = size;
        memcpy(call->data, context, size);
    }
    return call;
}

/* The one queued-send primitive. Queues a call to a queue, and to a window of it when window
   is not zero; the lock must be held and the queue known to be live. finish_call waits. */
static BOOL start_call(AnchorBlock *anchor, HWND window, QueuedCall *call)
{
    Pending *item = calloc(1, sizeof *item);
    if (!item)
        return FALSE;
    item->anchor = anchor;
    item->window = window;
    item->call = call;
    append_pending(item);
    return TRUE;
}

/* No lock is held here. The waiter keeps draining its own queue, so a thread sending to it
   is served and two threads sending to each other both proceed. */
static uint32 finish_call(AnchorBlock *self, QueuedCall *call)
{
    uint32 state;
    while ((state = __atomic_load_n(&call->state, __ATOMIC_ACQUIRE)) == CALL_WAITING)
    {
        if (self)
            drainForeignMessages(self);
        lx_execution_poll_termination(GProcessState.execution);
        lx_host_sleep_ms(1);
    }
    return state;
}

BOOL lx_pm_window_call(HWND window, LxPmWindowCall function, void *context, size_t size,
                       uintptr_t *result)
{
    lx_host_once(&initialized, initialize);
    AnchorBlock *self = getAnchorBlockNoHAB();
    QueuedCall *call = lock ? new_call(function, context, size) : NULL;
    if (!call)
        return FALSE;
    AnchorBlock *owner = NULL;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target; target = target->next)
        if (target->window == window)
        {
            owner = target->anchor;
            break;
        }
    BOOL queued = owner && owner != self && start_call(owner, window, call);
    lx_host_mutex_unlock(lock);
    if (!queued)
    {
        free(call);
        return FALSE;
    }
    LxHostEvent wake = {0};
    lx_host_video_push(&wake);
    uint32 state = finish_call(self, call);
    if (state == CALL_FINISHED)
    {
        memcpy(context, call->data, size);
        *result = call->result;
    }
    else if (state == CALL_REPLIED)
        *result = call->reply;
    release_call(call);
    return state == CALL_FINISHED || state == CALL_REPLIED;
}

BOOL lx_pm_reply_message(uintptr_t result)
{
    QueuedCall *call = current_call;
    if (!call)
        return FALSE;
    uint32 expected = CALL_WAITING;
    call->reply = result;
    return __atomic_compare_exchange_n(&call->state, &expected, CALL_REPLIED, 0, __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
}

BOOL lx_pm_in_window_call(void)
{
    return current_call != NULL;
}

void registerMessageQueue(AnchorBlock *anchor)
{
    lx_host_once(&initialized, initialize);
    QueueEntry *entry = lock ? calloc(1, sizeof *entry) : NULL;
    if (!entry)
        return;
    entry->anchor = anchor;
    lx_host_mutex_lock(lock, UINT32_MAX);
    entry->next = queues;
    queues = entry;
    lx_host_mutex_unlock(lock);
}

/* A destroyed queue or an exiting thread can run nothing more: all its work is cancelled. */
void retireAnchorMessages(AnchorBlock *anchor)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (QueueEntry **entry = &queues; *entry; entry = &(*entry)->next)
        if ((*entry)->anchor == anchor)
        {
            QueueEntry *found = *entry;
            *entry = found->next;
            free(found);
            break;
        }
    Pending **link = &pending;
    tail = NULL;
    while (*link)
    {
        Pending *item = *link;
        if (item->anchor != anchor)
        {
            tail = item;
            link = &item->next;
            continue;
        }
        *link = item->next;
        if (item->call)
            cancel_call(item->call);
        free(item);
    }
    lx_host_mutex_unlock(lock);
}

void unregisterWindowMessages(HWND window)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    if ((window >> 16) == window_prefix)
        __atomic_fetch_and(&live_windows[(window & 0xffff) / 32], ~(1u << (window % 32)),
                           __ATOMIC_RELEASE);
    AnchorBlock *owner = NULL;
    for (Target **link = &targets; *link; link = &(*link)->next)
        if ((*link)->window == window)
        {
            Target *target = *link;
            owner = target->anchor;
            *link = target->next;
            free(target);
            break;
        }
    Pending **link = &pending;
    tail = NULL;
    while (*link)
    {
        Pending *item = *link;
        if (item->window == window && item->host_event && item->event.type == LX_HOST_EVENT_QUIT)
        {
            /* Quit targets the owner's queue, not this particular popup or
               child. Keep it attached to a surviving window; removing the
               owner's final window still retires it before the anchor dies. */
            for (Target *replacement = targets; replacement; replacement = replacement->next)
                if (replacement->anchor == item->anchor)
                {
                    item->window = replacement->window;
                    break;
                }
        }
        /* A queue losing its last window may be about to stop pumping, so work addressed to
           it ends here too; a sender must not hang on it. Queues never given a window are
           untouched, and leave only through retireAnchorMessages. */
        if (item->window == window ||
            (!item->window && item->anchor == owner && !anchor_listed(owner)))
        {
            *link = item->next;
            if (item->call)
                cancel_call(item->call);
            free(item);
        }
        else
        {
            tail = item;
            link = &item->next;
        }
    }
    lx_host_mutex_unlock(lock);
}

static BOOL post_packet(HWND window, ULONG message, MPARAM first, MPARAM second, ULONG reserved,
                        BOOL require_open32)
{
    lx_host_once(&initialized, initialize);
    if (!lock || !window || window < 0x10000 || window >= 0xffff0000u)
        return FALSE;
    if ((window >> 16) != window_prefix)
    {
        if (require_open32)
            return FALSE;
        if ((uintptr_t)first > UINT32_MAX || (uintptr_t)second > UINT32_MAX)
            return FALSE;
        char name[64];
        mailboxName(name, window >> 16);
        LxHostMailbox *destination;
        uint32 owner;
        if (lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &destination, &owner))
            return FALSE;
        liveName(name, owner, window >> 16);
        LxHostShared *state = NULL;
        uint32 *bits = NULL;
        BOOL result = FALSE;
        if (lx_host_shared_open(name, 0, &state) == LX_HOST_SHARED_OK &&
            lx_host_shared_size(state) == LIVE_SIZE &&
            (bits = lx_host_shared_map(state, NULL, 0)) &&
            (__atomic_load_n(&bits[(window & 0xffff) / 32], __ATOMIC_ACQUIRE) &
             (1u << (window % 32))))
        {
            uint8 wire[MESSAGE_SIZE];
            uint32 words[] = {window, message, (uint32)(uintptr_t)first, (uint32)(uintptr_t)second,
                              currentSystemTicks()};
            for (unsigned i = 0; i < 5; i++)
                lx_guest_store_u32(wire + i * 4, words[i]);
            result = lx_host_mailbox_send(destination, wire, sizeof wire, 0) == 0;
        }
        if (bits)
            lx_host_shared_unmap(bits, LIVE_SIZE);
        if (state)
            lx_host_shared_close(state);
        lx_host_mailbox_close(destination);
        return result;
    }
    Pending *item = calloc(1, sizeof *item);
    if (!item)
        return FALSE;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target; target = target->next)
        if (target->window == window && (!require_open32 || target->open32))
        {
            item->anchor = target->anchor;
            break;
        }
    if (!item->anchor)
    {
        lx_host_mutex_unlock(lock);
        free(item);
        return FALSE;
    }
    item->message = (QMSG){window, message, first, second, currentSystemTicks(), {0, 0}, reserved};
    item->window = window;
    if (tail)
        tail->next = item;
    else
        pending = item;
    tail = item;
    lx_host_mutex_unlock(lock);
    LxHostEvent wake = {0};
    lx_host_video_push(&wake);
    return TRUE;
}

BOOL windowMessageTargetExists(HWND window)
{
    lx_host_once(&initialized, initialize);
    if (!lock || window < 0x10000 || window >= 0xffff0000u)
        return FALSE;
    if ((window >> 16) == window_prefix)
    {
        BOOL found = FALSE;
        lx_host_mutex_lock(lock, UINT32_MAX);
        for (Target *target = targets; target; target = target->next)
            if (target->window == window)
            {
                found = TRUE;
                break;
            }
        lx_host_mutex_unlock(lock);
        return found;
    }
    char name[64];
    mailboxName(name, window >> 16);
    LxHostMailbox *mailbox;
    uint32 owner;
    if (lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &mailbox, &owner))
        return FALSE;
    liveName(name, owner, window >> 16);
    LxHostShared *state = NULL;
    uint32 *bits = NULL;
    BOOL found = FALSE;
    if (lx_host_shared_open(name, 0, &state) == LX_HOST_SHARED_OK &&
        lx_host_shared_size(state) == LIVE_SIZE && (bits = lx_host_shared_map(state, NULL, 0)))
        found = !!(__atomic_load_n(&bits[(window & 0xffff) / 32], __ATOMIC_ACQUIRE) &
                   (1u << (window % 32)));
    if (bits)
        lx_host_shared_unmap(bits, LIVE_SIZE);
    if (state)
        lx_host_shared_close(state);
    lx_host_mailbox_close(mailbox);
    return found;
}

BOOL lx_pm_host_event(const LxHostEvent *event)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !event)
        return FALSE;
    if (event->type == LX_HOST_EVENT_QUIT && lock)
    {
        /* SDL's queue belongs to the process. A PM worker may poll Quit before
           the application's window thread, so notify each window-owning queue
           once rather than consuming it only on the polling thread. */
        BOOL staged = FALSE;
        lx_host_mutex_lock(lock, UINT32_MAX);
        for (Target *target = targets; target; target = target->next)
        {
            AnchorBlock *owner = target->anchor;
            if (owner == anchor || !owner->message_queue.hmq)
                continue;
            Target *earlier = targets;
            while (earlier != target && earlier->anchor != owner)
                earlier = earlier->next;
            if (earlier != target)
                continue;
            Pending *item = calloc(1, sizeof *item);
            if (!item)
                continue;
            item->anchor = owner;
            /* A representative window ties delivery to the owner's lifetime.
               unregisterWindowMessages transfers Quit if this window closes. */
            item->window = target->window;
            item->host_event = TRUE;
            item->event = *event;
            if (tail)
                tail->next = item;
            else
                pending = item;
            tail = item;
            staged = TRUE;
        }
        lx_host_mutex_unlock(lock);
        if (staged)
        {
            LxHostEvent wake = {0};
            lx_host_video_push(&wake);
        }
        return processHostEvent(anchor, event) || staged;
    }
    Window *window = event->window_data;
    if (!window || !window->hwnd || !lock)
        return processHostEvent(anchor, event);
    lx_host_mutex_lock(lock, UINT32_MAX);
    AnchorBlock *owner = NULL;
    for (Target *target = targets; target; target = target->next)
        if (target->window == window->hwnd)
        {
            owner = target->anchor;
            break;
        }
    if (!owner || owner == anchor)
    {
        lx_host_mutex_unlock(lock);
        return owner ? processHostEvent(anchor, event) : FALSE;
    }
    Pending *item = calloc(1, sizeof *item);
    if (item)
    {
        item->anchor = owner;
        item->window = window->hwnd;
        item->host_event = TRUE;
        item->event = *event;
        if (tail)
            tail->next = item;
        else
            pending = item;
        tail = item;
    }
    lx_host_mutex_unlock(lock);
    if (item)
    {
        LxHostEvent wake = {0};
        lx_host_video_push(&wake);
    }
    return item != NULL;
}

void drainForeignMessages(AnchorBlock *anchor)
{
    if (!lock)
        return;
    if (inbox)
    {
        uint8 wire[MESSAGE_SIZE];
        size_t size;
        unsigned priority;
        BOOL session_ending = FALSE;
        for (;;)
        {
            uint32 token = 0;
            if (lx_host_mailbox_receive(inbox, wire, sizeof wire, &size, &priority, &token, 0))
                break;
            if (size != sizeof wire)
                continue;
            HWND window = lx_guest_load_u32(wire);
            if (!window && lx_guest_load_u32(wire + 4) == LX_PM_SESSION_END)
            {
                session_ending = TRUE;
                continue;
            }
            /* The local registry validates lifetime and assigns the owning thread. */
            if ((window >> 16) == window_prefix)
                postForeignMessage(window, lx_guest_load_u32(wire + 4),
                                   (MPARAM)(uintptr_t)lx_guest_load_u32(wire + 8),
                                   (MPARAM)(uintptr_t)lx_guest_load_u32(wire + 12));
        }
        if (session_ending)
        {
            /* Host Quit reaches every queue here and honours WinCancelShutdown. */
            LxHostEvent quit = {0};
            quit.type = LX_HOST_EVENT_QUIT;
            WinBroadcastMsg(HWND_DESKTOP, WM_SAVEAPPLICATION, 0, 0, SESSION_BROADCAST);
            lx_pm_host_event(&quit);
        }
    }
    lx_host_mutex_lock(lock, UINT32_MAX);
    Pending **link = &pending;
    Pending *ready = NULL, **ready_tail = &ready;
    tail = NULL;
    while (*link)
    {
        Pending *item = *link;
        if (item->anchor == anchor)
        {
            *link = item->next;
            item->next = NULL;
            *ready_tail = item;
            ready_tail = &item->next;
        }
        else
        {
            tail = item;
            link = &item->next;
        }
    }
    lx_host_mutex_unlock(lock);
    while (ready)
    {
        Pending *item = ready;
        ready = item->next;
        /* A queue message has no window, so it needs no window lifetime check. */
        if (item->call)
        {
            if (!item->window || getWindowFromHWND(anchor, item->window))
                run_call(item->call);
            else
                cancel_call(item->call);
        }
        else if (!item->window || getWindowFromHWND(anchor, item->window))
        {
            if (item->host_event)
                processHostEvent(anchor, &item->event);
            else
                postMessage(anchor, &item->message);
        }
        free(item);
    }
}

/* Runs on the receiving queue's thread, so the wait covers delivery into that queue. */
static uintptr_t deliver_queue_message(void *context)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (anchor)
        postMessage(anchor, context);
    return 1;
}

BOOL broadcastQueues(AnchorBlock *self, ULONG message, MPARAM first, MPARAM second, BOOL wait)
{
    lx_host_once(&initialized, initialize);
    BOOL ok = TRUE;
    /* A queue that always cancels shutdown never receives WM_QUIT. */
    if (message != WM_QUIT || !self->message_queue.cancel_shutdown)
        ok = WinPostMsg(0, message, first, second);
    if (!lock)
        return ok;
    QMSG packet = {0, message, first, second, currentSystemTicks(), {0, 0}, 0};
    QueuedCall **calls = NULL;
    size_t count = 0, capacity = 0;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (QueueEntry *entry = queues; entry; entry = entry->next)
    {
        AnchorBlock *anchor = entry->anchor;
        if (anchor == self || (message == WM_QUIT && anchor->message_queue.cancel_shutdown))
            continue;
        if (count == capacity)
        {
            size_t next = capacity ? capacity * 2 : 8;
            QueuedCall **grown = realloc(calls, next * sizeof *grown);
            if (!grown)
            {
                ok = FALSE;
                break;
            }
            calls = grown;
            capacity = next;
        }
        QueuedCall *call = new_call(deliver_queue_message, &packet, sizeof packet);
        if (!call || !start_call(anchor, 0, call))
        {
            free(call);
            ok = FALSE;
            continue;
        }
        calls[count++] = call;
    }
    lx_host_mutex_unlock(lock);
    LxHostEvent wake = {0};
    lx_host_video_push(&wake);
    /* A post-only broadcast drops the sender's interest at once and returns. */
    for (size_t i = 0; i < count; i++)
    {
        if (wait)
            finish_call(self, calls[i]);
        release_call(calls[i]);
    }
    free(calls);
    return ok;
}

typedef struct SessionEnd
{
    char own[32];
    BOOL failed;
} SessionEnd;

static void deliver_session_end(const char *name, void *context)
{
    SessionEnd *end = context;
    if (!strcmp(name, end->own))
        return;
    LxHostMailbox *destination;
    uint32 owner;
    int opened = lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &destination, &owner);
    if (opened == 1)
        return; /* The owner's lifetime lock is gone, so that worker has already ended. */
    uint8 wire[MESSAGE_SIZE] = {0};
    lx_guest_store_u32(wire + 4, LX_PM_SESSION_END);
    if (opened || lx_host_mailbox_send(destination, wire, sizeof wire, 0))
        end->failed = TRUE;
    if (!opened)
        lx_host_mailbox_close(destination);
}

BOOL lx_pm_end_session(void)
{
    const char *directory = getenv("TWOINE_IPC_DIR");
    if (!directory || !*directory)
        return TRUE;
    lx_host_once(&initialized, initialize);
    SessionEnd end = {.failed = FALSE};
    mailboxName(end.own, window_prefix);
    return !lx_host_mailbox_each(directory, "pm-post-", deliver_session_end, &end) && !end.failed;
}

BOOL postForeignMessage(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    return post_packet(window, message, first, second, 0, FALSE);
}

BOOL lx_pm_mark_open32_window(HWND window)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !lock)
        return FALSE;
    BOOL found = FALSE;
    lx_host_mutex_lock(lock, UINT32_MAX);
    for (Target *target = targets; target; target = target->next)
        if (target->window == window && target->anchor == anchor)
        {
            target->open32 = TRUE;
            found = TRUE;
            break;
        }
    lx_host_mutex_unlock(lock);
    return found;
}

BOOL lx_pm_post_open32_message(HWND window, ULONG message, ULONG first, ULONG second)
{
    return post_packet(window, LX_PM_OPEN32_POST, (MPARAM)(uintptr_t)first,
                       (MPARAM)(uintptr_t)second, message, TRUE);
}
