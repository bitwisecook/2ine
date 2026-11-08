/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host/system.h"
#include <stdio.h>
#include <time.h>

uint64_t lx_host_realtime_us(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
}

int lx_host_calendar(int64_t seconds, int utc, LxHostCalendar *result)
{
    struct tm value;
    time_t time = (time_t)seconds;
    if ((int64_t)time != seconds || !(utc ? gmtime_r(&time, &value) : localtime_r(&time, &value)))
        return 0;
    result->utc_offset_minutes = utc ? 0 : (int)(value.tm_gmtoff / 60);
    result->year = value.tm_year + 1900;
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
    value.tm_year = calendar->year - 1900;
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
