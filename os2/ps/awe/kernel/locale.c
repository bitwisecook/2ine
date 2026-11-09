/* Public domain. No rights reserved. Wholly AI-authored. */
/* The current NLS provider supplies the US English ANSI locale. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/bsedos.h"

static const struct
{
    ULONG type;
    const char *value;
} locale_items[] = {
    {0x0001, "0409"},          /* LOCALE_ILANGUAGE */
    {0x0002, "English"},       /* LOCALE_SLOCALIZEDDISPLAYNAME */
    {0x0003, "ENU"},           /* LOCALE_SABBREVLANGNAME */
    {0x0004, "English"},       /* LOCALE_SNATIVELANGUAGENAME */
    {0x0005, "1"},             /* LOCALE_ICOUNTRY */
    {0x0006, "United States"}, /* LOCALE_SLOCALIZEDCOUNTRYNAME */
    {0x0007, "US"},            /* LOCALE_SABBREVCTRYNAME */
    {0x0008, "United States"}, /* LOCALE_SNATIVECOUNTRYNAME */
    {0x0009, "409"},           /* LOCALE_IDEFAULTLANGUAGE */
    {0x000A, "1"},             /* LOCALE_IDEFAULTCOUNTRY */
    {0x000B, "437"},           /* LOCALE_IDEFAULTCODEPAGE */
    {0x000C, ","},             /* LOCALE_SLIST */
    {0x000D, "1"},             /* LOCALE_IMEASURE */
    {0x000E, "."},             /* LOCALE_SDECIMAL */
    {0x000F, ","},             /* LOCALE_STHOUSAND */
    {0x0010, "3;0"},           /* LOCALE_SGROUPING */
    {0x0011, "2"},             /* LOCALE_IDIGITS */
    {0x0012, "0"},             /* LOCALE_ILZERO */
    {0x0013, "0123456789"},    /* LOCALE_SNATIVEDIGITS */
    {0x0014, "$"},             /* LOCALE_SCURRENCY */
    {0x0015, "USD"},           /* LOCALE_SINTLSYMBOL */
    {0x0016, "."},             /* LOCALE_SMONDECIMALSEP */
    {0x0017, ","},             /* LOCALE_SMONTHOUSANDSEP */
    {0x0018, "3;0"},           /* LOCALE_SMONGROUPING */
    {0x0019, "2"},             /* LOCALE_ICURRDIGITS */
    {0x001A, "2"},             /* LOCALE_IINTLCURRDIGITS */
    {0x001B, "0"},             /* LOCALE_ICURRENCY */
    {0x001C, "0"},             /* LOCALE_INEGCURR */
    {0x001D, "/"},             /* LOCALE_SDATE */
    {0x001E, ":"},             /* LOCALE_STIME */
    {0x001F, "MM/dd/yy"},      /* LOCALE_SSHORTDATE */
    {0x0020, "MMMM dd, yyyy"}, /* LOCALE_SLONGDATE */
    {0x0021, "0"},             /* LOCALE_IDATE */
    {0x0022, "0"},             /* LOCALE_ILDATE */
    {0x0023, "0"},             /* LOCALE_ITIME */
    {0x0024, "0"},             /* LOCALE_ICENTURY */
    {0x0025, "0"},             /* LOCALE_ITLZERO */
    {0x0026, "0"},             /* LOCALE_IDAYLZERO */
    {0x0027, "0"},             /* LOCALE_IMONLZERO */
    {0x0028, "am"},            /* LOCALE_S1159 */
    {0x0029, "pm"},            /* LOCALE_S2359 */
    {0x002A, "Monday"},        /* LOCALE_SDAYNAME1 */
    {0x002B, "Tuesday"},       /* LOCALE_SDAYNAME2 */
    {0x002C, "Wednesday"},     /* LOCALE_SDAYNAME3 */
    {0x002D, "Thursday"},      /* LOCALE_SDAYNAME4 */
    {0x002E, "Friday"},        /* LOCALE_SDAYNAME5 */
    {0x002F, "Saturday"},      /* LOCALE_SDAYNAME6 */
    {0x0030, "Sunday"},        /* LOCALE_SDAYNAME7 */
    {0x0031, "Mon"},           /* LOCALE_SABBREVDAYNAME1 */
    {0x0032, "Tue"},           /* LOCALE_SABBREVDAYNAME2 */
    {0x0033, "Wed"},           /* LOCALE_SABBREVDAYNAME3 */
    {0x0034, "Thu"},           /* LOCALE_SABBREVDAYNAME4 */
    {0x0035, "Fri"},           /* LOCALE_SABBREVDAYNAME5 */
    {0x0036, "Sat"},           /* LOCALE_SABBREVDAYNAME6 */
    {0x0037, "Sun"},           /* LOCALE_SABBREVDAYNAME7 */
    {0x0038, "January"},       /* LOCALE_SMONTHNAME1 */
    {0x0039, "February"},      /* LOCALE_SMONTHNAME2 */
    {0x003A, "March"},         /* LOCALE_SMONTHNAME3 */
    {0x003B, "April"},         /* LOCALE_SMONTHNAME4 */
    {0x003C, "May"},           /* LOCALE_SMONTHNAME5 */
    {0x003D, "June"},          /* LOCALE_SMONTHNAME6 */
    {0x003E, "July"},          /* LOCALE_SMONTHNAME7 */
    {0x003F, "August"},        /* LOCALE_SMONTHNAME8 */
    {0x0040, "September"},     /* LOCALE_SMONTHNAME9 */
    {0x0041, "October"},       /* LOCALE_SMONTHNAME10 */
    {0x0042, "November"},      /* LOCALE_SMONTHNAME11 */
    {0x0043, "December"},      /* LOCALE_SMONTHNAME12 */
    {0x0044, "Jan"},           /* LOCALE_SABBREVMONTHNAME1 */
    {0x0045, "Feb"},           /* LOCALE_SABBREVMONTHNAME2 */
    {0x0046, "Mar"},           /* LOCALE_SABBREVMONTHNAME3 */
    {0x0047, "Apr"},           /* LOCALE_SABBREVMONTHNAME4 */
    {0x0048, "May"},           /* LOCALE_SABBREVMONTHNAME5 */
    {0x0049, "Jun"},           /* LOCALE_SABBREVMONTHNAME6 */
    {0x004A, "Jul"},           /* LOCALE_SABBREVMONTHNAME7 */
    {0x004B, "Aug"},           /* LOCALE_SABBREVMONTHNAME8 */
    {0x004C, "Sep"},           /* LOCALE_SABBREVMONTHNAME9 */
    {0x004D, "Oct"},           /* LOCALE_SABBREVMONTHNAME10 */
    {0x004E, "Nov"},           /* LOCALE_SABBREVMONTHNAME11 */
    {0x004F, "Dec"},           /* LOCALE_SABBREVMONTHNAME12 */
    {0x0050, ""},              /* LOCALE_SPOSITIVESIGN */
    {0x0051, "-"},             /* LOCALE_SNEGATIVESIGN */
    {0x0052, "1"},             /* LOCALE_IPOSSIGNPOSN */
    {0x0053, "0"},             /* LOCALE_INEGSIGNPOSN */
    {0x0054, "1"},             /* LOCALE_IPOSSYMPRECEDES */
    {0x0055, "0"},             /* LOCALE_IPOSSEPBYSPACE */
    {0x0056, "1"},             /* LOCALE_INEGSYMPRECEDES */
    {0x0057, "0"},             /* LOCALE_INEGSEPBYSPACE */
    {0x1001, "English"},       /* LOCALE_SENGLISHLANGUAGENAME */
    {0x1002, "United States"}, /* LOCALE_SENGLISHCOUNTRYNAME */
    {0x1003, "hh:mm:ss tt"},   /* LOCALE_STIMEFORMAT */
    {0x1004, "1252"},          /* LOCALE_IDEFAULTANSICODEPAGE */
    {0x1005, "0"},             /* LOCALE_ITIMEMARKPOSN */
    {0x1009, "1"},             /* LOCALE_ICALENDARTYPE */
    {0x100B, "0"},             /* LOCALE_IOPTIONALCALENDAR */
    {0x100C, "6"},             /* LOCALE_IFIRSTDAYOFWEEK */
    {0x100D, "0"},             /* LOCALE_IFIRSTWEEKOFYEAR */
    {0x100E, ""},              /* LOCALE_SMONTHNAME13 */
    {0x100F, ""},              /* LOCALE_SABBREVMONTHNAME13 */
    {0x1010, "1"},             /* LOCALE_INEGNUMBER */
};

LONG GetLocaleInfo(ULONG locale, ULONG type, PSZ output, LONG capacity)
{
    TRACE_NATIVE("GetLocaleInfo(%X, %X, %p, %d)", locale, type, output, capacity);
    if (capacity < 0 || (capacity && !output) || (type & 0x3FFF0000))
    {
        open32SetError(87);
        return 0;
    }
    if (locale && locale != 0x400 && locale != 0x800 && locale != 0x409)
    {
        open32SetError(87);
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
                open32SetError(122);
                return 0;
            }
            memcpy(output, locale_items[i].value, required);
            return (LONG)required;
        }
    open32SetError(87);
    return 0;
}

BOOL IsDBCSLeadByte(ULONG character)
{
    typedef APIRET(OS2API * QueryFn)(ULONG, PCOUNTRYCODE, PCHAR);
    QueryFn query = (QueryFn)open32DosService("DosQueryDBCSEnv");
    COUNTRYCODE country = {0, 0};
    unsigned char ranges[12] = {0};
    ULONG error = query ? query(sizeof ranges, &country, (PCHAR)ranges) : 120;
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

ULONG GetUserDefaultLCID(void)
{
    /* The current NLS provider exposes United States country and English locale. */
    return 0x0409;
}

ULONG GetOEMCP(void)
{
    /* The NLS provider currently exposes CP437 as the process OEM code page. */
    return 437;
}

ULONG GetACP(void)
{
    /* Windows code-page identifier for the ANSI encoding exposed by GetLocaleInfo. */
    return 1252;
}
