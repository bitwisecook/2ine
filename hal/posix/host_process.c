/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#include "host.h"
#include <unistd.h>
#include <sys/resource.h>
void lx_host_process_ids(uint32_t *process, uint32_t *parent)
{
    *process = (uint32_t)getpid();
    *parent = (uint32_t)getppid();
}

size_t lx_host_stack_limit(void)
{
    struct rlimit limit;
    if (!getrlimit(RLIMIT_STACK, &limit) && limit.rlim_cur != RLIM_INFINITY &&
        limit.rlim_cur <= SIZE_MAX)
        return (size_t)limit.rlim_cur;
    return 8 * 1024 * 1024;
}

void lx_host_process_exit(uint32_t status)
{
    _exit((int)(status & 255));
}
