/* Public domain. No rights reserved. Wholly AI-authored. */
/* Typed resource ownership and generation-checked OS/2 handles. */
#include "handles.h"
#include "../runtime/host_sync.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

typedef struct Resource {
    void *value;
    void (*destroy)(void *);
    size_t references;
    size_t handles;
    LxHandleKind kind;
    struct Resource *next;
} Resource;
typedef struct Slot { Resource *resource; uint16_t generation; } Slot;
/* Index zero and the 16-bit special-handle range are never returned. */
#define SLOT_COUNT 65535u
static Slot Slots[SLOT_COUNT];
static uint32_t NextSlot = 1;
static Resource *Resources;
static LxHostMutex *Mutex;
static LxHostOnce Once = LX_HOST_ONCE_INIT;
static void initializeMutex(void) { Mutex = lx_host_mutex_create(0, 0); if (!Mutex) abort(); }
static void lock(void)
{
    lx_host_once(&Once, initializeMutex);
    LxHostLockResult result = lx_host_mutex_lock(Mutex, UINT32_MAX);
    if (result != LX_HOST_LOCK_OK && result != LX_HOST_LOCK_OWNER_DIED) abort();
}

uint32_t lx_handle_create(LxHandleKind kind, void *value, void (*destroy)(void *))
{
    if (!value || kind < LX_HANDLE_MODULE || kind > LX_HANDLE_FILE) return 0;
    lock();
    uint32_t index = NextSlot;
    for (unsigned i = 0; i < SLOT_COUNT - 1; i++) {
        if (!Slots[index].resource && Slots[index].generation != UINT16_MAX) break;
        if (++index == SLOT_COUNT) index = 1;
    }
    Slot *slot = &Slots[index];
    if (slot->resource || slot->generation == UINT16_MAX) {
        lx_host_mutex_unlock(Mutex); return 0;
    }
    Resource *r;
    for (r = Resources; r && r->value != value; r = r->next) {}
    if (r && (r->kind != kind || r->destroy != destroy)) { lx_host_mutex_unlock(Mutex); return 0; }
    if (!r) {
        r = calloc(1, sizeof *r);
        if (!r) { lx_host_mutex_unlock(Mutex); return 0; }
        r->kind = kind; r->value = value; r->destroy = destroy;
        r->next = Resources; Resources = r;
    }
    r->references++; r->handles++;
    slot->generation++; slot->resource = r;
    NextSlot = index + 1; if (NextSlot == SLOT_COUNT) NextSlot = 1;
    uint32_t handle = ((uint32_t) slot->generation << 16) | index;
    lx_host_mutex_unlock(Mutex);
    return handle;
}
static Resource *resolve(uint32_t handle, LxHandleKind kind)
{
    uint32_t index = handle & UINT16_MAX;
    if (!index || index >= SLOT_COUNT || Slots[index].generation != (handle >> 16)) return NULL;
    Resource *r = Slots[index].resource;
    return r && r->kind == kind ? r : NULL;
}
int lx_handle_borrow(uint32_t handle, LxHandleKind kind, LxHandleBorrow *borrow)
{
    if (!borrow) return 0;
    memset(borrow, 0, sizeof *borrow);
    lock();
    Resource *r = resolve(handle, kind);
    if (r) { r->references++; borrow->resource = r->value; borrow->lease = r; }
    lx_host_mutex_unlock(Mutex); return r != NULL;
}
/* Drop under the table lock, destroy outside it. Destructors may close handles. */
static Resource *drop(Resource *r)
{
    if (--r->references) return NULL;
    Resource **link = &Resources;
    while (*link != r) link = &(*link)->next;
    *link = r->next; return r;
}
static void dispose(Resource *r)
{
    if (r) { if (r->destroy) r->destroy(r->value); free(r); }
}
void lx_handle_release(LxHandleBorrow *borrow)
{
    if (!borrow || !borrow->lease) return;
    lock();
    Resource *last = drop(borrow->lease);
    memset(borrow, 0, sizeof *borrow);
    lx_host_mutex_unlock(Mutex); dispose(last);
}
int lx_handle_close_guarded(uint32_t handle, LxHandleKind kind, size_t allowed,
                             int (*guard)(void *))
{
    lock();
    Resource *r = resolve(handle, kind);
    Resource *last = NULL;
    int success = r != NULL;
    if (r && (r->references - r->handles > allowed || (guard && !guard(r->value)))) success = -1;
    if (success == 1) {
        Slots[handle & UINT16_MAX].resource = NULL; r->handles--; last = drop(r);
    }
    lx_host_mutex_unlock(Mutex); dispose(last); return success;
}
int lx_handle_close(uint32_t handle, LxHandleKind kind)
{
    return lx_handle_close_guarded(handle, kind, SIZE_MAX, NULL) == 1;
}
size_t lx_handle_count(LxHandleKind kind, const void *resource)
{
    lock();
    size_t count = 0;
    for (Resource *r = Resources; r; r = r->next)
        if (r->kind == kind && r->value == resource) { count = r->handles; break; }
    lx_host_mutex_unlock(Mutex); return count;
}
