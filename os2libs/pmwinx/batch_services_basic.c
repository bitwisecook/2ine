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
ULONG GetTimeZoneInformation(PVOID output)
{
    if (!output)
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    LxHostCalendar now;
    if (!lx_host_calendar((int64_t)(lx_host_realtime_us() / 1000000), 0, &now))
    {
        open32SetError(31);
        return UINT32_MAX;
    }
    memset(output, 0, 172);
    lx_guest_store_u32(output, (ULONG)-now.utc_offset_minutes);
    /* The host exposes the effective bias, but no transition rules. UNKNOWN is
       the defined result when both transition SYSTEMTIME records are empty. */
    return 0;
}

BOOL GetStringTypeEx(ULONG locale, ULONG kind, PSZ input, LONG count, PVOID output)
{
    if (!input || !output || count == 0 || count < -1 || count > 1024 * 1024 ||
        (kind != 1 && kind != 2 && kind != 4) ||
        (locale && locale != 0x400 && locale != 0x800 && locale != 0x409))
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
