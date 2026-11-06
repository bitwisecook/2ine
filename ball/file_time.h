/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_FILE_TIME_H
#define TB_FILE_TIME_H
#include <stdint.h>
#include <time.h>
void tb_file_time_cache_reset(void);
#ifdef TB_MEDIA_TIME_CACHE
int64_t tb_local_file_time(unsigned date, unsigned clock, struct tm *parts);
#else
/* Mounted writable volumes use the current local zone on every call. Media
   requests opt into a request-scoped snapshot provided by platform.c. */
static inline int64_t tb_local_file_time(unsigned date, unsigned clock, struct tm *parts)
{
    (void)date;
    (void)clock;
    return (int64_t)mktime(parts);
}
#endif
static inline int tb_calendar_day(int year, int month, int day)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 0 || month > 11 || day < 1)
        return 0;
    int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day <= days[month] + (month == 1 && leap);
}
/* A missing timestamp is distinct from the Unix epoch. DOS dates have no timezone. */
static inline int64_t tb_dos_time(unsigned date, unsigned clock)
{
    struct tm parts = {0};
    parts.tm_year = (int)(date >> 9) + 80;
    parts.tm_mon = (int)((date >> 5) & 15) - 1;
    parts.tm_mday = (int)(date & 31);
    parts.tm_hour = (int)(clock >> 11);
    parts.tm_min = (int)((clock >> 5) & 63);
    parts.tm_sec = (int)(clock & 31) * 2;
    parts.tm_isdst = -1;
    if (!tb_calendar_day(parts.tm_year + 1900, parts.tm_mon, parts.tm_mday) || parts.tm_hour > 23 ||
        parts.tm_min > 59 || parts.tm_sec > 59)
        return INT64_MIN;
    return tb_local_file_time(date, clock, &parts);
}
/* Date in the high word and clock in the low word, clamped to the DOS date range. */
static inline uint32_t tb_dos_stamp(int64_t seconds)
{
    time_t value = (time_t)seconds;
    struct tm date;
#ifdef _WIN32
    localtime_s(&date, &value);
#else
    localtime_r(&value, &date);
#endif
    unsigned year = date.tm_year + 1900 < 1980   ? 1980
                    : date.tm_year + 1900 > 2107 ? 2107
                                                 : (unsigned)date.tm_year + 1900;
    return (uint32_t)(year - 1980) << 25 | (uint32_t)(date.tm_mon + 1) << 21 |
           (uint32_t)date.tm_mday << 16 | (uint32_t)date.tm_hour << 11 |
           (uint32_t)date.tm_min << 5 | (uint32_t)date.tm_sec / 2;
}
static inline int64_t tb_iso_time(const unsigned char *date)
{
    struct tm parts = {0};
    parts.tm_year = date[0];
    parts.tm_mon = (int)date[1] - 1;
    parts.tm_mday = date[2];
    parts.tm_hour = date[3];
    parts.tm_min = date[4];
    parts.tm_sec = date[5];
    int zone = (int)(int8_t)date[6];
    if (!tb_calendar_day(parts.tm_year + 1900, parts.tm_mon, parts.tm_mday) || parts.tm_hour > 23 ||
        parts.tm_min > 59 || parts.tm_sec > 59 || zone < -48 || zone > 52)
        return INT64_MIN;
    int year = parts.tm_year + 1900;
    static const int days_before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    int64_t days = (int64_t)(year - 1970) * 365 + (year - 1) / 4 - 1969 / 4 - (year - 1) / 100 +
                   1969 / 100 + (year - 1) / 400 - 1969 / 400;
    days += days_before[parts.tm_mon] + (leap && parts.tm_mon > 1) + parts.tm_mday - 1;
    return days * 86400 + parts.tm_hour * 3600 + parts.tm_min * 60 + parts.tm_sec - zone * 900;
}
#endif
