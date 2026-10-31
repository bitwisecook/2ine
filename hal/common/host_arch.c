/* Public domain. No rights reserved. Wholly AI-authored. */
#include "hal/host.h"
const char *lx_host_arch(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "other";
#endif
}

uint32_t lx_host_atomic_increment(uint32_t *value)
{
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}
