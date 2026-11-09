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

    enum
    {
        EH_NONCONTINUABLE = 0x1,
        EH_UNWINDING = 0x2,
        EH_EXIT_UNWIND = 0x4,
        EH_STACK_INVALID = 0x8,
        EH_NESTED_CALL = 0x10
    };

#define XCPT_CONTINUE_SEARCH 0x00000000
#define XCPT_CONTINUE_EXECUTION 0xFFFFFFFF
#define XCPT_CONTINUE_STOP 0x00716668

#define XCPT_NONCONTINUABLE_EXCEPTION 0xC0000024
#define XCPT_INVALID_DISPOSITION 0xC0000025
#define XCPT_UNWIND 0xC0000026

    enum
    {
        CONTEXT_CONTROL = 0x00000001,
        CONTEXT_INTEGER = 0x00000002,
        CONTEXT_SEGMENTS = 0x00000004,
        CONTEXT_FLOATING_POINT = 0x00000008
    };

    enum
    {
        EXCEPTION_MAXIMUM_PARAMETERS = 4
    };

    typedef void *PEXCEPTIONREGISTRATIONRECORD;

    typedef void *PEXCEPTIONREPORTRECORD;

    typedef void *PCONTEXTRECORD;

#ifdef __cplusplus
}
#endif

#endif
