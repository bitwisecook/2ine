/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_OS2TYPES_H_
#define _INCL_OS2TYPES_H_

#include <stdint.h>
#include "hal/abi.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* lxapigen.py reads OS2APIINFO; the C compiler ignores it. */
#define OS2APIINFO(...)

#ifndef APIENTRY
#define APIENTRY LX_HOST_API_ENTRY
#endif

#ifndef OS2EXPORT
#define OS2EXPORT __attribute__((visibility("default")))
#endif

#ifndef OS2API
#define OS2API APIENTRY
#endif

/* OS2API16 declarations use the host C ABI; guest entry uses generated USE16 bridges. */
#ifndef OS2API16
#define OS2API16 OS2API
#endif

    typedef uint32_t APIRET;
    typedef uint16_t APIRET16;
    typedef uint32_t APIRET32;

    typedef uint32_t BOOL;

    typedef void VOID;
    typedef VOID *PVOID;
    typedef PVOID *PPVOID;
    typedef char CHAR, *PCHAR;
    typedef uint8_t UCHAR, *PUCHAR;
    typedef int16_t SHORT, *PSHORT;
    typedef uint16_t USHORT, *PUSHORT;
    typedef int32_t LONG, *PLONG;
    typedef uint32_t ULONG, *PULONG;
    typedef uint32_t BOOL32, *PBOOL32;
    typedef int64_t LONGLONG, *PLONGLONG;
    typedef uint64_t ULONGLONG, *PULONGLONG;
    typedef uint8_t BYTE, *PBYTE;

    typedef uint16_t SHANDLE, *PSHANDLE;
    typedef uint32_t LHANDLE, *PLHANDLE;
    typedef LHANDLE HANDLE, *PHANDLE;
    typedef HANDLE HMODULE, *PHMODULE;
    typedef HANDLE HFILE, *PHFILE;
    typedef HANDLE HDIR, *PHDIR;
    typedef HANDLE HEV, *PHEV;
    typedef HANDLE HMTX, *PHMTX;
    typedef HANDLE HMUX, *PHMUX;
    typedef HANDLE HQUEUE, *PHQUEUE;
    typedef HANDLE HPS, *PHPS;
    typedef HANDLE HDC, *PHDC;
    typedef HANDLE HPOINTER, *PHPOINTER;
    typedef HANDLE PID, *PPID;
    typedef HANDLE TID, *PTID;
    typedef SHANDLE HVIO, *PHVIO;
    typedef SHANDLE HKBD, *PHKBD;

    typedef PCHAR PSZ;
    typedef PCHAR PCH;

    typedef USHORT SEL;
    typedef PUSHORT PSEL;

    typedef int(APIENTRY *PFN)(void);

    typedef ULONG ERRORID;

#define NULLHANDLE 0
#define TRUE 1
#define FALSE 0

#ifdef __cplusplus
}
#endif

#endif
