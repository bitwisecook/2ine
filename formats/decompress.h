/* Derived from original 2ine; zlib-style license. See ../LICENSE.txt. */
#ifndef TWOINE_DECOMPRESS_H
#define TWOINE_DECOMPRESS_H
#include "formats/types.h"
LX_PRIVATE void linearmove(uint8 *dst, const uint8 *src, uint8 len);
LX_PRIVATE int decompressExePack2(uint8 *dst, const uint32 dstlen, const uint8 *src,
                                  const uint32 srclen);
LX_PRIVATE int decompressIterated(uint8 *dst, uint32 dstlen, const uint8 *src, uint32 srclen);
#endif
