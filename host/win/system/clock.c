/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host monotonic and wall clocks, sleeping and calendar conversion. */
#include "host/clock.h"
#include <time.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* FILETIME counts 100-nanosecond ticks from 1601-01-01 UTC. */
#define FILETIME_UNIX_EPOCH UINT64_C(116444736000000000)
#define FILETIME_TICKS_PER_MICROSECOND 10
#define MICROSECONDS_PER_SECOND 1000000u
#define SECONDS_PER_MINUTE 60
/* struct tm counts years from 1900 and months from zero. */
#define TM_YEAR_BASE 1900

uint64_t lx_host_monotonic_us(void)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(now.QuadPart / frequency.QuadPart) * MICROSECONDS_PER_SECOND +
           (uint64_t)(now.QuadPart % frequency.QuadPart) * MICROSECONDS_PER_SECOND /
               (uint64_t)frequency.QuadPart;
}

void lx_host_sleep_ms(uint32_t milliseconds)
{
    Sleep(milliseconds);
}

uint64_t lx_host_realtime_us(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (ticks - FILETIME_UNIX_EPOCH) / FILETIME_TICKS_PER_MICROSECOND;
}

int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result)
{
    struct tm value;
    __time64_t time = seconds;
    if (utc ? _gmtime64_s(&value, &time) : _localtime64_s(&value, &time))
        return 0;
    result->utc_offset_minutes = utc ? 0 : (int)((_mkgmtime64(&value) - time) / SECONDS_PER_MINUTE);
    result->year = value.tm_year + TM_YEAR_BASE;
    result->month = value.tm_mon + 1;
    result->day = value.tm_mday;
    result->weekday = value.tm_wday;
    result->hour = value.tm_hour;
    result->minute = value.tm_min;
    result->second = value.tm_sec;
    return 1;
}
