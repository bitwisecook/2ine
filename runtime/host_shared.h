/* Host shared-memory objects. Names are caller-provided ASCII identifiers. */
#ifndef TWOINE_HOST_SHARED_H
#define TWOINE_HOST_SHARED_H
#include <stddef.h>
typedef struct LxHostShared LxHostShared;
typedef enum LxHostSharedResult {
    LX_HOST_SHARED_OK, LX_HOST_SHARED_MISSING, LX_HOST_SHARED_EXISTS,
    LX_HOST_SHARED_DENIED, LX_HOST_SHARED_NOMEM
} LxHostSharedResult;
LxHostSharedResult lx_host_shared_open(const char *name, size_t create_size, LxHostShared **output);
size_t lx_host_shared_size(const LxHostShared *object);
/* Exact views never replace an existing mapping. The caller owns the view;
   release it with lx_host_shared_unmap independently of the object's lifetime. */
void *lx_host_shared_map(LxHostShared *object, void *hint, int exact);
void lx_host_shared_unmap(void *view, size_t size);
void lx_host_shared_close(LxHostShared *object);
#endif
