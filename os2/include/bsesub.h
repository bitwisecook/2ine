/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_BSESUB_H_
#define _INCL_BSESUB_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_KBD: keyboard */

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

    /* INCL_VIO: video */

#pragma pack(push, 1)

    typedef struct
    {
        USHORT cb;
        UCHAR fbType;
        UCHAR color;
        USHORT col;
        USHORT row;
        USHORT hres;
        USHORT vres;
        UCHAR fmt_ID;
        UCHAR attrib;
        ULONG buf_addr;
        ULONG buf_length;
        ULONG full_length;
        ULONG partial_length;
        ULONG ext_data_addr;
    } VIOMODEINFO, *PVIOMODEINFO;

#pragma pack(pop)

#pragma pack(push, 2)

    typedef struct
    {
        USHORT cb;
        USHORT adapter;
        USHORT display;
        ULONG cbMemory;
        USHORT Configuration;
        USHORT VDHVersion;
        USHORT Flags;
        ULONG HWBufferSize;
        ULONG FullSaveSize;
        ULONG PartSaveSize;
        USHORT EMAdaptersOFF;
        USHORT EMDisplaysOFF;
    } VIOCONFIGINFO, *PVIOCONFIGINFO;

#pragma pack(pop)

    enum
    {
        VGMT_OTHER = 0x1,
        VGMT_GRAPHICS = 0x02,
        VGMT_DISABLEBURST = 0x04
    };

    typedef struct
    {
        USHORT yStart;
        USHORT cEnd;
        USHORT cx;
        USHORT attr;
    } VIOCURSORINFO, *PVIOCURSORINFO;

    OS2EXPORT APIRET16 OS2API16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioSetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioGetAnsi(PUSHORT enabled, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioGetConfig(USHORT config, PVIOCONFIGINFO info, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioWrtTty(PCH pch, USHORT cb, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioSetCp(USHORT reserved, USHORT codepage, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioGetCp(USHORT reserved, PUSHORT codepage, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioGetCurPos(PUSHORT pusRow, PUSHORT pusColumn, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioGetBuf(PULONG pLVB, PUSHORT pcbLVB, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioGetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioScrollUp(USHORT usTopRow, USHORT usLeftCol, USHORT usBotRow,
                                            USHORT usRightCol, USHORT cbLines, PBYTE pCell,
                                            HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioSetCurPos(USHORT usRow, USHORT usColumn, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioSetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioReadCellStr(PCH pchCellStr, PUSHORT pcb, USHORT usRow,
                                               USHORT usColumn, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioReadCharStr(PCH pch, PUSHORT pcb, USHORT row, USHORT column,
                                               HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioScrollDn(USHORT top, USHORT left, USHORT bottom, USHORT right,
                                            USHORT lines, PBYTE cell, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioWrtCellStr(PCH pchCellStr, USHORT cb, USHORT usRow,
                                              USHORT usColumn, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioWrtCharStrAtt(PCH pch, USHORT cb, USHORT usRow, USHORT usColumn,
                                                 PBYTE pAttr, HVIO hvio);

    OS2EXPORT APIRET16 OS2API16 VioWrtNCell(PBYTE pCell, USHORT cb, USHORT usRow, USHORT usColumn,
                                            HVIO hvio);

#ifdef __cplusplus
}
#endif

#endif
