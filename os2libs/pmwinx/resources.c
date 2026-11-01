/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 resource identifiers pack the OS/2 type and name into one word. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"

static HMODULE resource_module(HMODULE module)
{
    return (!module || module == UINT32_MAX) && GLoaderState.main_module
               ? GLoaderState.main_module->handle
               : module;
}

ULONG FindResource(HMODULE module, PSZ name, PSZ type)
{
    TRACE_NATIVE("FindResource(%u, %p, %p)", module, name, type);
    static const uint16 types[] = {0, 1, 2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 11};
    uintptr_t identity = (uintptr_t)name, kind = (uintptr_t)type;
    if (!identity || identity > UINT16_MAX || !kind || kind > UINT16_MAX)
    {
        open32SetError(123);
        return 0;
    }
    if (kind <= 255)
        kind = kind < sizeof types / sizeof *types ? types[kind] : 0;
    ULONG size;
    if (!kind || !GLoaderState.queryResourceSize ||
        GLoaderState.queryResourceSize(resource_module(module), kind, identity, &size))
    {
        open32SetError(1814);
        return 0;
    }
    return (ULONG)((kind << 16) | identity);
}

ULONG SizeofResource(HMODULE module, ULONG resource)
{
    ULONG size;
    if (!resource || !GLoaderState.queryResourceSize ||
        GLoaderState.queryResourceSize(resource_module(module), resource >> 16, resource & 0xFFFF,
                                       &size))
    {
        open32SetError(1814);
        return 0;
    }
    return size;
}

ULONG LoadResource(HMODULE module, ULONG resource)
{
    void *data;
    uint32 address;
    if (!resource || !GLoaderState.getResource ||
        GLoaderState.getResource(resource_module(module), resource >> 16, resource & 0xFFFF,
                                 &data) ||
        !lx_execution_address(GLoaderState.execution, data, 1, LX_GUEST_READ, &address))
    {
        open32SetError(1814);
        return 0;
    }
    return address;
}

ULONG LockResource(ULONG resource)
{
    if (!lx_execution_memory(GLoaderState.execution, resource, 1, LX_GUEST_READ))
    {
        open32SetError(6);
        return 0;
    }
    return resource;
}

LONG LoadString(HMODULE module, ULONG identity, PCHAR output, LONG capacity)
{
    TRACE_NATIVE("LoadString(%u, %u, %p, %d)", module, identity, output, capacity);
    if (!module || !output || capacity <= 0)
    {
        if (output && capacity > 0)
            output[0] = 0;
        open32SetError(module ? 87 : 6);
        return 0;
    }
    if (identity & 0xFF000000u)
        identity &= 0xFFFF;
    LONG result = WinLoadString(0, resource_module(module), identity, capacity, output);
    if (!result)
        open32SetError(1814);
    return result;
}
