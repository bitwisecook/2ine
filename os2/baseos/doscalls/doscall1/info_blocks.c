/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosGetInfoBlocks(PTIB *pptib, PPIB *pppib)
{
    TRACE_NATIVE("DosGetInfoBlocks(%p, %p)", pptib, pppib);

    if (pptib != NULL)
        *pptib = (PTIB)LX_GETTIB();

    if (pppib != NULL)
        *pppib = GProcessState.guest_pib ? lx_execution_mapping_host(GProcessState.guest_pib)
                                         : (PPIB)&GProcessState.pib;

    return 0;
}
