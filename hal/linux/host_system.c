/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host.h"
#include <sys/sysinfo.h>
int lx_host_system_memory(uint64_t *total, uint64_t *available)
{
    if (!total || !available)
        return 0;
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        return 0;
    *total = (uint64_t)info.totalram * info.mem_unit;
    *available = (uint64_t)info.freeram * info.mem_unit;
    return 1;
}

const char *lx_host_os(void)
{
    return "linux";
}
