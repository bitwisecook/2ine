/* Derived from original 2ine; zlib-style license. See ../LICENSE.txt. */
#ifndef TWOINE_LX_EXEPACK_H
#define TWOINE_LX_EXEPACK_H
#include "formats/executable/types.h"
/* LX page decompression. Both return 0 when the packed data does not fit dstlen;
   on success the rest of dst is zero-filled. */
/* EXEPACK:2 (LX page flag 5): back-references with 4/8/12-bit lengths. */
LX_PRIVATE int lx_exepack2_decompress(uint8 *dst, const uint32 dstlen, const uint8 *src,
                                      const uint32 srclen);
/* EXEPACK:1 (LX page flag 1, NE iterated segments): repeated runs. */
LX_PRIVATE int lx_exepack_iterated_decompress(uint8 *dst, uint32 dstlen, const uint8 *src,
                                              uint32 srclen);
#endif
