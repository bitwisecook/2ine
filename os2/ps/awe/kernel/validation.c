/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pointer queries inspect declared guest mappings without dereferencing bad addresses. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"

static BOOL bad_span(PVOID pointer, ULONG bytes, unsigned protection)
{
    if (!bytes)
        return FALSE;
    uintptr_t address = (uintptr_t)pointer;
    return address > UINT32_MAX ||
           !lx_execution_memory(GProcessState.execution, (uint32)address, bytes, protection);
}

BOOL IsBadReadPtr(PVOID pointer, ULONG bytes)
{
    return bad_span(pointer, bytes, LX_GUEST_READ);
}

BOOL IsBadWritePtr(PVOID pointer, ULONG bytes)
{
    return bad_span(pointer, bytes, LX_GUEST_WRITE);
}

BOOL IsBadCodePtr(ULONG procedure)
{
    return !open32ValidProcedure(procedure);
}

BOOL IsBadHugeReadPtr(PVOID pointer, ULONG size)
{
    return IsBadReadPtr(pointer, size);
}

BOOL IsBadHugeWritePtr(PVOID pointer, ULONG size)
{
    return IsBadWritePtr(pointer, size);
}

BOOL IsBadStringPtr(ULONG address, ULONG maximum)
{
    for (ULONG i = 0; i < maximum; i++)
    {
        uint8 byte;
        if (address > UINT32_MAX - i ||
            !lx_execution_read(GProcessState.execution, address + i, &byte, 1))
        {
            open32SetError(ERROR_ACCESS_DENIED);
            return TRUE;
        }
        if (!byte)
            return FALSE;
    }
    return FALSE;
}
