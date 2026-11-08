/* Public domain. No rights reserved. Wholly AI-authored. */
/* ARC squeezed members: a node table of signed child links, then LSB-first codes. */
#include "storage/archives/codecs/codecs.h"
#include "formats/endian.h"

/* A node count, then per node two signed 16-bit links: a non-negative link is the next
   node, a negative link -1-symbol ends a code. Symbol 256 ends the stream. */
enum
{
    SQUEEZE_HEADER_BYTES = 2,
    SQUEEZE_NODE_BYTES = 4,
    SQUEEZE_LINK_BYTES = 2,
    SQUEEZE_NODE_LIMIT = 256,
    SQUEEZE_END = 256
};

int tb_squeeze_decode(const unsigned char *input, size_t packed, unsigned char *output,
                      size_t capacity, size_t *used)
{
    if (packed < SQUEEZE_HEADER_BYTES)
        return 0;
    unsigned count = tb_le16(input);
    if (!count || count > SQUEEZE_NODE_LIMIT ||
        SQUEEZE_HEADER_BYTES + count * SQUEEZE_NODE_BYTES > packed)
        return 0;
    TbBitReader bits = {input, packed, (SQUEEZE_HEADER_BYTES + count * SQUEEZE_NODE_BYTES) * 8, 1};
    *used = 0;
    for (;;)
    {
        unsigned node = 0;
        int value;
        do
        {
            if (node >= count)
                return 0;
            unsigned direction = tb_bits_lsb(&bits, 1);
            if (!bits.valid)
                return 0;
            value = (int16_t)tb_le16(input + SQUEEZE_HEADER_BYTES + node * SQUEEZE_NODE_BYTES +
                                     direction * SQUEEZE_LINK_BYTES);
            node = (unsigned)value;
        } while (value >= 0);
        value = -value - 1;
        if (value == SQUEEZE_END)
            return 1;
        if (value > 255 || *used == capacity)
            return 0;
        output[(*used)++] = (unsigned char)value;
    }
}
