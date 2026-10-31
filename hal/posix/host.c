/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host memory and clock services; no guest CPU state lives in this file. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "host.h"
#include <errno.h>
#include <sys/mman.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
static int posix_protection(unsigned p)
{
    return ((p & LX_HOST_READ) ? PROT_READ : 0) | ((p & LX_HOST_WRITE) ? PROT_WRITE : 0) |
           ((p & LX_HOST_EXECUTE) ? PROT_EXEC : 0);
}
void *lx_host_vm_map(void *hint, size_t size, unsigned permissions, int exact)
{
    if (!size || (permissions & ~7u) || (exact && !hint))
    {
        errno = EINVAL;
        return NULL;
    }
    /* A hint is deliberately used without MAP_FIXED, including on kernels
       lacking MAP_FIXED_NOREPLACE. Verify the returned address before use. */
    void *p = mmap(hint, size, posix_protection(permissions), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    if (exact && p != hint)
    {
        munmap(p, size);
        errno = EEXIST;
        return NULL;
    }
    return p;
}
int lx_host_vm_protect(void *memory, size_t size, unsigned permissions)
{
    if (!memory || !size || (permissions & ~7u))
    {
        errno = EINVAL;
        return 0;
    }
    return mprotect(memory, size, posix_protection(permissions)) == 0;
}
size_t lx_host_page_size(void)
{
    return (size_t)sysconf(_SC_PAGESIZE);
}
void *lx_host_memory_alloc(size_t size)
{
    return lx_host_vm_map(NULL, size, LX_HOST_READ | LX_HOST_WRITE, 0);
}

void lx_host_memory_free(void *memory, size_t size)
{
    munmap(memory, size);
}

uint64_t lx_host_monotonic_us(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

void lx_host_sleep_ms(uint32_t milliseconds)
{
    if (!milliseconds)
    {
        sched_yield();
        return;
    }
    struct timespec remaining = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&remaining, &remaining) && errno == EINTR)
    {
    }
}
