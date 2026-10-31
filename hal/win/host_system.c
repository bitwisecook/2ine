/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host physical-memory queries; guest policy and size limits are applied above. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "hal/host.h"
#include <string.h>
#include <time.h>
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

uint64_t lx_host_realtime_us(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (ticks - UINT64_C(116444736000000000)) / 10;
}
int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result)
{
    struct tm value;
    __time64_t time = seconds;
    if (utc ? _gmtime64_s(&value, &time) : _localtime64_s(&value, &time))
        return 0;
    result->utc_offset_minutes = utc ? 0 : (int)((_mkgmtime64(&value) - time) / 60);
    result->year = value.tm_year + 1900;
    result->month = value.tm_mon + 1;
    result->day = value.tm_mday;
    result->weekday = value.tm_wday;
    result->hour = value.tm_hour;
    result->minute = value.tm_min;
    result->second = value.tm_sec;
    return 1;
}

const char *lx_host_os(void)
{
    return "windows";
}
