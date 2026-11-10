/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMDEV_H_
#define _INCL_PMDEV_H_

#include "os2/include/pmgpi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_DEV */
    /* DevOpenDC types */
    enum
    {
        OD_QUEUED = 2,
        OD_INFO = 6
    };

    /* Device escapes */
    enum
    {
        DEVESC_STARTDOC = 8150,
        DEVESC_ENDDOC = 8151,
        DEVESC_ABORTDOC = 8153,
        DEVESC_NEWFRAME = 16300,
        DEVESC_RAWDATA = 16303
    };

    OS2EXPORT BOOL OS2API DevQueryCaps(HDC dc, LONG start, LONG count, PLONG output);

    OS2EXPORT LONG OS2API DevEscape(HDC dc, LONG code, LONG input_count, PVOID input,
                                    PLONG output_count, PVOID output);

    OS2EXPORT LONG OS2API DevPostDeviceModes(HAB hab, PVOID data, PSZ driver, PSZ device, PSZ name,
                                             ULONG options);

    OS2EXPORT BOOL OS2API DevQueryDeviceNames(HAB hab, PSZ driver, PLONG devices, PVOID names,
                                              PVOID descriptions, PLONG types, PVOID data_types);

    OS2EXPORT LONG OS2API DevQueryHardcopyCaps(HDC dc, LONG first, LONG count, PVOID output);

    OS2EXPORT HDC OS2API DevOpenDC(HAB hab, LONG type, PSZ token, LONG count, PVOID data,
                                   HDC compatible);

    OS2EXPORT ULONG OS2API DevCloseDC(HDC dc);

#ifdef __cplusplus
}
#endif

#endif
