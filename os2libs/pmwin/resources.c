/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 string bundles contain a codepage word and sixteen counted ASCIIZ strings. */
#include "os2libs/pmwin/internal.h"
LONG WinLoadString(HAB hab, HMODULE module, ULONG id, LONG capacity, PCHAR output)
{
    TRACE_NATIVE("WinLoadString(%u, %u, %u, %d, %p)", (uint)hab, (uint)module, (uint)id,
                 (int)capacity, output);
    if (!output || capacity <= 0 || id > UINT16_MAX)
        return 0;
    output[0] = 0;
    const ULONG bundle = id / 16 + 1;
    ULONG size = 0;
    void *data = NULL;
    if (!GLoaderState.queryResourceSize || !GLoaderState.getResource ||
        !GLoaderState.freeResource || GLoaderState.queryResourceSize(module, 5, bundle, &size) ||
        size < 2 || GLoaderState.getResource(module, 5, bundle, &data))
        return 0;
    const uint8 *bytes = data;
    size_t at = 2;
    LONG result = 0;
    for (unsigned i = 0; i <= (id & 15) && at < size; i++)
    {
        unsigned length = bytes[at++];
        if (length > size - at)
            break;
        if (i == (id & 15) && length && !bytes[at + length - 1])
        {
            size_t count = length - 1;
            if (count >= (size_t)capacity)
                count = (size_t)capacity - 1;
            memcpy(output, bytes + at, count);
            output[count] = 0;
            result = (LONG)count;
            break;
        }
        at += length;
    }
    GLoaderState.freeResource(data);
    return result;
}
