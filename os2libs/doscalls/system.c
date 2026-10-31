/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"

APIRET DosGetInfoBlocks(PTIB *pptib, PPIB *pppib)
{
    TRACE_NATIVE("DosGetInfoBlocks(%p, %p)", pptib, pppib);

    if (pptib != NULL)
        *pptib = (PTIB)LX_GETTIB();

    if (pppib != NULL)
        *pppib = GLoaderState.guest_pib ? lx_execution_mapping_host(GLoaderState.guest_pib)
                                        : (PPIB)&GLoaderState.pib;

    return 0;
}

APIRET DosQuerySysInfo(ULONG first, ULONG last, PVOID _buf, ULONG buflen)
{
    TRACE_NATIVE("DosQuerySysInfo(%u, %u, %p, %u)", (uint)first, (uint)last, _buf, (uint)buflen);

    uint32 *buf = (uint32 *)_buf;
    if (last < first)
        return ERROR_INVALID_PARAMETER;
    if ((buflen / sizeof(uint32)) < ((last - first) + 1))
        return ERROR_BUFFER_OVERFLOW;
    for (uint32 varid = first; varid <= last; varid++)
    {
        switch (varid)
        {
            case QSV_MAX_PATH_LENGTH:
                *(buf++) = PATH_MAX;
                break;
            case QSV_MAX_TEXT_SESSIONS:
                *(buf++) = 999999;
                break;
            case QSV_MAX_PM_SESSIONS:
                *(buf++) = 999999;
                break;
            case QSV_MAX_VDM_SESSIONS:
                *(buf++) = 0;
                break;
            case QSV_BOOT_DRIVE:
                *(buf++) = 3;
                break; // "C:"
            case QSV_DYN_PRI_VARIATION:
                *(buf++) = 1;
                break;
            case QSV_MAX_WAIT:
                *(buf++) = 1;
                break;
            case QSV_MIN_SLICE:
                *(buf++) = 1;
                break;
            case QSV_MAX_SLICE:
                *(buf++) = 10;
                break;
            case QSV_PAGE_SIZE:
                *(buf++) = 4096;
                break;

            case QSV_VERSION_MAJOR:
                *(buf++) = 20;
                break; // OS/2 Warp 4.5
            case QSV_VERSION_MINOR:
                *(buf++) = 45;
                break; // OS/2 Warp 4.5
            case QSV_VERSION_REVISION:
                *(buf++) = 0;
                break; // OS/2 Warp 4.5

            case QSV_MS_COUNT:
                *(buf++) = (uint32)(lx_host_monotonic_us() / 1000);
                break;

            case QSV_TIME_LOW:
                *(buf++) = (uint32)(lx_host_realtime_us() / 1000000);
                break;
            case QSV_TIME_HIGH:
                *(buf++) = 0;
                break;

            case QSV_TOTPHYSMEM:
            case QSV_TOTRESMEM:
            case QSV_TOTAVAILMEM:
            {
                uint64 total = 0, available = 0;
                lx_host_system_memory(&total, &available);
                uint64 value = (varid == QSV_TOTPHYSMEM)    ? total
                               : (varid == QSV_TOTAVAILMEM) ? available
                                                            : total - available;
                *(buf++) = value > UINT32_MAX ? UINT32_MAX : (uint32)value;
                break;
            }

            case QSV_MAXPRMEM:
                *(buf++) = 0x7FFFFFFF;
                break;
            case QSV_MAXSHMEM:
                *(buf++) = 0x7FFFFFFF;
                break;

            case QSV_TIMER_INTERVAL:
                *(buf++) = 10;
                break;
            case QSV_MAX_COMP_LENGTH:
                *(buf++) = NAME_MAX;
                break;
            case QSV_FOREGROUND_FS_SESSION:
                *(buf++) = 1;
                break;
            case QSV_FOREGROUND_PROCESS:
                *(buf++) = 1;
                break;

            default:
                return ERROR_INVALID_PARAMETER;
        }
    }

    return NO_ERROR;
}

APIRET DosGetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("DosGetDateTime(%p)", pdt);

    const uint64 now = lx_host_realtime_us();
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

APIRET DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP)
{
    TRACE_NATIVE("DosQueryCp(%u, %p, %p)", (uint)cb, arCP, pcCP);

    FIXME("This is mostly a stub");

    if (cb < sizeof(*arCP) * 2)
        return ERROR_CPLIST_TOO_SMALL;

    // 437 == United States codepage.
    arCP[0] = 437; // current process codepage
    arCP[1] = 437; // prepared system codepage

    if (pcCP)
        *pcCP = sizeof(*arCP) * 2;

    return NO_ERROR;
}

APIRET DosDevConfig(PVOID pdevinfo, ULONG item)
{
    TRACE_NATIVE("DosDevConfig(%p, %u)", pdevinfo, (uint)item);

    if (pdevinfo == NULL)
        return ERROR_INVALID_PARAMETER;

    BYTE *info = (BYTE *)pdevinfo;
    switch (item)
    {
        case DEVINFO_PRINTER:
            *info = 0;
            break; // we don't report any printers.
        case DEVINFO_RS232:
            *info = 0;
            break; // we don't report any serial ports.
        case DEVINFO_FLOPPY:
            *info = 0;
            break; // we don't report any floppies.
        case DEVINFO_COPROCESSOR:
            *info = 1;
            break;
        case DEVINFO_SUBMODEL:
            *info = 1;
            break;
        case DEVINFO_MODEL:
            *info = 252;
            break;
        case DEVINFO_ADAPTER:
            *info = 1;
            break;
        default:
            return ERROR_INVALID_PARAMETER;
    }

    return NO_ERROR;
}

APIRET DosQuerySysState(ULONG func, ULONG arg1, ULONG pid, ULONG _res_, PVOID buf, ULONG bufsz)
{
    TRACE_NATIVE("DosQuerySysState(%u, %u, %u, %u, %p, %u)", (uint)func, (uint)arg1, (uint)pid,
                 (uint)_res_, buf, (uint)bufsz);
    FIXME("implement me");
    return ERROR_INVALID_PARAMETER;
}

APIRET16 Dos16GetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("Dos16GetDateTime(%p)", pdt);
    return (APIRET16)DosGetDateTime(pdt);
}

APIRET16 Dos16GetDBCSEv(USHORT cb, PVOID pcc, PVOID buf)
{
    TRACE_NATIVE("Dos16GetDBCSEv(%u, %p, %p)", (uint)cb, pcc, buf);
    (void)pcc;
    if (buf && cb)
        memset(buf, 0, cb);
    return NO_ERROR;
}

APIRET16 Dos16DevConfig(PVOID pdevinfo, USHORT item, USHORT reserved)
{
    TRACE_NATIVE("Dos16DevConfig(%p, %u, %u)", pdevinfo, (uint)item, (uint)reserved);
    return (APIRET16)DosDevConfig(pdevinfo, (ULONG)item);
}

APIRET16 Dos16GetVersion(PUSHORT pver)
{
    TRACE_NATIVE("Dos16GetVersion(%p)", pver);
    uint8 *bytes = (uint8 *)pver;
    bytes[0] = 45; // OS/2 Warp 4.52 reports this.
    bytes[1] = 20;
    return NO_ERROR;
}

APIRET16 Dos16GetMachineMode(PBYTE pmode)
{
    TRACE_NATIVE("Dos16GetMachineMode(%p)", pmode);
    *pmode = 1; // 1==OS/2 mode (as opposed to zero for DOS mode).
    return NO_ERROR;
}

APIRET16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP)
{
    TRACE_NATIVE("Dos16GetCp(%u, %p, %p)", (uint)cb, arCP, pcCP);

    FIXME("This is mostly a stub");
    FIXME("...and mostly copy/paste from DosQueryCp()...");

    if (cb < sizeof(*arCP) * 2)
        return ERROR_CPLIST_TOO_SMALL;

    // 437 == United States codepage.
    arCP[0] = 437; // current process codepage
    arCP[1] = 437; // prepared system codepage

    if (pcCP)
        *pcCP = sizeof(*arCP) * 2;

    return NO_ERROR;
}

APIRET16 Dos16GetInfoSeg(PSEL globalseg, PSEL localseg)
{
    TRACE_NATIVE("Dos16GetInfoSeg(%p, %p)", globalseg, localseg);
    if (getenv("TWOINE_TRACE_SEGMENTS"))
        fprintf(stderr, "2INE InfoSeg: ginfosel=%u(%04X) linfosel=%u(%04X) -> %04X/%04X\n",
                (uint)ginfosel, (uint)(ginfosel << 3 | 7), (uint)linfosel,
                (uint)(linfosel << 3 | 7), (uint)(ginfosel << 3 | 7), (uint)(linfosel << 3 | 7));
    if (globalseg)
        *globalseg = ginfosel << 3 | 7;
    if (localseg)
        *localseg = linfosel << 3 | 7;
    return NO_ERROR;
}

APIRET DosQueryDBCSEnv(ULONG buflen, PCOUNTRYCODE pcc, PCHAR buf)
{
    TRACE_NATIVE("DosQueryDBCSEnv(%u, %p, %p)", (uint)buflen, pcc, buf);
    if ((!pcc) || (!buf))
        return ERROR_INVALID_PARAMETER;
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;
    memset(buf, '\0', buflen);
    return NO_ERROR;
}
