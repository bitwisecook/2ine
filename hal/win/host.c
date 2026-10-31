/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host memory and clock services; no guest CPU state lives in this file. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "hal/host.h"
#include <errno.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static DWORD windows_protection(unsigned p)
{
    if (p & LX_HOST_EXECUTE)
        return p & LX_HOST_WRITE  ? PAGE_EXECUTE_READWRITE
               : p & LX_HOST_READ ? PAGE_EXECUTE_READ
                                  : PAGE_EXECUTE;
    return p & LX_HOST_WRITE ? PAGE_READWRITE : p & LX_HOST_READ ? PAGE_READONLY : PAGE_NOACCESS;
}
void *lx_host_vm_map(void *hint, size_t size, unsigned permissions, int exact)
{
    if (!size || (permissions & ~7u) || (exact && !hint))
    {
        errno = EINVAL;
        return NULL;
    }
    void *p = VirtualAlloc(hint, size, MEM_RESERVE | MEM_COMMIT, windows_protection(permissions));
    if (p && exact && p != hint)
    {
        VirtualFree(p, 0, MEM_RELEASE);
        p = NULL;
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
    DWORD previous;
    return VirtualProtect(memory, size, windows_protection(permissions), &previous) != 0;
}
size_t lx_host_page_size(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}
void *lx_host_memory_alloc(size_t size)
{
    return lx_host_vm_map(NULL, size, LX_HOST_READ | LX_HOST_WRITE, 0);
}

void lx_host_memory_free(void *memory, size_t size)
{
    (void)size;
    VirtualFree(memory, 0, MEM_RELEASE);
}

uint64_t lx_host_monotonic_us(void)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(now.QuadPart / frequency.QuadPart) * 1000000u +
           (uint64_t)(now.QuadPart % frequency.QuadPart) * 1000000u / (uint64_t)frequency.QuadPart;
}

void lx_host_sleep_ms(uint32_t milliseconds)
{
    Sleep(milliseconds);
}
