/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_KBDCALLS_H_
#define _INCL_KBDCALLS_H_

#include "os2/include/os2types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#pragma pack(push, 2)

    typedef struct
    {
        UCHAR chChar;
        UCHAR chScan;
        UCHAR fbStatus;
        UCHAR bNlsShift;
        USHORT fsState;
        ULONG time;
    } KBDKEYINFO, *PKBDKEYINFO;

    typedef struct
    {
        USHORT cb;
        USHORT cchIn;
    } STRINGINBUF, *PSTRINGINBUF;

    typedef struct
    {
        USHORT cb;
        USHORT fsMask;
        USHORT chTurnAround;
        USHORT fsInterim;
        USHORT fsState;
    } KBDINFO, *PKBDINFO;

#pragma pack(pop)

    OS2EXPORT APIRET16 OS2API16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdStringIn(PCHAR pch, PSTRINGINBUF pchin, USHORT flag, HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdSetCp(USHORT reserved, USHORT codepage, HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdGetCp(ULONG reserved, PUSHORT codepage, HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdFlushBuffer(HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdPeek(PKBDKEYINFO pkbci, HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdGetStatus(PKBDINFO pkbci, HKBD hkbd);
    OS2EXPORT APIRET16 OS2API16 KbdSetStatus(PKBDINFO pkbci, HKBD hkbd);

#ifdef __cplusplus
}
#endif

#endif
