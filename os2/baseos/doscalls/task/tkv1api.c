/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Environment scanning and process IDs for the 16-bit API. */
#include "os2/baseos/doscalls/internal.h"

APIRET DosScanEnv(PSZ name, PSZ *outval)
{
    TRACE_NATIVE("DosScanEnv('%s', %p)", name, outval);

    char *env = GProcessState.pib.environment;
    const size_t len = strlen(name);
    while (*env)
    {
        if ((strncmp(env, name, len) == 0) && (env[len] == '='))
        {
            *outval = env + len + 1;
            return NO_ERROR;
        }
        env += strlen(env) + 1;
    }

    return ERROR_ENVVAR_NOT_FOUND;
}

APIRET16 Dos16ScanEnv(PSZ name, PULONG value)
{
    if (!name || !value)
        return ERROR_INVALID_PARAMETER;
    PSZ text;
    APIRET rc = DosScanEnv(name, &text);
    if (rc)
        return (APIRET16)rc;
    ULONG pointer = GProcessState.convert32to1616(text);
    if (!pointer)
        return ERROR_INVALID_ADDRESS;
    lx_guest_store_u32(value, pointer);
    return NO_ERROR;
}

APIRET16 Dos16GetPID(PPIDINFO16 ppidinfo)
{
    TRACE_NATIVE("Dos16GetPID(%p)", ppidinfo);
    PTIB2 tib2 = (PTIB2)LX_GETTIB2();

    // This interface has 16-bit fields even when host IDs are wider.
    ppidinfo->pid = (USHORT)GProcessState.pib.process_id;
    ppidinfo->tid = (USHORT)tib2->tib2_ultid;
    ppidinfo->pidParent = (USHORT)GProcessState.pib.parent_id;
    return NO_ERROR;
}

APIRET16 Dos16GetEnv(PUSHORT psel, PUSHORT pcmdoffset)
{
    TRACE_NATIVE("Dos16GetEnv(%p, %p)", psel, pcmdoffset);
    const uint32 cmd1616 = GProcessState.convert32to1616(GProcessState.pib.command);
    if (pcmdoffset)
        *pcmdoffset = (USHORT)(cmd1616 & 0xFFFF);
    if (psel)
        *psel = (USHORT)((cmd1616 >> 16) & 0xFFFF);
    return NO_ERROR;
}
