/* Public domain. No rights reserved. Wholly AI-authored. */
/* zoo LZD: LSB-first LZW with codes 256 (reset) and 257 (end). */
#include "storage/archives/codecs/codecs.h"
#include <limits.h>

static unsigned lzd_bits(TbBitReader *bits, unsigned count)
{
    if (!bits->valid || count > 13 || bits->bit > bits->size * 8 ||
        count > bits->size * 8 - bits->bit)
    {
        bits->valid = 0;
        return 0;
    }
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++, bits->bit++)
        value |= ((bits->data[bits->bit / 8] >> (bits->bit % 8)) & 1u) << i;
    return value;
}

int tb_lzd_decode(const unsigned char *input, size_t packed, unsigned char *output, size_t size)
{
    uint16_t prefix[8192];
    unsigned char suffix[8192], stack[8193];
    TbBitReader bits = {input, packed, 0, 1};
    unsigned width = 9, next = 258, previous = UINT_MAX, first = 0;
    size_t used = 0;
    while (bits.valid)
    {
        unsigned code = lzd_bits(&bits, width), original = code;
        if (!bits.valid)
            break;
        if (code == 256)
        {
            width = 9;
            next = 258;
            previous = UINT_MAX;
            continue;
        }
        if (code == 257)
            return used == size;
        if (code > next || code >= 8192)
            return 0;
        if (previous == UINT_MAX)
        {
            if (code > 255 || used == size)
                return 0;
            output[used++] = (unsigned char)code;
            previous = code;
            first = code;
            continue;
        }
        unsigned depth = 0;
        if (code == next)
        {
            stack[depth++] = (unsigned char)first;
            code = previous;
        }
        while (code > 255)
        {
            if (code < 258 || code >= next || depth >= 8192)
                return 0;
            stack[depth++] = suffix[code];
            code = prefix[code];
        }
        first = code;
        stack[depth++] = (unsigned char)first;
        if (depth > size - used)
            return 0;
        while (depth)
            output[used++] = stack[--depth];
        if (next < 8192)
        {
            prefix[next] = (uint16_t)previous;
            suffix[next++] = (unsigned char)first;
            if (next >= (1u << width) && width < 13)
                width++;
        }
        previous = original;
    }
    return 0;
}
