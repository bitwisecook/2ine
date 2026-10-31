/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host physical-memory queries; guest policy and size limits are applied above. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host.h"
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#else
#include <sys/sysinfo.h>
#endif
int lx_host_system_memory(uint64_t *total, uint64_t *available)
{
    if (!total || !available)
        return 0;
#if defined(_WIN32)
    MEMORYSTATUSEX info = {0};
    info.dwLength = sizeof info;
    if (!GlobalMemoryStatusEx(&info))
        return 0;
    *total = info.ullTotalPhys;
    *available = info.ullAvailPhys;
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    size_t size = sizeof bytes;
    if (sysctlbyname("hw.memsize", &bytes, &size, NULL, 0) != 0)
        return 0;
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    kern_return_t rc = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count);
    mach_port_deallocate(mach_task_self(), host);
    if (rc != KERN_SUCCESS)
        return 0;
    *total = bytes;
    *available = ((uint64_t)vm.free_count + vm.inactive_count) * lx_host_page_size();
#else
    struct sysinfo info;
    if (sysinfo(&info) != 0)
        return 0;
    *total = (uint64_t)info.totalram * info.mem_unit;
    *available = (uint64_t)info.freeram * info.mem_unit;
#endif
    return 1;
}

uint64_t lx_host_realtime_us(void)
{
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (ticks - UINT64_C(116444736000000000)) / 10;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
#endif
}
int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result)
{
    struct tm value;
#if defined(_WIN32)
    __time64_t time = seconds;
    if (utc ? _gmtime64_s(&value, &time) : _localtime64_s(&value, &time))
        return 0;
    result->utc_offset_minutes = utc ? 0 : (int)((_mkgmtime64(&value) - time) / 60);
#else
    time_t time = (time_t)seconds;
    if ((int64_t)time != seconds || !(utc ? gmtime_r(&time, &value) : localtime_r(&time, &value)))
        return 0;
    result->utc_offset_minutes = utc ? 0 : (int)(value.tm_gmtoff / 60);
#endif
    result->year = value.tm_year + 1900;
    result->month = value.tm_mon + 1;
    result->day = value.tm_mday;
    result->weekday = value.tm_wday;
    result->hour = value.tm_hour;
    result->minute = value.tm_min;
    result->second = value.tm_sec;
    return 1;
}
