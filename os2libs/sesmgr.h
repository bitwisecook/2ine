/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_SESMGR_H_
#define _INCL_SESMGR_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        USHORT Length;
        USHORT Related;
        USHORT FgBg;
        USHORT TraceOpt;
        PSZ PgmTitle;
        PSZ PgmName;
        PBYTE PgmInputs;
        PBYTE TermQ;
        PBYTE Environment;
        USHORT InheritOpt;
        USHORT SessionType;
        PSZ IconFile;
        ULONG PgmHandle;
        USHORT PgmControl;
        USHORT InitXPos;
        USHORT InitYPos;
        USHORT InitXSize;
        USHORT InitYSize;
        USHORT Reserved;
        PSZ ObjectBuffer;
        ULONG ObjectBuffLen;
    } STARTDATA, *PSTARTDATA;
    typedef struct
    {
        USHORT Length, SelectInd, BondInd;
    } STATUSDATA, *PSTATUSDATA;

    OS2EXPORT APIRET OS2API DosStartSession(PSTARTDATA psd, PULONG pidSession, PPID ppid)
        OS2APIINFO(ord = 37);
    OS2EXPORT APIRET OS2API DosSelectSession(ULONG session) OS2APIINFO(38);
    OS2EXPORT APIRET OS2API DosSetSession(ULONG session, PSTATUSDATA status) OS2APIINFO(39);
    OS2EXPORT APIRET16 OS2API16 Dos16SMSetTitle(PCHAR title)
        OS2APIINFO(ord = 5, name = DOSSMSETTITLE);

#ifdef __cplusplus
}
#endif

#endif
