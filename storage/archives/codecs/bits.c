/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded bit reader for the legacy archive codecs, in either bit order. */
#include "storage/archives/codecs/codecs.h"

/* Clears bits->valid unless count more bits remain. */
static int bits_available(TbBitReader *bits, unsigned count)
{
    if (!bits->valid || count > TB_BITS_MAX || bits->bit > bits->size * 8 ||
        count > bits->size * 8 - bits->bit)
    {
        bits->valid = 0;
        return 0;
    }
    return 1;
}

unsigned tb_bits_msb(TbBitReader *bits, unsigned count)
{
    if (!bits_available(bits, count))
        return 0;
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++, bits->bit++)
        value = (value << 1) | ((bits->data[bits->bit / 8] >> (7 - bits->bit % 8)) & 1);
    return value;
}

unsigned tb_bits_lsb(TbBitReader *bits, unsigned count)
{
    if (!bits_available(bits, count))
        return 0;
    unsigned value = 0;
    for (unsigned i = 0; i < count; i++, bits->bit++)
        value |= ((bits->data[bits->bit / 8] >> (bits->bit % 8)) & 1u) << i;
    return value;
}
