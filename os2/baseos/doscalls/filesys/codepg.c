/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

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

APIRET DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP)
{
    TRACE_NATIVE("DosQueryCp(%u, %p, %p)", (uint)cb, arCP, pcCP);

    if (cb && !arCP)
        return ERROR_INVALID_PARAMETER;
    const ULONG pages[] = {GProcessState.process_codepage ? GProcessState.process_codepage : 437,
                           437, 850};
    ULONG count = cb / sizeof(ULONG);
    if (count > 3)
        count = 3;
    if (count)
        memcpy(arCP, pages, count * sizeof(ULONG));
    if (pcCP)
        *pcCP = count * sizeof(ULONG);
    return count < 3 ? ERROR_CPLIST_TOO_SMALL : NO_ERROR;
}

APIRET16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP)
{
    TRACE_NATIVE("Dos16GetCp(%u, %p, %p)", (uint)cb, arCP, pcCP);

    if (cb && !arCP)
        return ERROR_INVALID_PARAMETER;
    const USHORT pages[] = {GProcessState.process_codepage ? GProcessState.process_codepage : 437,
                            437, 850};
    USHORT count = cb / sizeof(USHORT);
    if (count > 3)
        count = 3;
    if (count)
        memcpy(arCP, pages, count * sizeof(USHORT));
    if (pcCP)
        *pcCP = count * sizeof(USHORT);
    return count < 3 ? ERROR_CPLIST_TOO_SMALL : NO_ERROR;
}
