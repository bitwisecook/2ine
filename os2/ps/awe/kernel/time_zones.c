/* Public domain. No rights reserved. Wholly AI-authored. */
/* GetTimeZoneInformation: the guest TZ rule when set, otherwise the host's zone. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/clock.h"
#include <stdio.h>

enum
{
    SECONDS_PER_MINUTE = 60,
    MINUTES_PER_HOUR = 60,
    SECONDS_PER_HOUR = 3600,
    SECONDS_PER_DAY = 86400,
    DAYS_PER_WEEK = 7,
    DAYS_PER_LEAP_YEAR = 366,
    US_PER_SECOND = 1000000,
    /* A rule's week 5 means the last such weekday of the month. */
    LAST_WEEK = 5,
    /* TZ's optional rule: start month, week, weekday, seconds; end month, week,
       weekday, seconds; daylight shift in seconds. */
    TZ_RULE_FIELDS = 9
};

typedef struct ZoneRule
{
    int month, week, weekday, seconds;
} ZoneRule;

typedef struct ZoneRules
{
    char standard[WIN32_TIME_ZONE_NAME_LENGTH], daylight[WIN32_TIME_ZONE_NAME_LENGTH];
    LONG bias, daylight_bias; /* Minutes west of UTC; daylight is relative to standard. */
    ZoneRule start, end;      /* Entering and leaving daylight time. */
    BOOL seasonal;
} ZoneRules;

static int month_days(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return days[month - 1] + (month == 2 && leap);
}

/* Seconds of the rule's moment in the given year, measured on the zone's local clock. */
static int64_t rule_moment(int year, const ZoneRule *rule)
{
    LxHostCalendar first = {.year = year, .month = rule->month, .day = 1};
    int64_t seconds = 0;
    if (!lx_host_calendar_seconds(&first, 1, &seconds) || !lx_host_calendar(seconds, 1, &first))
        return 0;
    int day = 1 + (rule->weekday - first.weekday + DAYS_PER_WEEK) % DAYS_PER_WEEK +
              (rule->week - 1) * DAYS_PER_WEEK;
    while (day > month_days(year, rule->month))
        day -= DAYS_PER_WEEK;
    return seconds + (int64_t)(day - 1) * SECONDS_PER_DAY + rule->seconds;
}

static const char *zone_word(const char *text, char *output)
{
    size_t n = 0;
    while (((*text >= 'A' && *text <= 'Z') || (*text >= 'a' && *text <= 'z')) &&
           n < WIN32_TIME_ZONE_NAME_LENGTH - 1)
        output[n++] = *text++;
    output[n] = 0;
    return text;
}

/* The C library form: NAME[+-]h[:mm[:ss]][DST[,month,week,day,seconds,...,shift]]. */
static BOOL guest_zone(ZoneRules *zone)
{
    char text[128];
    ULONG length = GetEnvironmentVariable("TZ", text, sizeof text);
    if (!length || length >= sizeof text)
        return FALSE;
    const char *p = zone_word(text, zone->standard);
    if (!zone->standard[0])
        return FALSE;
    char *end;
    long hours = strtol(p, &end, 10), minutes = 0;
    if (end == p)
        return FALSE;
    BOOL negative = *p == '-';
    p = end;
    if (*p == ':')
    {
        minutes = strtol(p + 1, &end, 10);
        p = end;
        if (*p == ':')
        {
            strtol(p + 1, &end, 10);
            p = end;
        }
    }
    zone->bias = (LONG)(hours * MINUTES_PER_HOUR + (negative ? -minutes : minutes));
    p = zone_word(p, zone->daylight);
    if (!zone->daylight[0])
        return TRUE;
    /* Without an explicit rule the OS/2 C library starts in April and ends in October. */
    long values[TZ_RULE_FIELDS] = {4, 1, 0, 7200, 10, -1, 0, 7200, 3600};
    for (unsigned i = 0; i < TZ_RULE_FIELDS && *p == ','; i++)
    {
        values[i] = strtol(p + 1, &end, 10);
        p = end;
    }
    /* A negative week counts from the end of the month. */
    zone->start = (ZoneRule){(int)values[0], values[1] < 0 ? LAST_WEEK : (int)values[1],
                             (int)values[2], (int)values[3]};
    zone->end = (ZoneRule){(int)values[4], values[5] < 0 ? LAST_WEEK : (int)values[5],
                           (int)values[6], (int)values[7]};
    if (zone->start.month < 1 || zone->start.month > 12 || zone->end.month < 1 ||
        zone->end.month > 12)
        return FALSE;
    zone->daylight_bias = (LONG)(-values[8] / SECONDS_PER_MINUTE);
    zone->seasonal = TRUE;
    return TRUE;
}

static int host_offset(int64_t seconds)
{
    LxHostCalendar date;
    return lx_host_calendar(seconds, 0, &date) ? date.utc_offset_minutes : 0;
}

/* A rule read back from one observed transition, on the clock in force before it. */
static ZoneRule host_rule(int64_t moment)
{
    LxHostCalendar date = {0};
    int64_t local = moment + (int64_t)host_offset(moment - 1) * SECONDS_PER_MINUTE;
    lx_host_calendar(local, 1, &date);
    int week = (date.day - 1) / DAYS_PER_WEEK + 1;
    if (date.day + DAYS_PER_WEEK > month_days(date.year, date.month))
        week = LAST_WEEK;
    return (ZoneRule){date.month, week, date.weekday,
                      date.hour * SECONDS_PER_HOUR + date.minute * SECONDS_PER_MINUTE +
                          date.second};
}

/* The host has no rule interface, so the year is probed for its offset changes. */
static BOOL host_zone(ZoneRules *zone, int64_t now)
{
    LxHostCalendar date;
    if (!lx_host_calendar(now, 1, &date))
        return FALSE;
    lx_host_zone_names(zone->standard, zone->daylight);
    LxHostCalendar first = {.year = date.year, .month = 1, .day = 1};
    int64_t start = 0;
    if (!lx_host_calendar_seconds(&first, 1, &start))
        return FALSE;
    int64_t changes[2] = {0, 0};
    unsigned found = 0;
    for (unsigned day = 0; day < DAYS_PER_LEAP_YEAR && found < 2; day++)
    {
        int64_t low = start + (int64_t)day * SECONDS_PER_DAY, high = low + SECONDS_PER_DAY;
        if (host_offset(low) == host_offset(high))
            continue;
        while (high - low > 1)
        {
            int64_t middle = low + (high - low) / 2;
            if (host_offset(middle) == host_offset(low))
                low = middle;
            else
                high = middle;
        }
        changes[found++] = high;
    }
    int offset = host_offset(start);
    if (found < 2)
    {
        zone->bias = -host_offset(now);
        return TRUE;
    }
    BOOL forward = host_offset(changes[0]) > offset;
    int standard = forward ? offset : host_offset(changes[0]);
    int daylight = forward ? host_offset(changes[0]) : offset;
    zone->bias = -standard;
    zone->daylight_bias = standard - daylight;
    zone->start = host_rule(changes[forward ? 0 : 1]);
    zone->end = host_rule(changes[forward ? 1 : 0]);
    zone->seasonal = TRUE;
    return TRUE;
}

static void store_zone_name(uint8 *output, const char *name)
{
    /* The guest names are UTF-16; zone names are ASCII. */
    for (size_t i = 0; i < WIN32_TIME_ZONE_NAME_LENGTH - 1 && name[i]; i++)
        lx_guest_store_u16(output + i * sizeof(USHORT), (uint8)name[i]);
}

static void store_zone_rule(uint8 *output, const ZoneRule *rule)
{
    /* A transition rule is a SYSTEMTIME whose day holds the week of the month. */
    lx_guest_store_u16(output + WIN32_SYSTEMTIME_MONTH, (USHORT)rule->month);
    lx_guest_store_u16(output + WIN32_SYSTEMTIME_DAY_OF_WEEK, (USHORT)rule->weekday);
    lx_guest_store_u16(output + WIN32_SYSTEMTIME_DAY, (USHORT)rule->week);
    lx_guest_store_u16(output + WIN32_SYSTEMTIME_HOUR, (USHORT)(rule->seconds / SECONDS_PER_HOUR));
    lx_guest_store_u16(output + WIN32_SYSTEMTIME_MINUTE,
                       (USHORT)(rule->seconds / SECONDS_PER_MINUTE % 60));
    lx_guest_store_u16(output + WIN32_SYSTEMTIME_SECOND,
                       (USHORT)(rule->seconds % SECONDS_PER_MINUTE));
}

ULONG GetTimeZoneInformation(PVOID output)
{
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return WIN32_TIME_ZONE_ID_INVALID;
    }
    int64_t now = (int64_t)(lx_host_realtime_us() / US_PER_SECOND);
    ZoneRules zone = {0};
    if (!guest_zone(&zone))
    {
        zone = (ZoneRules){0};
        if (!host_zone(&zone, now))
        {
            open32SetError(ERROR_GEN_FAILURE);
            return WIN32_TIME_ZONE_ID_INVALID;
        }
    }
    uint8 *wire = output;
    memset(wire, 0, WIN32_TIME_ZONE_INFORMATION_SIZE);
    lx_guest_store_u32(wire + WIN32_TIME_ZONE_INFORMATION_BIAS, (ULONG)zone.bias);
    store_zone_name(wire + WIN32_TIME_ZONE_INFORMATION_STANDARD_NAME, zone.standard);
    store_zone_name(wire + WIN32_TIME_ZONE_INFORMATION_DAYLIGHT_NAME, zone.daylight);
    /* A zone without transitions has no rules. */
    if (!zone.seasonal)
        return WIN32_TIME_ZONE_ID_UNKNOWN;
    store_zone_rule(wire + WIN32_TIME_ZONE_INFORMATION_STANDARD_DATE, &zone.end);
    store_zone_rule(wire + WIN32_TIME_ZONE_INFORMATION_DAYLIGHT_DATE, &zone.start);
    lx_guest_store_u32(wire + WIN32_TIME_ZONE_INFORMATION_DAYLIGHT_BIAS, (ULONG)zone.daylight_bias);
    /* Both rule moments and the current time are compared on the standard clock. */
    LxHostCalendar date;
    int64_t local = now - (int64_t)zone.bias * SECONDS_PER_MINUTE;
    if (!lx_host_calendar(local, 1, &date))
        return WIN32_TIME_ZONE_ID_UNKNOWN;
    int64_t start = rule_moment(date.year, &zone.start);
    int64_t end =
        rule_moment(date.year, &zone.end) + (int64_t)zone.daylight_bias * SECONDS_PER_MINUTE;
    BOOL daylight = start < end ? local >= start && local < end : local >= start || local < end;
    return daylight ? WIN32_TIME_ZONE_ID_DAYLIGHT : WIN32_TIME_ZONE_ID_STANDARD;
}
