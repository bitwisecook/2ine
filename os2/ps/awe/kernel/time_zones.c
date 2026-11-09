/* Public domain. No rights reserved. Wholly AI-authored. */
/* GetTimeZoneInformation: the guest TZ rule when set, otherwise the host's zone. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/clock.h"
#include <stdio.h>

typedef struct ZoneRule
{
    int month, week, weekday, seconds;
} ZoneRule;

typedef struct ZoneRules
{
    char standard[32], daylight[32];
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
    int day = 1 + (rule->weekday - first.weekday + 7) % 7 + (rule->week - 1) * 7;
    while (day > month_days(year, rule->month))
        day -= 7;
    return seconds + (int64_t)(day - 1) * 86400 + rule->seconds;
}

static const char *zone_word(const char *text, char *output)
{
    size_t n = 0;
    while (((*text >= 'A' && *text <= 'Z') || (*text >= 'a' && *text <= 'z')) && n < 31)
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
    zone->bias = (LONG)(hours * 60 + (negative ? -minutes : minutes));
    p = zone_word(p, zone->daylight);
    if (!zone->daylight[0])
        return TRUE;
    /* Without an explicit rule the OS/2 C library starts in April and ends in October. */
    long values[9] = {4, 1, 0, 7200, 10, -1, 0, 7200, 3600};
    for (unsigned i = 0; i < 9 && *p == ','; i++)
    {
        values[i] = strtol(p + 1, &end, 10);
        p = end;
    }
    zone->start = (ZoneRule){(int)values[0], values[1] < 0 ? 5 : (int)values[1], (int)values[2],
                             (int)values[3]};
    zone->end = (ZoneRule){(int)values[4], values[5] < 0 ? 5 : (int)values[5], (int)values[6],
                           (int)values[7]};
    if (zone->start.month < 1 || zone->start.month > 12 || zone->end.month < 1 ||
        zone->end.month > 12)
        return FALSE;
    zone->daylight_bias = (LONG)(-values[8] / 60);
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
    int64_t local = moment + (int64_t)host_offset(moment - 1) * 60;
    lx_host_calendar(local, 1, &date);
    int week = (date.day - 1) / 7 + 1;
    if (date.day + 7 > month_days(date.year, date.month))
        week = 5;
    return (ZoneRule){date.month, week, date.weekday,
                      date.hour * 3600 + date.minute * 60 + date.second};
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
    for (unsigned day = 0; day < 366 && found < 2; day++)
    {
        int64_t low = start + (int64_t)day * 86400, high = low + 86400;
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
    for (size_t i = 0; i < 31 && name[i]; i++)
        lx_guest_store_u16(output + i * 2, (uint8)name[i]);
}

static void store_zone_rule(uint8 *output, const ZoneRule *rule)
{
    lx_guest_store_u16(output + 2, (USHORT)rule->month);
    lx_guest_store_u16(output + 4, (USHORT)rule->weekday);
    lx_guest_store_u16(output + 6, (USHORT)rule->week);
    lx_guest_store_u16(output + 8, (USHORT)(rule->seconds / 3600));
    lx_guest_store_u16(output + 10, (USHORT)(rule->seconds / 60 % 60));
    lx_guest_store_u16(output + 12, (USHORT)(rule->seconds % 60));
}

ULONG GetTimeZoneInformation(PVOID output)
{
    if (!output)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    int64_t now = (int64_t)(lx_host_realtime_us() / 1000000);
    ZoneRules zone = {0};
    if (!guest_zone(&zone))
    {
        zone = (ZoneRules){0};
        if (!host_zone(&zone, now))
        {
            open32SetError(31);
            return UINT32_MAX;
        }
    }
    uint8 *wire = output;
    memset(wire, 0, 172);
    lx_guest_store_u32(wire, (ULONG)zone.bias);
    store_zone_name(wire + 4, zone.standard);
    store_zone_name(wire + 88, zone.daylight);
    if (!zone.seasonal)
        return 0; /* TIME_ZONE_ID_UNKNOWN: this zone has no transitions. */
    store_zone_rule(wire + 68, &zone.end);
    store_zone_rule(wire + 152, &zone.start);
    lx_guest_store_u32(wire + 168, (ULONG)zone.daylight_bias);
    /* Both rule moments and the current time are compared on the standard clock. */
    LxHostCalendar date;
    int64_t local = now - (int64_t)zone.bias * 60;
    if (!lx_host_calendar(local, 1, &date))
        return 0;
    int64_t start = rule_moment(date.year, &zone.start);
    int64_t end = rule_moment(date.year, &zone.end) + (int64_t)zone.daylight_bias * 60;
    BOOL daylight = start < end ? local >= start && local < end : local >= start || local < end;
    return daylight ? 2 : 1;
}
