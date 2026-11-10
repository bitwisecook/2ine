/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Process code pages: DosSetProcessCp and DosQueryCp. */
#include "os2/baseos/doscalls/internal.h"

/* DosQueryCp lists the current code page, then the prepared ones. */
enum
{
    CODEPAGE_LIST_COUNT = 3
};

APIRET DosSetProcessCp(ULONG cp)
{
    TRACE_NATIVE("DosSetProcessCp(%u)", (unsigned int)cp);
    if (cp != CODEPAGE_US && cp != CODEPAGE_MULTILINGUAL)
    {
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
    const ULONG pages[] = {GProcessState.process_codepage ? GProcessState.process_codepage
                                                          : CODEPAGE_US,
                           CODEPAGE_US, CODEPAGE_MULTILINGUAL};
    ULONG count = cb / sizeof(ULONG);
    if (count > CODEPAGE_LIST_COUNT)
        count = CODEPAGE_LIST_COUNT;
    if (count)
        memcpy(arCP, pages, count * sizeof(ULONG));
    if (pcCP)
        *pcCP = count * sizeof(ULONG);
    return count < CODEPAGE_LIST_COUNT ? ERROR_CPLIST_TOO_SMALL : NO_ERROR;
}

APIRET16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP)
{
    TRACE_NATIVE("Dos16GetCp(%u, %p, %p)", (uint)cb, arCP, pcCP);

    if (cb && !arCP)
        return ERROR_INVALID_PARAMETER;
    const USHORT pages[] = {GProcessState.process_codepage ? GProcessState.process_codepage
                                                           : CODEPAGE_US,
                            CODEPAGE_US, CODEPAGE_MULTILINGUAL};
    USHORT count = cb / sizeof(USHORT);
    if (count > CODEPAGE_LIST_COUNT)
        count = CODEPAGE_LIST_COUNT;
    if (count)
        memcpy(arCP, pages, count * sizeof(USHORT));
    if (pcCP)
        *pcCP = count * sizeof(USHORT);
    return count < CODEPAGE_LIST_COUNT ? ERROR_CPLIST_TOO_SMALL : NO_ERROR;
}

APIRET16 Dos16SetCp(USHORT codepage, USHORT reserved)
{
    return Dos16SetProcCp(codepage, reserved);
}
