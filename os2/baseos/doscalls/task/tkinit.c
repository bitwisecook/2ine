/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

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
