/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host/clock.h"
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#define MILLISECONDS_PER_SECOND 1000
#define MICROSECONDS_PER_SECOND 1000000u
#define NANOSECONDS_PER_MICROSECOND 1000u
#define NANOSECONDS_PER_MILLISECOND 1000000L
#define SECONDS_PER_MINUTE 60
/* struct tm counts years from 1900 and months from zero. */
#define TM_YEAR_BASE 1900

uint64_t lx_host_monotonic_us(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * MICROSECONDS_PER_SECOND +
           (uint64_t)now.tv_nsec / NANOSECONDS_PER_MICROSECOND;
}

void lx_host_sleep_ms(uint32_t milliseconds)
{
    if (!milliseconds)
    {
        sched_yield();
        return;
    }
    struct timespec remaining = {milliseconds / MILLISECONDS_PER_SECOND,
                                 (long)(milliseconds % MILLISECONDS_PER_SECOND) *
                                     NANOSECONDS_PER_MILLISECOND};
    while (nanosleep(&remaining, &remaining) && errno == EINTR)
    {
    }
}

uint64_t lx_host_realtime_us(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now))
        return 0;
    return (uint64_t)now.tv_sec * MICROSECONDS_PER_SECOND +
           (uint64_t)now.tv_nsec / NANOSECONDS_PER_MICROSECOND;
}

int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result)
{
    struct tm value;
    time_t time = (time_t)seconds;
    if ((int64_t)time != seconds || !(utc ? gmtime_r(&time, &value) : localtime_r(&time, &value)))
        return 0;
    result->utc_offset_minutes = utc ? 0 : (int)(value.tm_gmtoff / SECONDS_PER_MINUTE);
    result->year = value.tm_year + TM_YEAR_BASE;
    result->month = value.tm_mon + 1;
    result->day = value.tm_mday;
    result->weekday = value.tm_wday;
    result->hour = value.tm_hour;
    result->minute = value.tm_min;
    result->second = value.tm_sec;
    return 1;
}

int lx_host_calendar_seconds(const LxHostCalendar *calendar, int utc, int64_t *seconds)
{
    if (!calendar || !seconds)
        return 0;
    struct tm value = {0};
    value.tm_year = calendar->year - TM_YEAR_BASE;
    value.tm_mon = calendar->month - 1;
    value.tm_mday = calendar->day;
    value.tm_hour = calendar->hour;
    value.tm_min = calendar->minute;
    value.tm_sec = calendar->second;
    value.tm_isdst = -1;
    time_t result = utc ? timegm(&value) : mktime(&value);
    LxHostCalendar check;
    if (!lx_host_calendar((int64_t)result, utc, &check) || check.year != calendar->year ||
        check.month != calendar->month || check.day != calendar->day ||
        check.hour != calendar->hour || check.minute != calendar->minute ||
        check.second != calendar->second)
        return 0;
    *seconds = (int64_t)result;
    return 1;
}

void lx_host_zone_names(char standard[32], char daylight[32])
{
    tzset();
    snprintf(standard, 32, "%s", tzname[0]);
    snprintf(daylight, 32, "%s", tzname[1]);
}

int lx_host_wall_time(int64_t *seconds, int32_t *micros, int32_t *west, int32_t *dst)
{
    struct timeval t;
    struct timezone zone;
    int r = gettimeofday(&t, &zone);
    *seconds = t.tv_sec;
    *micros = (int32_t)t.tv_usec;
    *west = zone.tz_minuteswest;
    *dst = zone.tz_dsttime;
    return r;
}
