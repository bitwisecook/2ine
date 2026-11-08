/* Public domain. No rights reserved. Wholly AI-authored. */
/* ARC RLE90 run-length decoding. */
#include "storage/archives/codecs/codecs.h"
#include <string.h>

int tb_rle90_decode(const unsigned char *input, size_t length, unsigned char *output,
                    size_t expected)
{
    size_t used = 0;
    for (size_t at = 0; at < length; at++)
    {
        unsigned byte = input[at];
        if (byte != 0x90)
        {
            if (used == expected)
                return 0;
            output[used++] = (unsigned char)byte;
            continue;
        }
        if (++at == length)
            return 0;
        unsigned repeat = input[at];
        if (!repeat)
        {
            if (used == expected)
                return 0;
            output[used++] = 0x90;
        }
        else
        {
            if (!used || repeat - 1 > expected - used)
                return 0;
            memset(output + used, output[used - 1], repeat - 1);
            used += repeat - 1;
        }
    }
    return used == expected;
}
