/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_BSEXCPT_H_
#define _INCL_BSEXCPT_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Exception records */

    typedef void *PEXCEPTIONREGISTRATIONRECORD;

    typedef void *PEXCEPTIONREPORTRECORD;

    typedef void *PCONTEXTRECORD;

#ifdef __cplusplus
}
#endif

#endif
