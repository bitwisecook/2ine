/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 service adapters. Original PMWINX export bodies and the OS/2 toolkit
   declarations determine validation, return conventions and native routing. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/case_tables.h"
#include "os2libs/pmshapi.h"
#include "hal/host.h"
#include <stdio.h>

BOOL AdjustWindowRect(PVOID rectangle, ULONG style, BOOL menu)
{
    return AdjustWindowRectEx(rectangle, style, menu, 0);
}
BOOL Beep(ULONG frequency, ULONG duration)
{
    typedef APIRET(OS2API * Fn)(ULONG, ULONG);
    Fn call = (Fn)open32DosService("DosBeep");
    ULONG error = frequency == UINT32_MAX ? 50 : call ? call(frequency, duration) : 120;
    if (error == 395) /* ERROR_INVALID_FREQUENCY */
        error = 87;
    if (error)
        open32SetError(error);
    return !error;
}
LONG CompareFileTime(PVOID first, PVOID second)
{
    if (!first || !second)
    {
        open32SetError(87);
        return -1;
    }
    ULONG ah = lx_guest_load_u32((uint8 *)first + 4), bh = lx_guest_load_u32((uint8 *)second + 4);
    ULONG al = lx_guest_load_u32(first), bl = lx_guest_load_u32(second);
    return ah != bh ? (ah > bh ? 1 : -1) : al != bl ? (al > bl ? 1 : -1) : 0;
}
BOOL DosDateTimeToFileTime(USHORT date, USHORT time, PVOID output)
{
    uint8 fields[16] = {0};
    lx_guest_store_u16(fields, 1980 + (date >> 9));
    lx_guest_store_u16(fields + 2, (date >> 5) & 15);
    lx_guest_store_u16(fields + 6, date & 31);
    lx_guest_store_u16(fields + 8, time >> 11);
    lx_guest_store_u16(fields + 10, (time >> 5) & 63);
    lx_guest_store_u16(fields + 12, (time & 31) * 2);
    return SystemTimeToFileTime(fields, output);
}
BOOL FileTimeToDosDateTime(PVOID input, PVOID date, PVOID time)
{
    uint8 fields[16];
    if (!date || !time || !FileTimeToSystemTime(input, fields))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG year = lx_guest_load_u16(fields);
    if (year < 1980 || year > 2107)
    {
        open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u16(date, ((year - 1980) << 9) | (lx_guest_load_u16(fields + 2) << 5) |
                                 lx_guest_load_u16(fields + 6));
    lx_guest_store_u16(time, (lx_guest_load_u16(fields + 8) << 11) |
                                 (lx_guest_load_u16(fields + 10) << 5) |
                                 (lx_guest_load_u16(fields + 12) / 2));
    return TRUE;
}
BOOL CopyFile(PSZ source, PSZ destination, BOOL exclusive)
{
    typedef APIRET(OS2API * Fn)(PSZ, PSZ, ULONG);
    Fn call = (Fn)open32DosService("DosCopy");
    ULONG error = !source || !destination ? 87
                  : call                  ? call(source, destination, exclusive ? 0 : 1)
                                          : 120;
    if (error)
        open32SetError(error);
    return !error;
}
static BOOL path_operation(PSZ path, const char *name)
{
    typedef APIRET(OS2API * Fn)(PSZ);
    Fn call = (Fn)open32DosService(name);
    ULONG error = !path ? 87 : call ? call(path) : 120;
    if (error)
        open32SetError(error);
    return !error;
}
BOOL DeleteFile(PSZ path)
{
    return path_operation(path, "DosDelete");
}
BOOL RemoveDirectory(PSZ path)
{
    return path_operation(path, "DosDeleteDir");
}
BOOL MoveFile(PSZ source, PSZ destination)
{
    typedef APIRET(OS2API * Fn)(PSZ, PSZ);
    Fn call = (Fn)open32DosService("DosMove");
    ULONG error = !source || !destination ? 87 : call ? call(source, destination) : 120;
    if (error)
        open32SetError(error);
    return !error;
}
BOOL CreateDirectory(PSZ path, PVOID security)
{
    (void)security; /* Open32 passes no security information to DosCreateDir. */
    typedef APIRET(OS2API * Fn)(PSZ, PVOID);
    Fn call = (Fn)open32DosService("DosCreateDir");
    ULONG error = !path ? 87 : call ? call(path, NULL) : 120;
    if (error)
        open32SetError(error);
    return !error;
}
BOOL SetFileAttributes(PSZ path, ULONG attributes)
{
    typedef APIRET(OS2API * Query)(PSZ, ULONG, PVOID, ULONG);
    typedef APIRET(OS2API * Set)(PSZ, ULONG, PVOID, ULONG, ULONG);
    Query query = (Query)open32DosService("DosQueryPathInfo");
    Set set = (Set)open32DosService("DosSetPathInfo");
    uint8 status[24] = {0};
    /* FILE_ATTRIBUTE_NORMAL stands alone; TEMPORARY is accepted and has no OS/2 bit. */
    ULONG error = !path || (attributes & ~0x1A7u) || ((attributes & 0x80) && (attributes & ~0x80u))
                      ? 87
                  : !query || !set ? 120
                                   : query(path, 1, status, sizeof status);
    if (!error)
    {
        lx_guest_store_u32(status + 20, attributes & 0x27);
        error = set(path, 1, status, sizeof status, 0);
    }
    if (error)
        open32SetError(error);
    return !error;
}
LONG _lopen(PSZ path, LONG flags)
{
    typedef APIRET(OS2API * Fn)(PSZ, PULONG, PULONG, ULONG, ULONG, ULONG, ULONG, PVOID);
    Fn call = (Fn)open32DosService("DosOpen");
    ULONG handle = 0, action = 0;
    ULONG error = !path || (flags & ~0x73) || (flags & 3) == 3 ? 87
                  : !call ? 120
                          : call(path, &handle, &action, 0, 0, 1,
                                 (flags & 0x73) | ((flags & 0x70) ? 0 : 0x40), NULL);
    if (error)
        open32SetError(error);
    return error ? -1 : (LONG)handle;
}
LONG _lcreat(PSZ path, LONG attributes)
{
    typedef APIRET(OS2API * Fn)(PSZ, PULONG, PULONG, ULONG, ULONG, ULONG, ULONG, PVOID);
    Fn call = (Fn)open32DosService("DosOpen");
    ULONG handle = 0, action = 0;
    /* Read-only creation selects read access; others are exclusive read/write. */
    ULONG mode = attributes & 1 ? 0 : 0x12;
    ULONG error = !path  ? 87
                  : !call ? 120
                          : call(path, &handle, &action, 0, attributes & 7, 0x12, mode, NULL);
    if (error)
        open32SetError(error);
    return error ? -1 : (LONG)handle;
}
ULONG SetHandleCount(ULONG count)
{
    typedef APIRET(OS2API * Fn)(PLONG, PULONG);
    Fn call = (Fn)open32DosService("DosSetRelMaxFH");
    LONG delta = 0;
    ULONG current = 0;
    if (!call)
        return 0;
    call(&delta, &current);
    delta = (LONG)(count - current);
    call(&delta, &count);
    return count;
}
SHORT GetFileTitle(PSZ path, PSZ output, USHORT capacity)
{
    if (!path || !output)
        return -1;
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            name = p + 1;
    size_t length = strlen(name) + 1;
    if (!*name || strpbrk(name, "*?"))
        return -1;
    if (length > capacity)
        return (SHORT)(length > INT16_MAX ? INT16_MAX : length);
    memcpy(output, name, length);
    return 0;
}
ULONG GetSystemDirectory(PSZ output, ULONG capacity)
{
    char path[260];
    ULONG length = GetWindowsDirectory(path, sizeof path);
    if (!output || !length || length >= sizeof path - 8)
        return 0;
    memcpy(path + length, "\\SYSTEM", 8);
    length += 7;
    if (capacity <= length)
        return length + 1;
    memcpy(output, path, length + 1);
    return length;
}
ULONG GetPrivateProfileInt(PSZ section, PSZ key, LONG fallback, PSZ filename)
{
    char text[64], *end;
    if (!section || !key || !filename)
    {
        open32SetError(87);
        return (ULONG)fallback;
    }
    if (!GetPrivateProfileString(section, key, "", text, sizeof text, filename))
        return (ULONG)fallback;
    long value = strtol(text, &end, 0);
    return end == text ? 0 : (ULONG)value;
}
BOOL WriteProfileString(PSZ section, PSZ key, PSZ value)
{
    if (!section)
        return !key && !value;
    size_t size = strlen(section) + 6;
    char *app = malloc(size);
    if (!app)
    {
        open32SetError(8);
        return FALSE;
    }
    snprintf(app, size, "WINI_%s", section);
    BOOL result = PrfWriteProfileString(HINI_USERPROFILE, app, key, value);
    free(app);
    return result;
}
VOID ZeroMemory(PVOID output, ULONG count)
{
    if (count)
        memset(output, 0, count);
}
BOOL IsBadHugeReadPtr(PVOID pointer, ULONG size)
{
    return IsBadReadPtr(pointer, size);
}
BOOL IsBadHugeWritePtr(PVOID pointer, ULONG size)
{
    return IsBadWritePtr(pointer, size);
}
BOOL IsBadStringPtr(ULONG address, ULONG maximum)
{
    for (ULONG i = 0; i < maximum; i++)
    {
        uint8 byte;
        if (address > UINT32_MAX - i ||
            !lx_execution_read(GLoaderState.execution, address + i, &byte, 1))
        {
            open32SetError(5);
            return TRUE;
        }
        if (!byte)
            return FALSE;
    }
    return FALSE;
}
static ULONG character_type(ULONG character)
{
    if (character > 255)
        return 0;
    char byte = (char)character;
    uint8 flags[2];
    return GetStringTypeEx(0x409, 1, &byte, 1, flags) ? lx_guest_load_u16(flags) : 0;
}
BOOL IsCharUpper(ULONG character)
{
    return (character_type(character) & 1) != 0;
}
BOOL IsCharAlpha(ULONG character)
{
    return (character_type(character) & 0x100) != 0;
}
BOOL IsCharAlphaNumeric(ULONG character)
{
    return (character_type(character) & 0x104) != 0;
}
LONG lstrcmp2(PSZ first, PSZ second)
{
    if (!first || !second)
    {
        open32SetError(87);
        return 0;
    }
    /* Both operands are compared as UTF-16 units from the OEM code page. */
    ULONG page = GetOEMCP();
    LONG a = MultiByteToWideChar(page, 1, first, -1, NULL, 0);
    LONG b = MultiByteToWideChar(page, 1, second, -1, NULL, 0);
    uint8 *left = a > 0 ? malloc((size_t)a * 2) : NULL;
    uint8 *right = b > 0 ? malloc((size_t)b * 2) : NULL;
    LONG result = 0;
    if (!left || !right || !MultiByteToWideChar(page, 1, first, -1, left, a) ||
        !MultiByteToWideChar(page, 1, second, -1, right, b))
        open32SetError(8);
    else
        for (LONG i = 0; i < a && i < b && !result; i++)
        {
            USHORT x = lx_guest_load_u16(left + i * 2), y = lx_guest_load_u16(right + i * 2);
            result = x < y ? -1 : x > y;
        }
    free(left);
    free(right);
    return result;
}
VOID OutputDebugString(PSZ text)
{
    if (text)
        fprintf(stderr, "Open32: %s", text);
}
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

BOOL GetStringTypeEx(ULONG locale, ULONG kind, PSZ input, LONG count, PVOID output)
{
    (void)locale; /* Input is single-byte text, and its classes do not vary by locale here. */
    if (!input || !output || count == 0 || count < -1 || count > 1024 * 1024 ||
        (kind != 1 && kind != 2 && kind != 4))
    {
        open32SetError(87);
        return FALSE;
    }
    if (count == -1)
        count = (LONG)strlen(input) + 1;
    uint8 *wide = malloc((size_t)count * 2);
    if (!wide)
    {
        open32SetError(8);
        return FALSE;
    }
    if (!MultiByteToWideChar(0, 0, input, count, wide, count))
    {
        free(wide);
        return FALSE;
    }
    for (LONG i = 0; i < count; i++)
    {
        ULONG c = lx_guest_load_u16(wide + i * 2);
        BOOL upper = (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ||
                     c == 0x178 || c == 0x152 || c == 0x160 || c == 0x17D;
        BOOL lower = (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) ||
                     c == 0x153 || c == 0x161 || c == 0x17E || c == 0x192;
        BOOL alpha = upper || lower || c == 0xAA || c == 0xBA || c == 0xB5;
        BOOL digit = c >= '0' && c <= '9', control = c < 32 || (c >= 0x7F && c < 0xA0);
        BOOL space = (c >= 9 && c <= 13) || c == 32 || c == 0xA0;
        BOOL blank = c == 9 || c == 32 || c == 0xA0;
        BOOL defined = c != 0xFFFD;
        USHORT value = 0;
        if (kind == 1)
            value = (upper ? 1 : 0) | (lower ? 2 : 0) | (digit ? 4 : 0) | (space ? 8 : 0) |
                    (!alpha && !digit && !space && !control && defined ? 0x10 : 0) |
                    (control ? 0x20 : 0) | (blank ? 0x40 : 0) |
                    (digit || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') ? 0x80 : 0) |
                    (alpha ? 0x100 : 0) | (defined ? 0x200 : 0);
        else if (kind == 2)
            value = alpha                                               ? 1
                    : digit                                             ? 3
                    : c == '\r' || c == '\n'                            ? 8
                    : c == '\t'                                         ? 9
                    : space                                             ? 10
                    : c == '+' || c == '-'                              ? 4
                    : c == '$' || c == 0xA3 || c == 0xA5 || c == 0x20AC ? 5
                    : c == ',' || c == '.' || c == ':'                  ? 7
                                                                        : 11;
        else
            value =
                (alpha ? 0x8000 : 0) |
                (c == 0xA8 || c == 0xAF || c == 0xB4 || c == 0xB8 || c == 0x2C6 || c == 0x2DC ? 2
                                                                                              : 0) |
                (!alpha && !digit && !space && !control ? 8 : 0);
        lx_guest_store_u16((uint8 *)output + i * 2, value);
    }
    free(wide);
    return TRUE;
}
