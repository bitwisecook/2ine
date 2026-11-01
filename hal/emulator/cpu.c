/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest-address ownership and execution-backend dispatch. */
#include "cpu_internal.h"
#include "hal/host.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct LxCpuContext
{
    LxCpu *owner;
    const LxCpuOperations *ops;
    void *data;
    LxCpuContext *next;
};

int lx_cpu_fail(LxCpu *cpu, const char *message)
{
    snprintf(cpu->error, sizeof cpu->error, "%s", message);
    return 0;
}

LxCpu *lx_cpu_create(const char *backend, char *error, size_t error_size)
{
    LxCpu *cpu = calloc(1, sizeof *cpu);
    if (!cpu)
    {
        if (error && error_size)
            snprintf(error, error_size, "Out of memory");
        return NULL;
    }
    if (!backend || !strcmp(backend, "auto") || !strcmp(backend, "unicorn"))
    {
        cpu->ops = &lx_cpu_unicorn_operations;
    }
    else
    {
        snprintf(cpu->error, sizeof cpu->error, "CPU backend '%s' is not linked", backend);
    }
    if (!cpu->ops || !cpu->ops->open(cpu))
    {
        if (error && error_size)
            snprintf(error, error_size, "%s", cpu->error);
        free(cpu);
        return NULL;
    }
    if (error && error_size)
        error[0] = '\0';
    return cpu;
}

void lx_cpu_destroy(LxCpu *cpu)
{
    if (!cpu)
        return;
    while (cpu->contexts)
    {
        LxCpuContext *c = cpu->contexts;
        cpu->contexts = c->next;
        c->owner = NULL;
        c->next = NULL;
    }
    while (cpu->segments)
    {
        LxCpuSegment *s = cpu->segments;
        cpu->segments = s->next;
        free(s);
    }
    cpu->ops->close(cpu);
    while (cpu->memory)
    {
        LxMemoryRegion *r = cpu->memory;
        cpu->memory = r->next;
        if (r->owns_host)
            lx_host_memory_free(r->host, r->size);
        free(r);
    }
    free(cpu);
}

const char *lx_cpu_backend(const LxCpu *cpu)
{
    return cpu->ops->name;
}
const char *lx_cpu_error(const LxCpu *cpu)
{
    return cpu->error;
}

static int aligned_range(LxCpu *cpu, uint32_t address, size_t size)
{
    if (!size || address % LX_GUEST_PAGE_SIZE || size % LX_GUEST_PAGE_SIZE ||
        size > LX_DESCRIPTOR_BASE || (uint64_t)address + size > LX_DESCRIPTOR_BASE)
        return lx_cpu_fail(cpu, "Invalid, overflowing or reserved guest memory range");
    return 1;
}

static int map_memory(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions,
                      void *backing, int owns_host)
{
    if (!aligned_range(cpu, address, size) || !permissions || (permissions & ~7u))
        return lx_cpu_fail(cpu, "Invalid guest mapping");
    for (LxMemoryRegion *r = cpu->memory; r; r = r->next)
    {
        if ((uint64_t)address < (uint64_t)r->address + r->size &&
            (uint64_t)r->address < (uint64_t)address + size)
            return lx_cpu_fail(cpu, "Overlapping guest mappings");
    }
    LxMemoryRegion *r = calloc(1, sizeof *r);
    if (!r)
        return lx_cpu_fail(cpu, "Out of memory");
    r->host = backing;
    r->owns_host = owns_host;
    if (!r->host)
    {
        free(r);
        return lx_cpu_fail(cpu, "Host memory allocation failed");
    }
    if (!cpu->ops->map(cpu, address, size, permissions, r->host))
    {
        free(r);
        return 0;
    }
    r->address = address;
    r->size = size;
    r->permissions = permissions;
    r->next = cpu->memory;
    cpu->memory = r;
    return 1;
}

int lx_cpu_map_borrowed(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions,
                        void *backing)
{
    if (!backing || (uintptr_t)backing % LX_GUEST_PAGE_SIZE)
        return lx_cpu_fail(cpu, "Borrowed memory must be page aligned");
    return map_memory(cpu, address, size, permissions, backing, 0);
}
int lx_cpu_map(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions)
{
    if (!aligned_range(cpu, address, size))
        return 0;
    void *backing = lx_host_memory_alloc(size);
    if (!backing)
        return lx_cpu_fail(cpu, "Host memory allocation failed");
    if (map_memory(cpu, address, size, permissions, backing, 1))
        return 1;
    lx_host_memory_free(backing, size);
    return 0;
}

static LxMemoryRegion **whole_region(LxCpu *cpu, uint32_t address, size_t size)
{
    for (LxMemoryRegion **r = &cpu->memory; *r; r = &(*r)->next)
        if ((*r)->address == address && (*r)->size == size)
            return r;
    lx_cpu_fail(cpu, "Operation requires one complete guest mapping");
    return NULL;
}

int lx_cpu_unmap(LxCpu *cpu, uint32_t address, size_t size)
{
    LxMemoryRegion **link = whole_region(cpu, address, size);
    if (!link || !cpu->ops->unmap(cpu, address, size))
        return 0;
    LxMemoryRegion *r = *link;
    *link = r->next;
    if (r->owns_host)
        lx_host_memory_free(r->host, size);
    free(r);
    return 1;
}

int lx_cpu_protect(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions)
{
    LxMemoryRegion **link = whole_region(cpu, address, size);
    if (!link || (permissions & ~7u))
        return lx_cpu_fail(cpu, "Invalid guest protection range");
    if (!cpu->ops->protect(cpu, address, size, permissions))
        return 0;
    (*link)->permissions = permissions;
    return 1;
}

void *lx_cpu_translate(LxCpu *cpu, uint32_t address, size_t size, unsigned permissions)
{
    if ((uint64_t)size > UINT64_C(0x100000000) - address || (permissions & ~7u))
    {
        lx_cpu_fail(cpu, "Guest pointer range overflows");
        return NULL;
    }
    for (LxMemoryRegion *r = cpu->memory; r; r = r->next)
    {
        if (address >= r->address && (uint64_t)address + size <= (uint64_t)r->address + r->size &&
            (r->permissions & permissions) == permissions)
            return (unsigned char *)r->host + (address - r->address);
    }
    lx_cpu_fail(cpu, "Unmapped or inaccessible guest pointer");
    return NULL;
}

int lx_cpu_read(LxCpu *cpu, uint32_t address, void *out, size_t size)
{
    if (size && !out)
        return lx_cpu_fail(cpu, "Missing host output buffer");
    void *p = lx_cpu_translate(cpu, address, size, LX_MEM_READ);
    if (!p)
        return 0;
    if (size)
        memcpy(out, p, size);
    return 1;
}

int lx_cpu_write(LxCpu *cpu, uint32_t address, const void *in, size_t size)
{
    if (size && !in)
        return lx_cpu_fail(cpu, "Missing host input buffer");
    void *p = lx_cpu_translate(cpu, address, size, LX_MEM_WRITE);
    if (!p)
        return 0;
    if (size)
        memcpy(p, in, size);
    return cpu->ops->invalidate(cpu, address, size);
}

int lx_cpu_set_segment(LxCpu *cpu, const LxSegment *s)
{
    if (!s || !(s->selector & ~3u) || (s->bits != 16 && s->bits != 32) ||
        ((uint64_t)s->base + s->limit >= UINT64_C(0x100000000)))
        return lx_cpu_fail(cpu, "Invalid x86 segment descriptor");
    LxCpuSegment *entry;
    for (entry = cpu->segments; entry; entry = entry->next)
        if ((entry->definition.selector & ~3u) == (s->selector & ~3u))
            break;
    if (entry)
    {
        if (!cpu->ops->segment(cpu, s))
            return 0;
        entry->definition = *s;
        return 1;
    }
    entry = malloc(sizeof *entry);
    if (!entry)
        return lx_cpu_fail(cpu, "Out of memory");
    if (!cpu->ops->segment(cpu, s))
    {
        free(entry);
        return 0;
    }
    entry->definition = *s;
    entry->next = cpu->segments;
    cpu->segments = entry;
    return 1;
}

void *lx_cpu_translate_far(LxCpu *cpu, uint16_t selector, uint32_t offset, size_t size,
                           unsigned permissions)
{
    for (LxCpuSegment *entry = cpu->segments; entry; entry = entry->next)
    {
        const LxSegment *s = &entry->definition;
        if ((s->selector & ~3u) != (selector & ~3u))
            continue;
        if (offset > s->limit || size > (uint64_t)s->limit + 1 - offset)
        {
            lx_cpu_fail(cpu, "Far pointer exceeds its segment limit");
            return NULL;
        }
        return lx_cpu_translate(cpu, s->base + offset, size, permissions);
    }
    lx_cpu_fail(cpu, "Far pointer has an unknown selector");
    return NULL;
}

int lx_cpu_get_registers(LxCpu *cpu, LxCpuRegisters *r)
{
    return cpu->ops->get_registers(cpu, r);
}
int lx_cpu_set_registers(LxCpu *cpu, const LxCpuRegisters *r)
{
    return cpu->ops->set_registers(cpu, r);
}
LxCpuEvent lx_cpu_run(LxCpu *cpu, uint64_t count, uint64_t us)
{
    return cpu->ops->run(cpu, count, us);
}
LxCpuEvent lx_cpu_run_blocks(LxCpu *cpu, uint64_t blocks)
{
    return cpu->ops->run_blocks(cpu, blocks);
}

LxCpuContext *lx_cpu_save(LxCpu *cpu)
{
    LxCpuContext *c = calloc(1, sizeof *c);
    if (!c)
    {
        lx_cpu_fail(cpu, "Out of memory");
        return NULL;
    }
    c->data = cpu->ops->save(cpu);
    if (!c->data)
    {
        free(c);
        return NULL;
    }
    c->owner = cpu;
    c->ops = cpu->ops;
    c->next = cpu->contexts;
    cpu->contexts = c;
    return c;
}

int lx_cpu_restore(LxCpu *cpu, const LxCpuContext *context)
{
    if (!context || context->owner != cpu)
        return lx_cpu_fail(cpu, "CPU context belongs to a different CPU");
    return cpu->ops->restore(cpu, context->data);
}
int lx_cpu_update(LxCpu *cpu, LxCpuContext *context)
{
    if (!context || context->owner != cpu)
        return lx_cpu_fail(cpu, "CPU context belongs to a different CPU");
    return cpu->ops->update_context(cpu, context->data);
}

void lx_cpu_context_free(LxCpuContext *context)
{
    if (context)
    {
        if (context->owner)
        {
            LxCpuContext **link = &context->owner->contexts;
            while (*link != context)
                link = &(*link)->next;
            *link = context->next;
        }
        context->ops->free_context(context->data);
        free(context);
    }
}
