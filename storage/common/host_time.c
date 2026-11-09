/* Public domain. No rights reserved. Wholly AI-authored. */
/* Local time for FAT and archive timestamps. */
#define _POSIX_C_SOURCE 200809L
#include "storage/common/host.h"
#include "storage/common/file_time.h"
#include <stdlib.h>
#include <string.h>

struct tm *tb_host_localtime(int64_t seconds, struct tm *result)
{
    time_t value = (time_t)seconds;
#ifdef _WIN32
    return localtime_s(result, &value) ? NULL : result;
#else
    return localtime_r(&value, result);
#endif
}

#ifdef TB_MEDIA_TIME_CACHE
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
static _Thread_local CFCalendarRef file_time_calendar;
#endif
/* mktime on macOS may reload zoneinfo for every DOS archive timestamp. A
   request uses one local-zone interpretation; no process-wide TZ is changed. */
/* Fibonacci hashing: 2^64 divided by the golden ratio. */
#define FIBONACCI_HASH_MULTIPLIER UINT64_C(11400714819323198485)
#define FILE_TIME_CACHE_BITS 12

static _Thread_local struct
{
    uint64_t key;
    int64_t value;
    int valid;
} file_time_cache[1u << FILE_TIME_CACHE_BITS];

void tb_file_time_cache_reset(void)
{
    memset(file_time_cache, 0, sizeof file_time_cache);
#ifdef __APPLE__
    if (file_time_calendar)
        CFRelease(file_time_calendar);
    file_time_calendar = NULL;
#endif
}

int64_t tb_local_file_time(unsigned date, unsigned clock, struct tm *parts)
{
    uint64_t key = ((uint64_t)date << 32) | clock;
    size_t slot = (size_t)((key ^ (key >> 17)) * FIBONACCI_HASH_MULTIPLIER) >>
                  (sizeof(size_t) * 8 - FILE_TIME_CACHE_BITS);
    if (!file_time_cache[slot].valid || file_time_cache[slot].key != key)
    {
        file_time_cache[slot].key = key;
#ifdef __APPLE__
        if (!file_time_calendar)
        {
            const char *environment = getenv("TZ");
            CFTimeZoneRef zone = NULL;
            if (environment && *environment)
            {
                CFStringRef name =
                    CFStringCreateWithCString(NULL, environment, kCFStringEncodingUTF8);
                if (name)
                {
                    zone = CFTimeZoneCreateWithName(NULL, name, true);
                    CFRelease(name);
                }
                if (!zone && (!strcmp(environment, "UTC0") || !strcmp(environment, "GMT0")))
                    zone = CFTimeZoneCreateWithTimeIntervalFromGMT(NULL, 0);
            }
            else
                zone = CFTimeZoneCopySystem();
            if (zone)
            {
                file_time_calendar = CFCalendarCreateWithIdentifier(NULL, kCFGregorianCalendar);
                if (file_time_calendar)
                    CFCalendarSetTimeZone(file_time_calendar, zone);
                CFRelease(zone);
            }
        }
        CFAbsoluteTime absolute;
        if (file_time_calendar &&
            CFCalendarComposeAbsoluteTime(file_time_calendar, &absolute, "yMdHms",
                                          parts->tm_year + 1900, parts->tm_mon + 1, parts->tm_mday,
                                          parts->tm_hour, parts->tm_min, parts->tm_sec))
            file_time_cache[slot].value = (int64_t)(absolute + kCFAbsoluteTimeIntervalSince1970);
        else
#endif
            file_time_cache[slot].value = (int64_t)mktime(parts);
        file_time_cache[slot].valid = 1;
    }
    return file_time_cache[slot].value;
}
#else
void tb_file_time_cache_reset(void) {}
#endif
