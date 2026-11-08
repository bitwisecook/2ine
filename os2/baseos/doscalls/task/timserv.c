/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

/* Clock adjustments belong to the emulated process and never change host time. */
static int64_t guest_clock_offset;

APIRET16 Dos16SetDateTime(PDATETIME datetime)
{
    if (!datetime || datetime->year < 1980 || datetime->year > 2079 || datetime->hundredths > 99 ||
        datetime->timezone < -780 || datetime->timezone > 780)
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
    guest_clock_offset =
        seconds * 1000000 + datetime->hundredths * 10000 - (int64_t)lx_host_realtime_us();
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
    if (!pdt || !lx_host_calendar((int64_t)(now / 1000000), 0, &value))
        return ERROR_INVALID_PARAMETER;
    pdt->hours = value.hour;
    pdt->minutes = value.minute;
    pdt->seconds = value.second > 59 ? 59 : value.second;
    pdt->hundredths = (now % 1000000) / 10000;
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
