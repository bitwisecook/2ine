/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 UTC calendar records and 100-nanosecond intervals since 1601. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/clock.h"

enum
{
    US_PER_SECOND = 1000000,
    US_PER_MS = 1000,
    SECONDS_PER_MINUTE = 60,
    SECONDS_PER_HOUR = 3600,
    SECONDS_PER_DAY = 86400,
    /* FILETIME counts 100-nanosecond ticks from 1601-01-01, a Monday. */
    TICKS_PER_SECOND = 10000000,
    TICKS_PER_MS = 10000,
    FILETIME_EPOCH_YEAR = 1601,
    /* Gregorian days before 1601-01-01: 1600*365+400-16+4. */
    DAYS_BEFORE_FILETIME_EPOCH = 584388,
    SYSTEMTIME_YEAR_MAX = 30827,
    /* One past 30828, the last year a 63-bit FILETIME reaches. */
    FILETIME_YEAR_LIMIT = 30829,
    /* MS-DOS dates: year since 1980 in bits 9-15, month 5-8, day 0-4; times: hour 11-15,
       minute 5-10, seconds/2 0-4. */
    DOS_EPOCH_YEAR = 1980,
    DOS_YEAR_MAX = 2107,
    DOS_YEAR_SHIFT = 9,
    DOS_MONTH_SHIFT = 5,
    DOS_MONTH_MASK = 15,
    DOS_DAY_MASK = 31,
    DOS_HOUR_SHIFT = 11,
    DOS_MINUTE_SHIFT = 5,
    DOS_MINUTE_MASK = 63,
    DOS_SECONDS_MASK = 31
};

static void get_time(PVOID output, int utc)
{
    uint64_t now = lx_host_realtime_us();
    LxHostCalendar value;
    if (!output || !lx_host_calendar((int64_t)(now / US_PER_SECOND), utc, &value))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return;
    }
    uint16 fields[] = {(uint16)value.year,    (uint16)value.month,
                       (uint16)value.weekday, (uint16)value.day,
                       (uint16)value.hour,    (uint16)value.minute,
                       (uint16)value.second,  (uint16)((now % US_PER_SECOND) / US_PER_MS)};
    /* SYSTEMTIME: eight WORDs in this order. */
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u16((uint8 *)output + sizeof *fields * i, fields[i]);
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const uint8 *wire = input;
    unsigned year = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_YEAR),
             month = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_MONTH),
             day = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_DAY),
             hour = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_HOUR),
             minute = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_MINUTE),
             second = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_SECOND),
             millisecond = lx_guest_load_u16(wire + WIN32_SYSTEMTIME_MILLISECONDS);
    static const unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (year < FILETIME_EPOCH_YEAR || year > SYSTEMTIME_YEAR_MAX || month < 1 || month > 12 ||
        day < 1 || day > month_days[month - 1] + (month == 2 && leap) || hour > 23 || minute > 59 ||
        second > 59 || millisecond > 999)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint64_t previous = year - 1;
    uint64_t days = previous * 365 + previous / 4 - previous / 100 + previous / 400 -
                    DAYS_BEFORE_FILETIME_EPOCH;
    for (unsigned m = 1; m < month; m++)
        days += month_days[m - 1] + (m == 2 && leap);
    days += day - 1;
    uint64_t ticks = (((days * 24 + hour) * 60 + minute) * 60 + second) * TICKS_PER_SECOND +
                     (uint64_t)millisecond * TICKS_PER_MS;
    lx_guest_store_u32((uint8 *)output + WIN32_FILETIME_LOW, (uint32)ticks);
    lx_guest_store_u32((uint8 *)output + WIN32_FILETIME_HIGH, (uint32)(ticks >> 32));
    return TRUE;
}

static uint64_t load_filetime(const void *input)
{
    return lx_guest_load_u32((const uint8 *)input + WIN32_FILETIME_LOW) |
           (uint64_t)lx_guest_load_u32((const uint8 *)input + WIN32_FILETIME_HIGH) << 32;
}

static BOOL local_filetime(PVOID input, PVOID output, BOOL utc)
{
    if (!input || !output || input == output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint64_t value = load_filetime(input);
    LxHostCalendar now;
    if (value > INT64_MAX ||
        !lx_host_calendar((int64_t)(lx_host_realtime_us() / US_PER_SECOND), 0, &now))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* These Windows APIs use the current timezone/DST bias, even for historical dates. */
    int64_t adjustment = (int64_t)now.utc_offset_minutes * SECONDS_PER_MINUTE * TICKS_PER_SECOND;
    if (utc)
        adjustment = -adjustment;
    if ((adjustment < 0 && value < (uint64_t)-adjustment) ||
        (adjustment >= 0 && value > (uint64_t)(INT64_MAX - adjustment)))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint64_t result =
        adjustment >= 0 ? value + (uint64_t)adjustment : value - (uint64_t)-adjustment;
    lx_guest_store_u32((uint8 *)output + WIN32_FILETIME_LOW, (uint32)result);
    lx_guest_store_u32((uint8 *)output + WIN32_FILETIME_HIGH, (uint32)(result >> 32));
    return TRUE;
}

BOOL FileTimeToLocalFileTime(PVOID input, PVOID output)
{
    return local_filetime(input, output, FALSE);
}

BOOL LocalFileTimeToFileTime(PVOID input, PVOID output)
{
    return local_filetime(input, output, TRUE);
}

static uint64_t year_days(unsigned year)
{
    uint64_t previous = year - 1;
    return previous * 365 + previous / 4 - previous / 100 + previous / 400 -
           DAYS_BEFORE_FILETIME_EPOCH;
}

BOOL FileTimeToSystemTime(PVOID input, PVOID output)
{
    if (!input || !output || load_filetime(input) > INT64_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint64_t ticks = load_filetime(input), seconds = ticks / TICKS_PER_SECOND;
    uint64_t days = seconds / SECONDS_PER_DAY;
    unsigned low = FILETIME_EPOCH_YEAR, high = FILETIME_YEAR_LIMIT;
    while (low + 1 < high)
    {
        unsigned middle = low + (high - low) / 2;
        if (year_days(middle) <= days)
            low = middle;
        else
            high = middle;
    }
    static const unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = low % 4 == 0 && (low % 100 != 0 || low % 400 == 0);
    unsigned day = (unsigned)(days - year_days(low)), month = 0;
    while (month < 11 && day >= month_days[month] + (month == 1 && leap))
    {
        day -= month_days[month] + (month == 1 && leap);
        month++;
    }
    uint16_t fields[] = {(uint16_t)low, (uint16_t)(month + 1),
                         /* Day 0 is a Monday; Sunday is 0. */
                         (uint16_t)((days + 1) % 7), (uint16_t)(day + 1),
                         (uint16_t)((seconds / SECONDS_PER_HOUR) % 24),
                         (uint16_t)((seconds / SECONDS_PER_MINUTE) % 60),
                         (uint16_t)(seconds % SECONDS_PER_MINUTE),
                         (uint16_t)((ticks % TICKS_PER_SECOND) / TICKS_PER_MS)};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u16((uint8 *)output + i * sizeof *fields, fields[i]);
    return TRUE;
}

LONG CompareFileTime(PVOID first, PVOID second)
{
    if (!first || !second)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    ULONG ah = lx_guest_load_u32((uint8 *)first + WIN32_FILETIME_HIGH),
          bh = lx_guest_load_u32((uint8 *)second + WIN32_FILETIME_HIGH);
    ULONG al = lx_guest_load_u32((uint8 *)first + WIN32_FILETIME_LOW),
          bl = lx_guest_load_u32((uint8 *)second + WIN32_FILETIME_LOW);
    return ah != bh ? (ah > bh ? 1 : -1) : al != bl ? (al > bl ? 1 : -1) : 0;
}

BOOL DosDateTimeToFileTime(USHORT date, USHORT time, PVOID output)
{
    uint8 fields[WIN32_SYSTEMTIME_SIZE] = {0};
    lx_guest_store_u16(fields + WIN32_SYSTEMTIME_YEAR, DOS_EPOCH_YEAR + (date >> DOS_YEAR_SHIFT));
    lx_guest_store_u16(fields + WIN32_SYSTEMTIME_MONTH, (date >> DOS_MONTH_SHIFT) & DOS_MONTH_MASK);
    lx_guest_store_u16(fields + WIN32_SYSTEMTIME_DAY, date & DOS_DAY_MASK);
    lx_guest_store_u16(fields + WIN32_SYSTEMTIME_HOUR, time >> DOS_HOUR_SHIFT);
    lx_guest_store_u16(fields + WIN32_SYSTEMTIME_MINUTE,
                       (time >> DOS_MINUTE_SHIFT) & DOS_MINUTE_MASK);
    lx_guest_store_u16(fields + WIN32_SYSTEMTIME_SECOND, (time & DOS_SECONDS_MASK) * 2);
    return SystemTimeToFileTime(fields, output);
}

BOOL FileTimeToDosDateTime(PVOID input, PVOID date, PVOID time)
{
    uint8 fields[WIN32_SYSTEMTIME_SIZE];
    if (!date || !time || !FileTimeToSystemTime(input, fields))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG year = lx_guest_load_u16(fields + WIN32_SYSTEMTIME_YEAR);
    if (year < DOS_EPOCH_YEAR || year > DOS_YEAR_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lx_guest_store_u16(date,
                       ((year - DOS_EPOCH_YEAR) << DOS_YEAR_SHIFT) |
                           (lx_guest_load_u16(fields + WIN32_SYSTEMTIME_MONTH) << DOS_MONTH_SHIFT) |
                           lx_guest_load_u16(fields + WIN32_SYSTEMTIME_DAY));
    lx_guest_store_u16(
        time, (lx_guest_load_u16(fields + WIN32_SYSTEMTIME_HOUR) << DOS_HOUR_SHIFT) |
                  (lx_guest_load_u16(fields + WIN32_SYSTEMTIME_MINUTE) << DOS_MINUTE_SHIFT) |
                  (lx_guest_load_u16(fields + WIN32_SYSTEMTIME_SECOND) / 2));
    return TRUE;
}
