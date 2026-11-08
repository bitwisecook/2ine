/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host shared-memory objects. Names are caller-provided ASCII identifiers. */
#ifndef TWOINE_HOST_SHARED_MEMORY_H
#define TWOINE_HOST_SHARED_MEMORY_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostShared LxHostShared;

typedef enum LxHostSharedResult
{
    LX_HOST_SHARED_OK,
    LX_HOST_SHARED_MISSING,
    LX_HOST_SHARED_EXISTS,
    LX_HOST_SHARED_DENIED,
    LX_HOST_SHARED_NOMEM
} LxHostSharedResult;

LxHostSharedResult lx_host_shared_open(const char *name, size_t create_size, LxHostShared **output);
/* Reserve a process-independent numeric slot while an object has live references. */
int lx_host_shared_slot(LxHostShared *object, uint64_t first, uint64_t limit, uint64_t alignment,
                        uint64_t *slot);
/* Find an existing live object by its exact numeric slot; never create backing. */
LxHostSharedResult lx_host_shared_find(uint64_t slot, LxHostShared **output, char *name,
                                       size_t capacity);
/* Caller-defined attributes and peer grants are bound to this backing identity. */
int lx_host_shared_publish(LxHostShared *object, uint32_t owner, uint32_t attributes);
int lx_host_shared_query_access(LxHostShared *object, uint32_t peer, uint32_t *owner,
                                uint32_t *attributes, uint32_t *permissions);
int lx_host_shared_grant(LxHostShared *object, uint32_t peer, uint32_t permissions);
/* Lifetime snapshot without creating backing or retaining a reference: 1 live, 0 idle, -1 error. */
int lx_host_shared_live(const char *name);
size_t lx_host_shared_size(const LxHostShared *object);
/* Exact views never replace an existing mapping. The caller owns the view;
   release it with lx_host_shared_unmap independently of the object's lifetime. */
void *lx_host_shared_map(LxHostShared *object, void *hint, int exact);
void lx_host_shared_unmap(void *view, size_t size);
void lx_host_shared_close(LxHostShared *object);
#endif
