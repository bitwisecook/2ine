/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_KBDCALLS_H_
#define _INCL_KBDCALLS_H_

#include "os2types.h"

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

    OS2EXPORT APIRET16 OS2API16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd)
        OS2APIINFO(ord = 4, name = KBDCHARIN);
    OS2EXPORT APIRET16 OS2API16 KbdStringIn(PCHAR pch, PSTRINGINBUF pchin, USHORT flag, HKBD hkbd)
        OS2APIINFO(ord = 9, name = KBDSTRINGIN);
    OS2EXPORT APIRET16 OS2API16 KbdSetCp(USHORT reserved, USHORT codepage, HKBD hkbd)
        OS2APIINFO(ord = 5, name = KBDSETCP);
    OS2EXPORT APIRET16 OS2API16 KbdGetCp(ULONG reserved, PUSHORT codepage, HKBD hkbd)
        OS2APIINFO(ord = 3, name = KBDGETCP);
    OS2EXPORT APIRET16 OS2API16 KbdFlushBuffer(HKBD hkbd)
        OS2APIINFO(ord = 13, name = KBDFLUSHBUFFER);
    OS2EXPORT APIRET16 OS2API16 KbdPeek(PKBDKEYINFO pkbci, HKBD hkbd)
        OS2APIINFO(ord = 22, name = KBDPEEK);
    OS2EXPORT APIRET16 OS2API16 KbdGetStatus(PKBDINFO pkbci, HKBD hkbd)
        OS2APIINFO(ord = 10, name = KBDGETSTATUS);
    OS2EXPORT APIRET16 OS2API16 KbdSetStatus(PKBDINFO pkbci, HKBD hkbd)
        OS2APIINFO(ord = 11, name = KBDSETSTATUS);

#ifdef __cplusplus
}
#endif

#endif
