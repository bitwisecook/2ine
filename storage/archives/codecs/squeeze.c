/* Public domain. No rights reserved. Wholly AI-authored. */
/* ARC squeezed members: a node table of signed child links, then LSB-first codes. */
#include "storage/archives/codecs/codecs.h"
#include "formats/endian.h"

int tb_squeeze_decode(const unsigned char *input, size_t packed, unsigned char *output,
                      size_t capacity, size_t *used)
{
    if (packed < 2)
        return 0;
    unsigned count = tb_le16(input);
    if (!count || count > 256 || 2 + count * 4 > packed)
        return 0;
    size_t bit = (2 + count * 4) * 8;
    *used = 0;
    for (;;)
    {
        unsigned node = 0;
        int value;
        do
        {
            if (node >= count || bit >= packed * 8)
                return 0;
            unsigned direction = (input[bit / 8] >> (bit % 8)) & 1;
            bit++;
            value = (int16_t)tb_le16(input + 2 + node * 4 + direction * 2);
            node = (unsigned)value;
        } while (value >= 0);
        value = -value - 1;
        if (value == 256)
            return 1;
        if (value > 255 || *used == capacity)
            return 0;
        output[(*used)++] = (unsigned char)value;
    }
}
