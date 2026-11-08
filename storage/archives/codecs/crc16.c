/* Public domain. No rights reserved. Wholly AI-authored. */
/* CRC-16/ARC, used by ARC and zoo members and zoo headers. */
#include "storage/archives/codecs/codecs.h"

uint16_t tb_crc16_arc(const unsigned char *data, size_t size)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1) ? 0xa001 : 0));
    }
    return crc;
}
