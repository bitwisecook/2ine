/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_OS2DEF_H_
#define _INCL_OS2DEF_H_

#include <stdint.h>
#include "host/abi.h"

#ifdef __cplusplus
extern "C"
{
#endif

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
    typedef HANDLE HSEM;

    typedef HANDLE HAB, *PHAB;

    typedef HANDLE HMQ, *PHMQ;

    typedef HANDLE HWND, *PHWND;

    typedef PVOID MPARAM, *PMPARAM;

    typedef PVOID MRESULT, *PMRESULT;

    typedef struct
    {
        LONG x;
        LONG y;
    } POINTL, *PPOINTL;

    typedef struct
    {
        LONG xLeft;
        LONG yBottom;
        LONG xRight;
        LONG yTop;
    } RECTL, *PRECTL;

    /* FATTRS selection flags */
    enum
    {
        FATTR_SEL_ITALIC = 0x0001,
        FATTR_SEL_UNDERSCORE = 0x0002,
        FATTR_SEL_OUTLINE = 0x0008,
        FATTR_SEL_STRIKEOUT = 0x0010,
        FATTR_SEL_BOLD = 0x0020
    };

    /* FONTMETRICS selection flags */
    enum
    {
        FM_SEL_ITALIC = 0x0001,
        FM_SEL_BOLD = 0x0020
    };

    /* Font attribute use */
    enum
    {
        FATTR_FONTUSE_NOMIX = 0x0002
    };

    /* ICONINFO fFormat values */
    enum
    {
        ICON_FILE = 1,
        ICON_RESOURCE = 2,
        ICON_DATA = 3,
        ICON_CLEAR = 4
    };

/* Mutable guest data exports: classObject followed by SOM method token words. The
   generated module glue maps them; guest SOM fills them when a class is built.
   pmwp.def gives each export's ordinal; lxapigen.py reads the size here. */
#define LX_GUEST_DATA_EXPORT(name, bytes)                                                          \
    enum                                                                                           \
    {                                                                                              \
        name##_bytes = bytes                                                                       \
    }

#define NULLHANDLE 0
#define TRUE 1
#define FALSE 0

#ifdef __cplusplus
}
#endif

#endif
