/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosEnterMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosEnterMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2)LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0xFFFF)
    {
        return ERROR_NESTING_TOO_DEEP;
    }
    else if (tib2->tib2_usMCCount == 0)
    {
        FIXME("block signals here");
    }

    tib2->tib2_usMCCount++;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
}

APIRET DosExitMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosExitMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2)LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0)
    {
        return ERROR_ALREADY_RESET;
    }
    else if (tib2->tib2_usMCCount == 1)
    {
        FIXME("unblock signals here");
    }

    tib2->tib2_usMCCount--;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
}
