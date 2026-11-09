/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 resource identifiers pack the OS/2 type and name into one word. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"

static HMODULE resource_module(HMODULE module)
{
    return (!module || module == UINT32_MAX) && GProcessState.main_module
               ? GProcessState.main_module->handle
               : module;
}

ULONG FindResource(HMODULE module, PSZ name, PSZ type)
{
    TRACE_NATIVE("FindResource(%u, %p, %p)", module, name, type);
    static const uint16 types[] = {0, 1, 2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 11};
    uintptr_t identity = (uintptr_t)name, kind = (uintptr_t)type;
    if (!identity || identity > UINT16_MAX || !kind || kind > UINT16_MAX)
    {
        open32SetError(ERROR_INVALID_NAME);
        return 0;
    }
    if (kind <= 255)
        kind = kind < sizeof types / sizeof *types ? types[kind] : 0;
    ULONG size;
    if (!kind || !GProcessState.queryResourceSize ||
        GProcessState.queryResourceSize(resource_module(module), kind, identity, &size))
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    return (ULONG)((kind << 16) | identity);
}

ULONG SizeofResource(HMODULE module, ULONG resource)
{
    ULONG size;
    if (!resource || !GProcessState.queryResourceSize ||
        GProcessState.queryResourceSize(resource_module(module), resource >> 16, resource & 0xFFFF,
                                        &size))
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    return size;
}

/* Applications read a bitmap resource as a DIB, so its file headers are skipped:
   an optional 14-byte array header, then the 14-byte header of the image itself. */
static uint32 bitmap_header(const uint8 *data, uint32 address)
{
    if (lx_guest_load_u16(data) == 0x4142) /* BA */
    {
        data += 14;
        address += 14;
    }
    switch (lx_guest_load_u16(data))
    {
        case 0x4D42: /* BM */
        case 0x4349: /* IC */
        case 0x4943: /* CI */
        case 0x5043: /* CP */
        case 0x5450: /* PT */
            return address + 14;
        default:
            return address;
    }
}

ULONG LoadResource(HMODULE module, ULONG resource)
{
    void *data;
    uint32 address;
    if (!resource || !GProcessState.getResource ||
        GProcessState.getResource(resource_module(module), resource >> 16, resource & 0xFFFF,
                                  &data) ||
        !lx_execution_address(GProcessState.execution, data, 1, LX_GUEST_READ, &address))
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    return resource >> 16 == 2 ? bitmap_header(data, address) : address;
}

ULONG LockResource(ULONG resource)
{
    if (!lx_execution_memory(GProcessState.execution, resource, 1, LX_GUEST_READ))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return resource;
}

LONG LoadString(HMODULE module, ULONG identity, PCHAR output, LONG capacity)
{
    TRACE_NATIVE("LoadString(%u, %u, %p, %d)", module, identity, output, capacity);
    if (!module || !output || capacity <= 0)
    {
        if (module && output && capacity > 0)
            output[0] = 0;
        open32SetError(module ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return 0;
    }
    if (identity & 0xFF000000u)
        identity &= 0xFFFF;
    LONG result = WinLoadString(0, resource_module(module), identity, capacity, output);
    if (!result)
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return result;
}
