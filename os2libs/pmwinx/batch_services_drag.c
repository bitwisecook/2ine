/* Public domain. No rights reserved. Wholly AI-authored. */
/* DROPFILES is a guest memory object, never a host DROPFILES pointer. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
VOID DragAcceptFiles(ULONG window, BOOL accept)
{
    LONG style = GetWindowLong(window, -20);
    SetWindowLong(window, -20, accept ? style | 0x10 : style & ~0x10);
}
VOID DragFinish(ULONG drop)
{
    GlobalFree(drop);
}
static ULONG drop_address(ULONG drop, uint8 header[20])
{
    ULONG address = GlobalLock(drop);
    if (!address || !lx_execution_read(GLoaderState.execution, address, header, 20))
    {
        open32SetError(6);
        return 0;
    }
    return address;
}
ULONG DragQueryFile(ULONG drop, ULONG index, PSZ output, ULONG capacity)
{
    uint8 header[20];
    ULONG base = drop_address(drop, header);
    if (!base)
        return 0;
    ULONG offset = lx_guest_load_u32(header), wide = lx_guest_load_u32(header + 16), address;
    if (offset < 20 || offset > 16u * 1024u * 1024u || base > UINT32_MAX - offset)
    {
        GlobalUnlock(drop);
        open32SetError(87);
        return 0;
    }
    address = base + offset;
    ULONG count = 0, result = 0;
    for (ULONG scanned = 0; scanned < 16u * 1024u * 1024u;)
    {
        ULONG start = address, length = 0;
        USHORT c = 0;
        for (;;)
        {
            uint8 wire[2];
            ULONG step = wide ? 2 : 1;
            if (address > UINT32_MAX - step ||
                !lx_execution_read(GLoaderState.execution, address, wire, step))
                goto drop_done;
            c = wide ? lx_guest_load_u16(wire) : wire[0];
            address += step;
            scanned += step;
            if (!c)
                break;
            if (count == index && output && length + 1 < capacity)
                output[length] = c < 256 ? (char)c : '?';
            length++;
            if (scanned >= 16u * 1024u * 1024u)
                goto drop_done;
        }
        if (address - start == (wide ? 2u : 1u))
        {
            if (index == UINT32_MAX)
                result = count;
            break;
        }
        if (count == index)
        {
            if (output && capacity)
                output[length < capacity ? length : capacity - 1] = 0;
            result = length;
            break;
        }
        count++;
    }
drop_done:
    GlobalUnlock(drop);
    return result;
}
BOOL DragQueryPoint(ULONG drop, PVOID output)
{
    uint8 header[20];
    ULONG address = drop_address(drop, header);
    if (!address)
        return FALSE;
    if (output)
        memcpy(output, header + 4, 8);
    BOOL result = !lx_guest_load_u32(header + 12);
    GlobalUnlock(drop);
    return result;
}
