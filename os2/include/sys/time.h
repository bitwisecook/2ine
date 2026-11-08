/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Time of day. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_TIME_H_
#define _INCL_SYS_TIME_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Time of day */

#pragma pack(push, 1)

    typedef struct OS2_timeval
    {
        int32_t tv_sec;
        int32_t tv_usec;
    } OS2_timeval;

    typedef struct OS2_timezone
    {
        int32_t tz_minuteswest;
        int32_t tz_dsttime;
    } OS2_timezone;

#pragma pack(pop)

    OS2EXPORT int OS2API OS2_gettimeofday(OS2_timeval *os2tv, OS2_timezone *os2tz);

#ifdef __cplusplus
}
#endif

#endif
