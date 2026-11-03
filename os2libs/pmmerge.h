/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMMERGE_H_
#define _INCL_PMMERGE_H_

#include "pmwin.h"

#ifdef __cplusplus
extern "C"
{
#endif

    OS2EXPORT ULONG OS2API WinQueryCpType(HMQ queue) OS2APIINFO(5480);

#ifdef __cplusplus
}
#endif

#endif
