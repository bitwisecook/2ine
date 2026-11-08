/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host virtual memory and physical-memory queries. */
#ifndef TWOINE_HOST_MEMORY_H
#define TWOINE_HOST_MEMORY_H
#include <stddef.h>
#include <stdint.h>

/* Guest executable permission does not require executable host backing memory. */
void *lx_host_memory_alloc(size_t size);
void lx_host_memory_free(void *memory, size_t size);
int lx_host_system_memory(uint64_t *total, uint64_t *available);

enum
{
    LX_HOST_READ = 1,
    LX_HOST_WRITE = 2,
    LX_HOST_EXECUTE = 4
};

/* Exact mappings fail on occupied addresses; existing mappings are never replaced.
   Host addresses and host protection are independent of guest virtual addresses. */
void *lx_host_vm_map(void *hint, size_t size, unsigned permissions, int exact);
int lx_host_vm_protect(void *memory, size_t size, unsigned permissions);
size_t lx_host_page_size(void);
#endif
