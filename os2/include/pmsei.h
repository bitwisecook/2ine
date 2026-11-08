/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMSEI_H_
#define _INCL_PMSEI_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_WINSEI: error information */

    OS2EXPORT ERRORID OS2API WinSetErrorInfo(ERRORID error, ULONG flags, ...);

#ifdef __cplusplus
}
#endif

#endif
