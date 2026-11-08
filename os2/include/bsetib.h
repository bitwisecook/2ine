/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_BSETIB_H_
#define _INCL_BSETIB_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Thread and process information blocks */

#pragma pack(push, 1)

    typedef struct TIB2
    {
        ULONG tib2_ultid;
        ULONG tib2_ulpri;
        ULONG tib2_version;
        USHORT tib2_usMCCount;
        USHORT tib2_fMCForceFlag;
    } TIB2, *PTIB2;

    typedef struct TIB
    {
        PVOID tib_pexchain;
        PVOID tib_pstack;
        PVOID tib_pstacklimit;
        PTIB2 tib_ptib2;
        ULONG tib_version;
        ULONG tib_ordinal;
    } TIB, *PTIB;

#pragma pack(pop)

    typedef struct PIB
    {
        ULONG pib_ulpid;
        ULONG pib_ulppid;
        HMODULE pib_hmte;
        PCHAR pib_pchcmd;
        PCHAR pib_pchenv;
        ULONG pib_flstatus;
        ULONG pib_ultype;
    } PIB, *PPIB;

#ifdef __cplusplus
}
#endif

#endif
