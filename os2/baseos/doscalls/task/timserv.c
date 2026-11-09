/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Date and time; settings offset the guest clock without changing the host. */
#include "os2/baseos/doscalls/internal.h"

/* The DATETIME range DosSetDateTime accepts; timezone is minutes west of UTC. */
enum
{
    DATETIME_YEAR_MINIMUM = 1980,
    DATETIME_YEAR_MAXIMUM = 2079,
    DATETIME_HUNDREDTHS_MAXIMUM = 99,
    DATETIME_TIMEZONE_LIMIT = 13 * 60,
    /* DATETIME has no leap second. */
    DATETIME_SECONDS_MAXIMUM = 59
};

/* Clock adjustments belong to the emulated process and never change host time. */
static int64_t guest_clock_offset;

APIRET16 Dos16SetDateTime(PDATETIME datetime)
{
    if (!datetime || datetime->year < DATETIME_YEAR_MINIMUM ||
        datetime->year > DATETIME_YEAR_MAXIMUM ||
        datetime->hundredths > DATETIME_HUNDREDTHS_MAXIMUM ||
        datetime->timezone < -DATETIME_TIMEZONE_LIMIT ||
        datetime->timezone > DATETIME_TIMEZONE_LIMIT)
        return ERROR_TS_DATETIME;
    LxHostCalendar value = {0};
    value.year = datetime->year;
    value.month = datetime->month;
    value.day = datetime->day;
    value.hour = datetime->hours;
    value.minute = datetime->minutes;
    value.second = datetime->seconds;
    int64_t seconds;
    if (!lx_host_calendar_seconds(&value, 0, &seconds))
        return ERROR_TS_DATETIME;
    grabLock(&GMutexDosCalls);
    guest_clock_offset = seconds * US_PER_SECOND + datetime->hundredths * US_PER_HUNDREDTH -
                         (int64_t)lx_host_realtime_us();
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}

APIRET DosSetDateTime(PDATETIME datetime)
{
    return Dos16SetDateTime(datetime);
}

APIRET DosGetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("DosGetDateTime(%p)", pdt);

    grabLock(&GMutexDosCalls);
    const uint64 now = lx_host_realtime_us() + guest_clock_offset;
    ungrabLock(&GMutexDosCalls);
    LxHostCalendar value;
    if (!pdt || !lx_host_calendar((int64_t)(now / US_PER_SECOND), 0, &value))
        return ERROR_INVALID_PARAMETER;
    pdt->hours = value.hour;
    pdt->minutes = value.minute;
    pdt->seconds =
        value.second > DATETIME_SECONDS_MAXIMUM ? DATETIME_SECONDS_MAXIMUM : value.second;
    pdt->hundredths = (now % US_PER_SECOND) / US_PER_HUNDREDTH;
    pdt->day = value.day;
    pdt->month = value.month;
    pdt->year = value.year;
    pdt->timezone = -value.utc_offset_minutes;
    pdt->weekday = value.weekday;

    return NO_ERROR;
}

APIRET16 Dos16GetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("Dos16GetDateTime(%p)", pdt);
    return (APIRET16)DosGetDateTime(pdt);
}
