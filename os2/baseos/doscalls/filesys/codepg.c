/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"
#include "os2/include/bsedos16.h"
#include "os2/include/private/bsep.h"
#include "emulator/launch_record.h"
#include <ctype.h>

APIRET DosSetProcessCp(ULONG cp)
{
    TRACE_NATIVE("DosSetProcessCp(%u)", (unsigned int)cp);
    if (cp != 437 && cp != 850)
    { // United States
        FIXME("non-US codepages unsupported at the moment");
        return ERROR_INVALID_CODE_PAGE;
    }
    GProcessState.process_codepage = (USHORT)cp;
    return NO_ERROR;
}

APIRET16 Dos16SetProcCp(USHORT codepage, USHORT reserved)
{
    return reserved ? ERROR_INVALID_PARAMETER : (APIRET16)DosSetProcessCp(codepage);
}
