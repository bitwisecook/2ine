/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded bit reader for the legacy archive codecs. */
#include "storage/archives/codecs/codecs.h"

unsigned tb_bits_msb(TbBitReader *bits, unsigned count)
{
    if (!bits->valid || count > 24 || bits->bit > bits->size * 8 ||
        count > bits->size * 8 - bits->bit)
    {
        bits->valid = 0;
        return 0;
    }
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++, bits->bit++)
        value = (value << 1) | ((bits->data[bits->bit / 8] >> (7 - bits->bit % 8)) & 1);
    return value;
}
