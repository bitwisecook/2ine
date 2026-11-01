/* Public domain. No rights reserved. Wholly AI-authored. */
/* Foreign posts are staged under a lock and consumed by the owning PM thread. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "hal/host_sync.h"
#include "hal/host_mailbox.h"
#include "hal/host_shared.h"
#include "guest/abi.h"
#include "os2libs/pmwin/open32_post.h"
typedef struct Target
{
    struct Target *next;
    HWND window;
    AnchorBlock *anchor;
    BOOL open32;
} Target;
typedef struct Pending
{
    struct Pending *next;
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
static uint32 next_window = FIRST_HWND_VALUE - 1;
static uint32 window_prefix;
static LxHostMailbox *inbox;
static LxHostShared *live;
static uint32 *live_windows;
enum
{
    LIVE_SIZE = 8192,
    MESSAGE_SIZE = 20
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
    lx_host_mutex_lock(lock, UINT32_MAX);
    target->next = targets;
    targets = target;
    __atomic_fetch_or(&live_windows[index / 32], 1u << (index % 32), __ATOMIC_RELEASE);
    lx_host_mutex_unlock(lock);
    return target->window;
}
void unregisterWindowMessages(HWND window)
{
    if (!lock)
        return;
    lx_host_mutex_lock(lock, UINT32_MAX);
    if ((window >> 16) == window_prefix)
        __atomic_fetch_and(&live_windows[(window & 0xffff) / 32], ~(1u << (window % 32)),
                           __ATOMIC_RELEASE);
    for (Target **link = &targets; *link; link = &(*link)->next)
        if ((*link)->window == window)
        {
            Target *target = *link;
            *link = target->next;
            free(target);
            break;
        }
    Pending **link = &pending;
    tail = NULL;
    while (*link)
    {
        Pending *item = *link;
        if (item->window == window)
        {
            *link = item->next;
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
                        BOOL open32)
{
    lx_host_once(&initialized, initialize);
    if (!lock || !window || window < 0x10000 || window >= 0xffff0000u)
        return FALSE;
    if ((window >> 16) != window_prefix)
    {
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
        if (target->window == window && (!open32 || target->open32))
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
BOOL postForeignMessage(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    return post_packet(window, message, first, second, 0, FALSE);
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
        for (;;)
        {
            uint32 token = 0;
            if (lx_host_mailbox_receive(inbox, wire, sizeof wire, &size, &priority, &token, 0))
                break;
            if (size != sizeof wire)
                continue;
            HWND window = lx_guest_load_u32(wire);
            /* The local registry validates lifetime and assigns the owning thread. */
            if ((window >> 16) == window_prefix)
                postForeignMessage(window, lx_guest_load_u32(wire + 4),
                                   (MPARAM)(uintptr_t)lx_guest_load_u32(wire + 8),
                                   (MPARAM)(uintptr_t)lx_guest_load_u32(wire + 12));
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
        if (getWindowFromHWND(anchor, item->window))
        {
            if (item->host_event)
                processHostEvent(anchor, &item->event);
            else
                postMessage(anchor, &item->message);
        }
        free(item);
    }
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
