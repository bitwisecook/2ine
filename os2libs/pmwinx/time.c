/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 UTC calendar records and 100-nanosecond intervals since 1601. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host.h"

static void get_time(PVOID output, int utc)
{
    uint64_t now = lx_host_realtime_us();
    LxHostCalendar value;
    if (!output || !lx_host_calendar((int64_t)(now / 1000000), utc, &value))
    {
        open32SetError(87);
        return;
    }
    uint16 fields[] = {(uint16)value.year,    (uint16)value.month,
                       (uint16)value.weekday, (uint16)value.day,
                       (uint16)value.hour,    (uint16)value.minute,
                       (uint16)value.second,  (uint16)((now % 1000000) / 1000)};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u16((uint8 *)output + 2 * i, fields[i]);
}

VOID GetSystemTime(PVOID output)
{
    get_time(output, 1);
}
VOID GetLocalTime(PVOID output)
{
    get_time(output, 0);
}

BOOL SystemTimeToFileTime(PVOID input, PVOID output)
{
    if (!input || !output)
    {
        open32SetError(87);
        return FALSE;
    }
    const uint8 *wire = input;
    unsigned year = lx_guest_load_u16(wire), month = lx_guest_load_u16(wire + 2),
             day = lx_guest_load_u16(wire + 6), hour = lx_guest_load_u16(wire + 8),
             minute = lx_guest_load_u16(wire + 10), second = lx_guest_load_u16(wire + 12),
             millisecond = lx_guest_load_u16(wire + 14);
    static const unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < 1601 || year > 30827 || month < 1 || month > 12 || day < 1 ||
        day > month_days[month - 1] + (month == 2 && leap) || hour > 23 || minute > 59 ||
        second > 59 || millisecond > 999)
    {
        open32SetError(87);
        return FALSE;
    }
    uint64_t previous = year - 1;
    /* Gregorian days before 1601-01-01: 1600*365+400-16+4 = 584388. */
    uint64_t days = previous * 365 + previous / 4 - previous / 100 + previous / 400 - 584388;
    for (unsigned m = 1; m < month; m++)
        days += month_days[m - 1] + (m == 2 && leap);
    days += day - 1;
    uint64_t ticks = (((days * 24 + hour) * 60 + minute) * 60 + second) * 10000000 +
                     (uint64_t)millisecond * 10000;
    lx_guest_store_u32(output, (uint32)ticks);
    lx_guest_store_u32((uint8 *)output + 4, (uint32)(ticks >> 32));
    return TRUE;
}
