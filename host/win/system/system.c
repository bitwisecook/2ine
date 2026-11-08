/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host physical-memory and OS queries; guest policy and size limits are applied above. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host/memory.h"
#include "host/system.h"
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int lx_host_system_memory(uint64_t *total, uint64_t *available)
{
    if (!total || !available)
        return 0;
    MEMORYSTATUSEX info = {0};
    info.dwLength = sizeof info;
    if (!GlobalMemoryStatusEx(&info))
        return 0;
    *total = info.ullTotalPhys;
    *available = info.ullAvailPhys;
    return 1;
}

const char *lx_host_os(void)
{
    return "windows";
}
