/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "formats/decompress.h"
#include <string.h>
#include <assert.h>
void linearmove(uint8 *dst, const uint8 *src, uint8 len)
{
    while (len--)
    {
        *dst = *src;
        dst++;
        src++;
    }
}

int decompressExePack2(uint8 *dst, const uint32 dstlen, const uint8 *src, const uint32 srclen)
{
    sint32 sOf = 0;
    sint32 dOf = 0;
    sint32 bOf = 0;
    uint8 b1 = 0;
    uint8 b2 = 0;

#define SRCAVAIL(n) ((sOf + (n)) <= srclen)
#define DSTAVAIL(n) ((dOf + (n)) <= dstlen)

    do
    {
        if (!SRCAVAIL(1))
            break;

        b1 = src[sOf];
        switch (b1 & 3)
        {
            case 0:
                if (b1 == 0)
                {
                    if (SRCAVAIL(2))
                    {
                        if (src[sOf + 1] == 0)
                        {
                            sOf += 2;
                            break;
                        }
                        else if (SRCAVAIL(3) && DSTAVAIL(src[sOf + 1]))
                        {
                            memset(dst + dOf, src[sOf + 2], src[sOf + 1]);
                            sOf += 3;
                            dOf += src[sOf - 2];
                        }
                        else
                        {
                            return 0;
                        }
                    }
                    else
                    {
                        return 0;
                    }
                }
                else if (SRCAVAIL((b1 >> 2) + 1) && DSTAVAIL(b1 >> 2))
                {
                    memcpy(dst + dOf, src + (sOf + 1), b1 >> 2);
                    dOf += b1 >> 2;
                    sOf += (b1 >> 2) + 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 1:
                if (!SRCAVAIL(2))
                    return 0;
                bOf = (*((const uint16 *)(src + sOf))) >> 7;
                b2 = ((b1 >> 4) & 7) + 3;
                b1 = ((b1 >> 2) & 3);
                sOf += 2;
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && ((dOf + b1 - bOf) >= 0))
                {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    linearmove(dst + dOf, dst + (dOf - bOf), b2);
                    dOf += b2;
                }
                else
                {
                    return 0;
                }
                break;

            case 2:
                if (!SRCAVAIL(2))
                    return 0;
                bOf = (*((const uint16 *)(src + sOf))) >> 4;
                b1 = ((b1 >> 2) & 3) + 3;
                if (DSTAVAIL(b1) && (dOf - bOf >= 0))
                {
                    linearmove(dst + dOf, dst + (dOf - bOf), b1);
                    dOf += b1;
                    sOf += 2;
                }
                else
                {
                    return 0;
                }
                break;

            case 3:
                if (!SRCAVAIL(3))
                    return 0;
                b2 = ((*((const uint16 *)(src + sOf))) >> 6) & 0x3F;
                b1 = (src[sOf] >> 2) & 0x0F;
                bOf = (*((const uint16 *)(src + (sOf + 1)))) >> 4;
                sOf += 3;
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && ((dOf + b1 - bOf) >= 0))
                {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    linearmove(dst + dOf, dst + (dOf - bOf), b2);
                    dOf += b2;
                }
                else
                {
                    return 0;
                }
                break;
        }
    } while (dOf < dstlen);
#undef SRCAVAIL
#undef DSTAVAIL

    if ((dstlen - dOf) > 0)
        memset(dst + dOf, '\0', dstlen - dOf);

    return 1;
}

int decompressIterated(uint8 *dst, uint32 dstlen, const uint8 *src, uint32 srclen)
{
    while (srclen)
    {
        if (srclen < 4)
            return 0;
        const uint16 iterations = *((uint16 *)src);
        src += 2;
        const uint16 len = *((uint16 *)src);
        src += 2;
        srclen -= 4;
        if (dstlen < (iterations * len))
            return 0;
        else if (srclen < len)
            return 0;

        for (uint16 i = 0; i < iterations; i++)
        {
            memcpy(dst, src, len);
            dst += len;
            dstlen -= len;
        }

        src += len;
        srclen -= len;
    }

    if (dstlen > 0)
        memset(dst, '\0', dstlen);

    return 1;
}
