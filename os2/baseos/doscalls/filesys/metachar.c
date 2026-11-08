/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

static unsigned editNameFold(unsigned char value)
{
    return value >= 'a' && value <= 'z' ? value - ('a' - 'A') : value;
}

APIRET DosEditName(ULONG level, PSZ source, PSZ edit, PBYTE target, ULONG capacity)
{
    TRACE_NATIVE("Dos16EditName(%u, '%s', '%s', %p, %u)", level, source, edit, target, capacity);
    if (level != 1 || !source || !edit || !target || !capacity)
        return ERROR_INVALID_PARAMETER;
    if (strpbrk(source, "\\/:") || strpbrk(edit, "\\/:"))
        return ERROR_INVALID_NAME;
    char result[CCHMAXPATHCOMP];
    unsigned used = 0;
    const char *input = source;
    for (const char *mask = edit; *mask; mask++)
    {
        if (*mask == '*')
        {
            while (*input && (!mask[1] || editNameFold(*input) != editNameFold(mask[1])))
            {
                if (used + 1 >= sizeof result)
                    return ERROR_INVALID_NAME;
                result[used++] = *input++;
            }
        }
        else if (*mask == '?')
        {
            if (*input && *input != '.')
            {
                if (used + 1 >= sizeof result)
                    return ERROR_INVALID_NAME;
                result[used++] = *input++;
            }
        }
        else
        {
            if (used + 1 >= sizeof result)
                return ERROR_INVALID_NAME;
            result[used++] = *mask;
            if (*mask == '.')
            {
                while (*input && *input != '.')
                    input++;
            }
            if (*input)
                input++;
        }
    }
    if (used && result[used - 1] == '.')
        used--;
    result[used] = 0;
    if (used + 1 > capacity)
        return ERROR_INVALID_NAME;
    uint32 address;
    if (!lx_execution_address(GProcessState.execution, target, used + 1, LX_GUEST_WRITE, &address))
        return ERROR_INVALID_PARAMETER;
    memcpy(target, result, used + 1);
    return NO_ERROR;
}

APIRET16 Dos16EditName(USHORT level, PSZ source, PSZ edit, PBYTE target, USHORT capacity)
{
    return DosEditName(level, source, edit, target, capacity);
}
