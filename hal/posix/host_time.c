/* Public domain. No rights reserved. Wholly AI-authored. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "hal/host.h"
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
