/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* DosQuerySysInfo. */
#include "os2/baseos/doscalls/internal.h"

/* Values 2ine reports for limits it does not impose. */
enum
{
    UNLIMITED_SESSIONS = 999999,
    /* Drive 3 is C:. */
    BOOT_DRIVE_C = 3,
    /* Scheduler timings, in milliseconds. */
    MIN_TIMESLICE_MS = 1,
    MAX_TIMESLICE_MS = 10,
    TIMER_INTERVAL_TENTHS_MS = 10,
    /* There is one session and one process in front. */
    FOREGROUND_SESSION_ID = 1,
    FOREGROUND_PROCESS_ID = 1
};

static const uint32 LARGEST_ALLOCATION = 0x7FFFFFFF;

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
                *(buf++) = UNLIMITED_SESSIONS;
                break;
            case QSV_MAX_PM_SESSIONS:
                *(buf++) = UNLIMITED_SESSIONS;
                break;
            case QSV_MAX_VDM_SESSIONS:
                *(buf++) = 0;
                break;
            case QSV_BOOT_DRIVE:
                *(buf++) = BOOT_DRIVE_C;
                break;
            case QSV_DYN_PRI_VARIATION:
                *(buf++) = 1;
                break;
            case QSV_MAX_WAIT:
                *(buf++) = 1;
                break;
            case QSV_MIN_SLICE:
                *(buf++) = MIN_TIMESLICE_MS;
                break;
            case QSV_MAX_SLICE:
                *(buf++) = MAX_TIMESLICE_MS;
                break;
            case QSV_PAGE_SIZE:
                *(buf++) = GUEST_PAGE_SIZE;
                break;

            case QSV_VERSION_MAJOR:
                *(buf++) = OS2_VERSION_MAJOR;
                break;
            case QSV_VERSION_MINOR:
                *(buf++) = OS2_VERSION_MINOR;
                break;
            case QSV_VERSION_REVISION:
                *(buf++) = OS2_VERSION_REVISION;
                break;

            case QSV_MS_COUNT:
                *(buf++) = (uint32)(lx_host_monotonic_us() / US_PER_MS);
                break;

            case QSV_TIME_LOW:
                *(buf++) = (uint32)(lx_host_realtime_us() / US_PER_SECOND);
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
                *(buf++) = LARGEST_ALLOCATION;
                break;
            case QSV_MAXSHMEM:
                *(buf++) = LARGEST_ALLOCATION;
                break;

            case QSV_TIMER_INTERVAL:
                *(buf++) = TIMER_INTERVAL_TENTHS_MS;
                break;
            case QSV_MAX_COMP_LENGTH:
                *(buf++) = NAME_MAX;
                break;
            case QSV_FOREGROUND_FS_SESSION:
                *(buf++) = FOREGROUND_SESSION_ID;
                break;
            case QSV_FOREGROUND_PROCESS:
                *(buf++) = FOREGROUND_PROCESS_ID;
                break;

            default:
                return ERROR_INVALID_PARAMETER;
        }
    }

    return NO_ERROR;
}
