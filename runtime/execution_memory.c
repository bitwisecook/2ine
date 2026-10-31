/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest mapping ownership, independent of the host address chosen for backing. */
#include "execution_internal.h"
#include "host.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Guest [address, address+size) -> host backing[0..size).
   Active borrows pin this translation against protect/unmap. */
struct LxGuestMapping {
    uint32_t address;
    size_t size;
    unsigned permissions;
    size_t borrowers;
    int shared;
    void *backing;
    LxGuestMapping *next;
};
#define GUEST_PAGE 4096u
#define GUEST_MAP_END UINT32_C(0xfffe0000)
#define GUEST_ALLOC_START UINT32_C(0x20000000)

static int available(LxExecution *e, uint32_t a, size_t n)
{
    if (!a || a % GUEST_PAGE || !n || n % GUEST_PAGE ||
        n > GUEST_MAP_END || (uint64_t) a + n > GUEST_MAP_END) return 0;
    for (LxGuestMapping *m = e->memory; m; m = m->next)
        if ((uint64_t) a < (uint64_t) m->address + m->size &&
            (uint64_t) m->address < (uint64_t) a + n) return 0;
    return 1;
}
static LxGuestMapping *map_unlocked(LxExecution *e, uint32_t address, size_t size,
                                 unsigned permissions, int exact, LxHostShared *shared)
{
    if (!size || size % GUEST_PAGE || (permissions & ~7u) || (!address && exact)) {
        lx_execution_fail(e, "Invalid guest mapping request"); return NULL;
    }
    LxGuestMapping *m = calloc(1, sizeof *m);
    if (!m) { lx_execution_fail(e, "Out of memory"); return NULL; }
    uint64_t candidate = exact ? address : (address ? address :
        (e->next_address > GUEST_ALLOC_START ? e->next_address : GUEST_ALLOC_START));
    /* The public guest page is 4 KiB. The host may require larger allocations. */
    const size_t host_page = lx_host_page_size();
    const size_t alignment = host_page > GUEST_PAGE ? host_page : GUEST_PAGE;
    if (!exact) candidate = (candidate + alignment - 1) & ~(uint64_t) (alignment - 1);
    for (; candidate < GUEST_MAP_END && size <= GUEST_MAP_END - candidate;
         candidate += alignment) {
        if (available(e, (uint32_t) candidate, size)) {
            if (shared) {
                m->backing = lx_host_shared_map(shared, e->ops->identity_memory ? (void *) (uintptr_t) candidate : NULL,
                                                 e->ops->identity_memory);
                m->shared = 1;
            } else if (e->ops->identity_memory) {
                m->backing = lx_host_vm_map((void *) (uintptr_t) candidate, size, permissions, 1);
            } else {
                m->backing = lx_host_memory_alloc(size);
            }
            if (m->backing) break;
        }
        if (exact) break;
    }
    if (!m->backing) { free(m); lx_execution_fail(e, "Guest address range unavailable"); return NULL; }
    m->address = (uint32_t) candidate; m->size = size; m->permissions = permissions;
    if (e->ops->map && !e->ops->map(e, m->address, size, permissions, m->backing)) {
        if (m->shared) lx_host_shared_unmap(m->backing, size);
        else lx_host_memory_free(m->backing, size);
        free(m); return NULL;
    }
    m->next = e->memory; e->memory = m;
    if (!exact && !address) e->next_address = m->address + (uint32_t) size;
    return m;
}
static LxGuestMapping *mapping_at_unlocked(LxExecution *e, uint32_t address)
{
    for (LxGuestMapping *m = e->memory; m; m = m->next)
        if (m->address == address) return m;
    return NULL;
}
uint32_t lx_execution_mapping_address(const LxGuestMapping *m) { return m->address; }
void *lx_execution_mapping_host(const LxGuestMapping *m) { return m->backing; }
size_t lx_execution_mapping_size(const LxGuestMapping *m) { return m->size; }
static LxGuestMapping **owned(LxExecution *e, const LxGuestMapping *m)
{
    for (LxGuestMapping **p = &e->memory; *p; p = &(*p)->next)
        if (*p == m) return p;
    lx_execution_fail(e, "Guest mapping does not belong to this execution instance");
    return NULL;
}
static int protect_unlocked(LxExecution *e, LxGuestMapping *m, unsigned permissions)
{
    if (!owned(e, m) || (permissions & ~7u)) return 0;
    if (m == e->tls_window) return lx_execution_fail(e, "Thread window permissions are fixed");
    if (m->borrowers) return lx_execution_fail(e, "Guest mapping has an active host borrow");
    if (e->ops->protect && !e->ops->protect(e, m->address, m->size, permissions)) return 0;
    if (e->ops->identity_memory && !lx_host_vm_protect(m->backing, m->size, permissions))
        return lx_execution_fail(e, "Host mapping protection failed");
    m->permissions = permissions;
    return 1;
}
static int unmap_unlocked(LxExecution *e, LxGuestMapping *m)
{
    LxGuestMapping **p = owned(e, m);
    if (!p) return 0;
    if (m->borrowers) return lx_execution_fail(e, "Guest mapping has an active host borrow");
    if (e->ops->unmap && !e->ops->unmap(e, m->address, m->size)) return 0;
    *p = m->next;
    if (m->shared) lx_host_shared_unmap(m->backing, m->size);
    else lx_host_memory_free(m->backing, m->size);
    free(m);
    return 1;
}
static void *memory_unlocked(LxExecution *e, uint32_t address, size_t size, unsigned permissions)
{
    if ((permissions & ~7u) || (uint64_t) size > UINT64_C(0x100000000) - address) {
        lx_execution_fail(e, "Guest pointer range overflows"); return NULL;
    }
    for (LxGuestMapping *m = e->memory; m; m = m->next)
        if (address >= m->address && (uint64_t) address + size <= (uint64_t) m->address + m->size &&
            (permissions & m->permissions) == permissions)
        {
            const size_t offset = address - m->address;
            if (m == e->tls_window) {
                void *tib = e->ops->current_tib();
                if (!tib || offset < e->services.tls_offset ||
                    offset + size > (uint64_t) e->services.tls_offset + 128) break;
                return (uint8_t *) tib + offset;
            }
            return (uint8_t *) m->backing + offset;
        }
    lx_execution_fail(e, "Guest pointer range is unmapped or inaccessible"); return NULL;
}
static int address_unlocked(LxExecution *e, const void *host, size_t size,
                          unsigned permissions, uint32_t *address)
{
    uintptr_t p = (uintptr_t) host;
    if (!address || (permissions & ~7u) || size > UINTPTR_MAX - p)
        return lx_execution_fail(e, "Invalid host buffer range");
    for (LxGuestMapping *m = e->memory; m; m = m->next) {
        uintptr_t base = (uintptr_t) m->backing;
        if (p >= base && p - base <= m->size && size <= m->size - (p - base) &&
            (permissions & m->permissions) == permissions) {
            *address = m->address + (uint32_t) (p - base); return 1;
        }
    }
    return lx_execution_fail(e, "Host buffer is not guest memory");
}

LxGuestMapping * lx_execution_map(LxExecution *e, uint32_t address, size_t size, unsigned permissions, int exact)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) {
        lx_execution_fail(e, "Cannot acquire guest memory lock"); return 0;
    }
    LxGuestMapping * result = map_unlocked(e, address, size, permissions, exact, NULL);
    lx_host_mutex_unlock(e->memory_mutex);
    return result;
}

LxGuestMapping * lx_execution_mapping_at(LxExecution *e, uint32_t address)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) {
        lx_execution_fail(e, "Cannot acquire guest memory lock"); return 0;
    }
    LxGuestMapping * result = mapping_at_unlocked(e, address);
    lx_host_mutex_unlock(e->memory_mutex);
    return result;
}

int  lx_execution_protect(LxExecution *e, LxGuestMapping *m, unsigned permissions)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) {
        lx_execution_fail(e, "Cannot acquire guest memory lock"); return 0;
    }
    int  result = protect_unlocked(e, m, permissions);
    lx_host_mutex_unlock(e->memory_mutex);
    return result;
}

int  lx_execution_unmap(LxExecution *e, LxGuestMapping *m)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) {
        lx_execution_fail(e, "Cannot acquire guest memory lock"); return 0;
    }
    int  result = unmap_unlocked(e, m);
    lx_host_mutex_unlock(e->memory_mutex);
    return result;
}

void * lx_execution_memory(LxExecution *e, uint32_t address, size_t size, unsigned permissions)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) {
        lx_execution_fail(e, "Cannot acquire guest memory lock"); return 0;
    }
    void * result = memory_unlocked(e, address, size, permissions);
    lx_host_mutex_unlock(e->memory_mutex);
    return result;
}

int  lx_execution_address(LxExecution *e, const void *host, size_t size, unsigned permissions, uint32_t *address)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) {
        lx_execution_fail(e, "Cannot acquire guest memory lock"); return 0;
    }
    int  result = address_unlocked(e, host, size, permissions, address);
    lx_host_mutex_unlock(e->memory_mutex);
    return result;
}

int lx_execution_atomic_u32(LxExecution *e, uint32_t address, int exchange,
                            uint32_t value, uint32_t *previous)
{
    if (!previous || lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return 0;
    void *host = memory_unlocked(e, address, 4, LX_GUEST_READ | LX_GUEST_WRITE);
    int ok = host && e->ops->atomic_u32(e, address, host, exchange, value, previous);
    lx_host_mutex_unlock(e->memory_mutex);
    return ok;
}

int lx_execution_borrow(LxExecution *e, uint32_t address, size_t size,
                         unsigned permissions, LxGuestBorrow *borrow)
{
    if (!borrow) return 0;
    memset(borrow, 0, sizeof *borrow);
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return 0;
    void *host = memory_unlocked(e, address, size, permissions);
    if (host) {
        uint32_t backing_address = address;
        if (e->tls_window && address >= e->tls_address && (uint64_t) address + size <= (uint64_t) e->tls_address + 128)
            if (!address_unlocked(e, host, size, permissions, &backing_address)) host = NULL;
        for (LxGuestMapping *m = e->memory; host && m; m = m->next)
            if (backing_address >= m->address && (uint64_t) backing_address + size <= (uint64_t) m->address + m->size) {
                m->borrowers++; borrow->host = host; borrow->execution = e; borrow->mapping = m; break;
            }
    }
    lx_host_mutex_unlock(e->memory_mutex); return borrow->mapping != NULL;
}
void lx_execution_release(LxGuestBorrow *borrow)
{
    if (!borrow || !borrow->mapping) return;
    LxExecution *e = borrow->execution;
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) abort();
    borrow->mapping->borrowers--;
    memset(borrow, 0, sizeof *borrow);
    lx_host_mutex_unlock(e->memory_mutex);
}

static size_t readable_extent(LxExecution *e, LxGuestMapping *m, uint32_t address, size_t maximum)
{
    uint64_t end = (uint64_t) m->address + m->size;
    if (m == e->tls_window) {
        uint32_t start = m->address + e->services.tls_offset;
        if (address < start || address >= start + 128u) return 0;
        end = (uint64_t) start + 128;
    }
    size_t extent = (size_t) (end - address);
    return extent < maximum ? extent : maximum;
}

char *lx_execution_copy_string(LxExecution *e, uint32_t address, size_t maximum)
{
    if (!maximum || lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return NULL;
    char *result = NULL;
    for (LxGuestMapping *m = e->memory; m; m = m->next) {
        if (address < m->address || (uint64_t) address >= (uint64_t) m->address + m->size ||
            !(m->permissions & LX_GUEST_READ)) continue;
        size_t n = readable_extent(e, m, address, maximum); if (!n) break;
        result = malloc(n);
        if (!result) break;
        void *source = memory_unlocked(e, address, n, LX_GUEST_READ);
        if (!source) { free(result); result = NULL; break; }
        e->ops->copy_out(e, source, result, n);
        if (!memchr(result, 0, n)) { free(result); result = NULL; }
        break;
    }
    if (!result) lx_execution_fail(e, "Unmapped, oversized or unterminated guest string");
    lx_host_mutex_unlock(e->memory_mutex); return result;
}

uint32_t lx_execution_tls_window(LxExecution *e)
{
    if (lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return 0;
    if (!e->tls_window && e->services.tls_offset + 128 <= GUEST_PAGE) {
        LxGuestMapping *mapping = map_unlocked(e, 0, GUEST_PAGE, LX_GUEST_READ | LX_GUEST_WRITE, 0, NULL);
        if (mapping && e->ops->identity_memory &&
            !lx_host_vm_protect(mapping->backing, GUEST_PAGE, 0)) {
            unmap_unlocked(e, mapping); mapping = NULL;
        }
        if (mapping) { e->tls_window = mapping; e->tls_address = mapping->address + e->services.tls_offset; }
    }
    uint32_t address = e->tls_address;
    lx_host_mutex_unlock(e->memory_mutex); return address;
}

LxGuestMapping *lx_execution_map_shared(LxExecution *e, LxHostShared *object)
{
    if (!object || lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return NULL;
    LxGuestMapping *m = map_unlocked(e, 0, lx_host_shared_size(object), 3, 0, object);
    lx_host_mutex_unlock(e->memory_mutex); return m;
}

char *lx_execution_copy_strings(LxExecution *e, uint32_t address, size_t maximum, unsigned count)
{
    if (!maximum || lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return NULL;
    char *result = NULL;
    for (LxGuestMapping *m = e->memory; m; m = m->next) {
        if (address < m->address || (uint64_t) address >= (uint64_t) m->address + m->size ||
            !(m->permissions & LX_GUEST_READ)) continue;
        size_t size = readable_extent(e, m, address, maximum); if (!size) break;
        result = malloc(size + 1);
        void *source = memory_unlocked(e, address, size, LX_GUEST_READ);
        if (!result || !source) { free(result); result = NULL; break; }
        e->ops->copy_out(e, source, result, size); result[size] = 0;
        unsigned found = 0; size_t i;
        for (i = 0; i < size; i++) {
            if (!result[i] && (count ? ++found == count : i + 1 < size && !result[i + 1])) break;
        }
        if (i == size) { free(result); result = NULL; }
        else result[i + 1] = 0;
        break;
    }
    if (!result) lx_execution_fail(e, "Unmapped, oversized or unterminated guest string list");
    lx_host_mutex_unlock(e->memory_mutex); return result;
}

int lx_execution_read(LxExecution *e, uint32_t address, void *output, size_t size)
{
    if ((size && !output) || lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return 0;
    void *host = memory_unlocked(e, address, size, LX_GUEST_READ);
    if (host && size) e->ops->copy_out(e, host, output, size);
    lx_host_mutex_unlock(e->memory_mutex); return host != NULL;
}
int lx_execution_write(LxExecution *e, uint32_t address, const void *input, size_t size)
{
    if ((size && !input) || lx_host_mutex_lock(e->memory_mutex, UINT32_MAX) != LX_HOST_LOCK_OK) return 0;
    void *host = memory_unlocked(e, address, size, LX_GUEST_WRITE);
    int ok = host && (!size || e->ops->copy_in(e, address, host, input, size));
    lx_host_mutex_unlock(e->memory_mutex); return ok;
}
int lx_execution_borrow_far(LxExecution *e, uint32_t pointer, size_t size,
                             unsigned permissions, LxGuestBorrow *borrow)
{
    if (!borrow) return 0;
    memset(borrow, 0, sizeof *borrow);
    if (!pointer || size > 0x10000u - (pointer & 0xffffu)) return lx_execution_fail(e, "Far buffer exceeds its segment");
    void *host = e->ops->far_to_host(pointer); uint32_t address;
    return host && lx_execution_address(e, host, size, permissions, &address) &&
           lx_execution_borrow(e, address, size, permissions, borrow);
}
