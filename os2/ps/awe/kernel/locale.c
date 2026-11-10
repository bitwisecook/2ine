/* Public domain. No rights reserved. Wholly AI-authored. */
/* The current NLS provider supplies the US English ANSI locale. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/bsedos.h"

enum
{
    /* MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT) */
    ENGLISH_US_LCID = 0x0409,
    OEM_CODE_PAGE = 437,
    ANSI_CODE_PAGE = 1252,
    /* GetLocaleInfo types may carry LOCALE_NOUSEROVERRIDE and LOCALE_USE_CP_ACP;
       any other bit above the type is refused. */
    LOCALE_TYPE_RESERVED_BITS = 0x3FFF0000,
    /* DosQueryDBCSEnv: up to five lead-byte ranges and the 0,0 that ends them. */
    DBCS_RANGE_BYTES = 12,
    STRING_TYPE_INPUT_LIMIT = 1024 * 1024,
    UTF16_UNIT_BYTES = 2
};

static const struct
{
    ULONG type;
    const char *value;
} locale_items[] = {
    {WIN32_LOCALE_ILANGUAGE, "0409"},
    {WIN32_LOCALE_SLANGUAGE, "English"},
    {WIN32_LOCALE_SABBREVLANGNAME, "ENU"},
    {WIN32_LOCALE_SNATIVELANGNAME, "English"},
    {WIN32_LOCALE_ICOUNTRY, "1"},
    {WIN32_LOCALE_SCOUNTRY, "United States"},
    {WIN32_LOCALE_SABBREVCTRYNAME, "US"},
    {WIN32_LOCALE_SNATIVECTRYNAME, "United States"},
    {WIN32_LOCALE_IDEFAULTLANGUAGE, "409"},
    {WIN32_LOCALE_IDEFAULTCOUNTRY, "1"},
    {WIN32_LOCALE_IDEFAULTCODEPAGE, "437"},
    {WIN32_LOCALE_SLIST, ","},
    {WIN32_LOCALE_IMEASURE, "1"},
    {WIN32_LOCALE_SDECIMAL, "."},
    {WIN32_LOCALE_STHOUSAND, ","},
    {WIN32_LOCALE_SGROUPING, "3;0"},
    {WIN32_LOCALE_IDIGITS, "2"},
    {WIN32_LOCALE_ILZERO, "0"},
    {WIN32_LOCALE_SNATIVEDIGITS, "0123456789"},
    {WIN32_LOCALE_SCURRENCY, "$"},
    {WIN32_LOCALE_SINTLSYMBOL, "USD"},
    {WIN32_LOCALE_SMONDECIMALSEP, "."},
    {WIN32_LOCALE_SMONTHOUSANDSEP, ","},
    {WIN32_LOCALE_SMONGROUPING, "3;0"},
    {WIN32_LOCALE_ICURRDIGITS, "2"},
    {WIN32_LOCALE_IINTLCURRDIGITS, "2"},
    {WIN32_LOCALE_ICURRENCY, "0"},
    {WIN32_LOCALE_INEGCURR, "0"},
    {WIN32_LOCALE_SDATE, "/"},
    {WIN32_LOCALE_STIME, ":"},
    {WIN32_LOCALE_SSHORTDATE, "MM/dd/yy"},
    {WIN32_LOCALE_SLONGDATE, "MMMM dd, yyyy"},
    {WIN32_LOCALE_IDATE, "0"},
    {WIN32_LOCALE_ILDATE, "0"},
    {WIN32_LOCALE_ITIME, "0"},
    {WIN32_LOCALE_ICENTURY, "0"},
    {WIN32_LOCALE_ITLZERO, "0"},
    {WIN32_LOCALE_IDAYLZERO, "0"},
    {WIN32_LOCALE_IMONLZERO, "0"},
    {WIN32_LOCALE_S1159, "am"},
    {WIN32_LOCALE_S2359, "pm"},
    {WIN32_LOCALE_SDAYNAME1, "Monday"},
    {WIN32_LOCALE_SDAYNAME2, "Tuesday"},
    {WIN32_LOCALE_SDAYNAME3, "Wednesday"},
    {WIN32_LOCALE_SDAYNAME4, "Thursday"},
    {WIN32_LOCALE_SDAYNAME5, "Friday"},
    {WIN32_LOCALE_SDAYNAME6, "Saturday"},
    {WIN32_LOCALE_SDAYNAME7, "Sunday"},
    {WIN32_LOCALE_SABBREVDAYNAME1, "Mon"},
    {WIN32_LOCALE_SABBREVDAYNAME2, "Tue"},
    {WIN32_LOCALE_SABBREVDAYNAME3, "Wed"},
    {WIN32_LOCALE_SABBREVDAYNAME4, "Thu"},
    {WIN32_LOCALE_SABBREVDAYNAME5, "Fri"},
    {WIN32_LOCALE_SABBREVDAYNAME6, "Sat"},
    {WIN32_LOCALE_SABBREVDAYNAME7, "Sun"},
    {WIN32_LOCALE_SMONTHNAME1, "January"},
    {WIN32_LOCALE_SMONTHNAME2, "February"},
    {WIN32_LOCALE_SMONTHNAME3, "March"},
    {WIN32_LOCALE_SMONTHNAME4, "April"},
    {WIN32_LOCALE_SMONTHNAME5, "May"},
    {WIN32_LOCALE_SMONTHNAME6, "June"},
    {WIN32_LOCALE_SMONTHNAME7, "July"},
    {WIN32_LOCALE_SMONTHNAME8, "August"},
    {WIN32_LOCALE_SMONTHNAME9, "September"},
    {WIN32_LOCALE_SMONTHNAME10, "October"},
    {WIN32_LOCALE_SMONTHNAME11, "November"},
    {WIN32_LOCALE_SMONTHNAME12, "December"},
    {WIN32_LOCALE_SABBREVMONTHNAME1, "Jan"},
    {WIN32_LOCALE_SABBREVMONTHNAME2, "Feb"},
    {WIN32_LOCALE_SABBREVMONTHNAME3, "Mar"},
    {WIN32_LOCALE_SABBREVMONTHNAME4, "Apr"},
    {WIN32_LOCALE_SABBREVMONTHNAME5, "May"},
    {WIN32_LOCALE_SABBREVMONTHNAME6, "Jun"},
    {WIN32_LOCALE_SABBREVMONTHNAME7, "Jul"},
    {WIN32_LOCALE_SABBREVMONTHNAME8, "Aug"},
    {WIN32_LOCALE_SABBREVMONTHNAME9, "Sep"},
    {WIN32_LOCALE_SABBREVMONTHNAME10, "Oct"},
    {WIN32_LOCALE_SABBREVMONTHNAME11, "Nov"},
    {WIN32_LOCALE_SABBREVMONTHNAME12, "Dec"},
    {WIN32_LOCALE_SPOSITIVESIGN, ""},
    {WIN32_LOCALE_SNEGATIVESIGN, "-"},
    {WIN32_LOCALE_IPOSSIGNPOSN, "1"},
    {WIN32_LOCALE_INEGSIGNPOSN, "0"},
    {WIN32_LOCALE_IPOSSYMPRECEDES, "1"},
    {WIN32_LOCALE_IPOSSEPBYSPACE, "0"},
    {WIN32_LOCALE_INEGSYMPRECEDES, "1"},
    {WIN32_LOCALE_INEGSEPBYSPACE, "0"},
    {WIN32_LOCALE_SENGLANGUAGE, "English"},
    {WIN32_LOCALE_SENGCOUNTRY, "United States"},
    {WIN32_LOCALE_STIMEFORMAT, "hh:mm:ss tt"},
    {WIN32_LOCALE_IDEFAULTANSICODEPAGE, "1252"},
    {WIN32_LOCALE_ITIMEMARKPOSN, "0"},
    {WIN32_LOCALE_ICALENDARTYPE, "1"},
    {WIN32_LOCALE_IOPTIONALCALENDAR, "0"},
    {WIN32_LOCALE_IFIRSTDAYOFWEEK, "6"},
    {WIN32_LOCALE_IFIRSTWEEKOFYEAR, "0"},
    {WIN32_LOCALE_SMONTHNAME13, ""},
    {WIN32_LOCALE_SABBREVMONTHNAME13, ""},
    {WIN32_LOCALE_INEGNUMBER, "1"},
};

LONG GetLocaleInfo(ULONG locale, ULONG type, PSZ output, LONG capacity)
{
    TRACE_NATIVE("GetLocaleInfo(%X, %X, %p, %d)", locale, type, output, capacity);
    if (capacity < 0 || (capacity && !output) || (type & LOCALE_TYPE_RESERVED_BITS))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (locale && locale != WIN32_LOCALE_USER_DEFAULT && locale != WIN32_LOCALE_SYSTEM_DEFAULT &&
        locale != ENGLISH_US_LCID)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    type &= 0xFFFF;
    for (unsigned i = 0; i < sizeof locale_items / sizeof *locale_items; i++)
        if (locale_items[i].type == type)
        {
            size_t required = strlen(locale_items[i].value) + 1;
            if (!capacity)
                return (LONG)required;
            if ((size_t)capacity < required)
            {
                open32SetError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            memcpy(output, locale_items[i].value, required);
            return (LONG)required;
        }
    open32SetError(ERROR_INVALID_PARAMETER);
    return 0;
}

BOOL IsDBCSLeadByte(ULONG character)
{
    typedef APIRET(OS2API * QueryFn)(ULONG, PCOUNTRYCODE, PCHAR);
    QueryFn query = (QueryFn)open32DosService("DosQueryDBCSEnv");
    COUNTRYCODE country = {0, 0};
    unsigned char ranges[DBCS_RANGE_BYTES] = {0};
    ULONG error =
        query ? query(sizeof ranges, &country, (PCHAR)ranges) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return FALSE;
    }
    character &= 255;
    for (unsigned i = 0; i + 1 < sizeof ranges && ranges[i]; i += 2)
        if (character >= ranges[i] && character <= ranges[i + 1])
            return TRUE;
    return FALSE;
}

USHORT GetUserDefaultLangID(void)
{
    return (USHORT)GetUserDefaultLCID();
}

BOOL GetStringTypeEx(ULONG locale, ULONG kind, PSZ input, LONG count, PVOID output)
{
    (void)locale; /* Input is single-byte text, and its classes do not vary by locale here. */
    if (!input || !output || count == 0 || count < -1 || count > STRING_TYPE_INPUT_LIMIT ||
        (kind != WIN32_CT_CTYPE1 && kind != WIN32_CT_CTYPE2 && kind != WIN32_CT_CTYPE3))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (count == -1)
        count = (LONG)strlen(input) + 1;
    uint8 *wide = malloc((size_t)count * UTF16_UNIT_BYTES);
    if (!wide)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (!MultiByteToWideChar(WIN32_CP_ACP, 0, input, count, wide, count))
    {
        free(wide);
        return FALSE;
    }
    for (LONG i = 0; i < count; i++)
    {
        /* Latin-1 letters plus the letters CP1252 adds in 0x80-0x9F. */
        ULONG c = lx_guest_load_u16(wide + i * UTF16_UNIT_BYTES);
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
        if (kind == WIN32_CT_CTYPE1)
            value =
                (upper ? WIN32_C1_UPPER : 0) | (lower ? WIN32_C1_LOWER : 0) |
                (digit ? WIN32_C1_DIGIT : 0) | (space ? WIN32_C1_SPACE : 0) |
                (!alpha && !digit && !space && !control && defined ? WIN32_C1_PUNCT : 0) |
                (control ? WIN32_C1_CNTRL : 0) | (blank ? WIN32_C1_BLANK : 0) |
                (digit || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') ? WIN32_C1_XDIGIT : 0) |
                (alpha ? WIN32_C1_ALPHA : 0) | (defined ? WIN32_C1_DEFINED : 0);
        else if (kind == WIN32_CT_CTYPE2)
            value = alpha                                               ? WIN32_C2_LEFTTORIGHT
                    : digit                                             ? WIN32_C2_EUROPENUMBER
                    : c == '\r' || c == '\n'                            ? WIN32_C2_BLOCKSEPARATOR
                    : c == '\t'                                         ? WIN32_C2_SEGMENTSEPARATOR
                    : space                                             ? WIN32_C2_WHITESPACE
                    : c == '+' || c == '-'                              ? WIN32_C2_EUROPESEPARATOR
                    : c == '$' || c == 0xA3 || c == 0xA5 || c == 0x20AC ? WIN32_C2_EUROPETERMINATOR
                    : c == ',' || c == '.' || c == ':'                  ? WIN32_C2_COMMONSEPARATOR
                                                                        : WIN32_C2_OTHERNEUTRAL;
        else
            value = (alpha ? WIN32_C3_ALPHA : 0) |
                    (c == 0xA8 || c == 0xAF || c == 0xB4 || c == 0xB8 || c == 0x2C6 || c == 0x2DC
                         ? WIN32_C3_DIACRITIC
                         : 0) |
                    (!alpha && !digit && !space && !control ? WIN32_C3_SYMBOL : 0);
        lx_guest_store_u16((uint8 *)output + i * UTF16_UNIT_BYTES, value);
    }
    free(wide);
    return TRUE;
}

ULONG GetUserDefaultLCID(void)
{
    /* The current NLS provider exposes United States country and English locale. */
    return ENGLISH_US_LCID;
}

ULONG GetOEMCP(void)
{
    /* The NLS provider currently exposes CP437 as the process OEM code page. */
    return OEM_CODE_PAGE;
}

ULONG GetACP(void)
{
    /* Windows code-page identifier for the ANSI encoding exposed by GetLocaleInfo. */
    return ANSI_CODE_PAGE;
}
