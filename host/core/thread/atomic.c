/* Public domain. No rights reserved. Wholly AI-authored. */
#include "host/sync.h"

uint32_t lx_host_atomic_increment(uint32_t *value)
{
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}
