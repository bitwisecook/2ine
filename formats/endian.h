/* Public domain. No rights reserved. Wholly AI-authored. */
/* Byte-order access to on-disk and in-file fields that may be unaligned. */
#ifndef TB_ENDIAN_H
#define TB_ENDIAN_H
#include <stdint.h>

static inline uint16_t tb_le16(const void *at)
{
    const unsigned char *p = (const unsigned char *)at;
    return (uint16_t)(p[0] | (unsigned)p[1] << 8);
}

static inline uint32_t tb_le32(const void *at)
{
    const unsigned char *p = (const unsigned char *)at;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t tb_le64(const void *at)
{
    const unsigned char *p = (const unsigned char *)at;
    return (uint64_t)tb_le32(p) | (uint64_t)tb_le32(p + 4) << 32;
}

static inline uint32_t tb_be32(const void *at)
{
    const unsigned char *p = (const unsigned char *)at;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static inline void tb_put_le16(void *at, uint16_t value)
{
    unsigned char *p = (unsigned char *)at;
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static inline void tb_put_le32(void *at, uint32_t value)
{
    unsigned char *p = (unsigned char *)at;
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(value >> (8 * i));
}
#endif
