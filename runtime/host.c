/* Host memory and clock services; no guest CPU state lives in this file. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "host.h"
#include <errno.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <time.h>
#include <sched.h>
#endif
#if defined(_WIN32)
static DWORD windows_protection(unsigned p)
{
    if (p & LX_HOST_EXECUTE)
        return p & LX_HOST_WRITE ? PAGE_EXECUTE_READWRITE :
               p & LX_HOST_READ ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    return p & LX_HOST_WRITE ? PAGE_READWRITE : p & LX_HOST_READ ? PAGE_READONLY : PAGE_NOACCESS;
}
#else
#include <unistd.h>
static int posix_protection(unsigned p)
{
    return ((p & LX_HOST_READ) ? PROT_READ : 0) |
           ((p & LX_HOST_WRITE) ? PROT_WRITE : 0) |
           ((p & LX_HOST_EXECUTE) ? PROT_EXEC : 0);
}
#endif
void *lx_host_vm_map(void *hint, size_t size, unsigned permissions, int exact)
{
    if (!size || (permissions & ~7u) || (exact && !hint)) { errno = EINVAL; return NULL; }
#if defined(_WIN32)
    void *p = VirtualAlloc(hint, size, MEM_RESERVE | MEM_COMMIT, windows_protection(permissions));
    if (p && exact && p != hint) { VirtualFree(p, 0, MEM_RELEASE); p = NULL; }
    return p;
#else
    /* A hint is deliberately used without MAP_FIXED, including on kernels
       lacking MAP_FIXED_NOREPLACE. Verify the returned address before use. */
    void *p = mmap(hint, size, posix_protection(permissions),
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (exact && p != hint) { munmap(p, size); errno = EEXIST; return NULL; }
    return p;
#endif
}
int lx_host_vm_protect(void *memory, size_t size, unsigned permissions)
{
    if (!memory || !size || (permissions & ~7u)) { errno = EINVAL; return 0; }
#if defined(_WIN32)
    DWORD previous;
    return VirtualProtect(memory, size, windows_protection(permissions), &previous) != 0;
#else
    return mprotect(memory, size, posix_protection(permissions)) == 0;
#endif
}
size_t lx_host_page_size(void)
{
#if defined(_WIN32)
    SYSTEM_INFO info; GetSystemInfo(&info); return info.dwPageSize;
#else
    return (size_t) sysconf(_SC_PAGESIZE);
#endif
}
void *lx_host_memory_alloc(size_t size)
{
    return lx_host_vm_map(NULL, size, LX_HOST_READ | LX_HOST_WRITE, 0);
}

void lx_host_memory_free(void *memory, size_t size)
{
#if defined(_WIN32)
    (void) size;
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, size);
#endif
}

uint64_t lx_host_monotonic_us(void)
{
#if defined(_WIN32)
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t) (now.QuadPart / frequency.QuadPart) * 1000000u +
           (uint64_t) (now.QuadPart % frequency.QuadPart) * 1000000u /
           (uint64_t) frequency.QuadPart;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t) now.tv_sec * 1000000u + (uint64_t) now.tv_nsec / 1000u;
#endif
}

const char *lx_host_os(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "posix";
#endif
}

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


void lx_host_sleep_ms(uint32_t milliseconds)
{
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    if (!milliseconds) { sched_yield(); return; }
    struct timespec remaining = {milliseconds / 1000, (long) (milliseconds % 1000) * 1000000};
    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
#endif
}
