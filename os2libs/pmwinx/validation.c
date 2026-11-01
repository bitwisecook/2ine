/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pointer queries inspect declared guest mappings without dereferencing bad addresses. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
static BOOL bad_span(PVOID pointer, ULONG bytes, unsigned protection)
{
    if (!bytes)
        return FALSE;
    uintptr_t address = (uintptr_t)pointer;
    return address > UINT32_MAX ||
           !lx_execution_memory(GLoaderState.execution, (uint32)address, bytes, protection);
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
