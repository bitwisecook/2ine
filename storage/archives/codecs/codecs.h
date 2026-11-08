/* Public domain. No rights reserved. Wholly AI-authored. */
/* Decoders shared by the ARC, ARJ and zoo readers. Each decodes a whole member held in
   memory and returns 1 only when the output is complete. */
#ifndef TB_STORAGE_CODECS_H
#define TB_STORAGE_CODECS_H
#include <stddef.h>
#include <stdint.h>

/* CRC-16/ARC: reflected polynomial 0xA001, initial value 0. */
uint16_t tb_crc16_arc(const unsigned char *data, size_t size);

/* A bounded bit reader over size bytes; bit is the number of bits consumed. valid
   clears on overrun, and every later read returns 0. ARJ and LZH codes are read
   MSB-first, zoo LZD and ARC squeeze codes LSB-first. */
typedef struct TbBitReader
{
    const unsigned char *data;
    size_t size, bit;
    int valid;
} TbBitReader;

#define TB_BITS_MAX 24u

/* Read count (at most TB_BITS_MAX) bits. MSB-first takes each byte's most significant
   bit first and returns the first bit as the value's most significant; LSB-first takes
   each byte's least significant bit first and returns it as the least significant. */
unsigned tb_bits_msb(TbBitReader *bits, unsigned count);
unsigned tb_bits_lsb(TbBitReader *bits, unsigned count);

/* Canonical Huffman code of up to 510 symbols and 16-bit codes. single is the only
   symbol of a zero-length code, or -1. */
typedef struct TbHuffman
{
    unsigned counts[17], symbols[510];
    int single;
} TbHuffman;

/* Builds tree from per-symbol code lengths; 0 for an over-subscribed code. */
int tb_huffman_build(TbHuffman *tree, const unsigned char *lengths, unsigned count);
/* Reads one MSB-first symbol, or returns -1 and clears bits->valid. */
int tb_huffman_symbol(TbBitReader *bits, const TbHuffman *tree);

/* ARJ methods 1-3 and zoo method 2: LZH blocks with a 2^dictionary_bits window. */
int tb_lzh_decode(const unsigned char *input, size_t packed, unsigned char *output, size_t expected,
                  unsigned dictionary_bits);
/* ARC repeat-byte encoding: 0x90 n repeats the previous byte n-1 more times. */
int tb_rle90_decode(const unsigned char *input, size_t length, unsigned char *output,
                    size_t expected);
/* ARC method 4 (squeezed) Huffman tree and codes; *used receives the output length. */
int tb_squeeze_decode(const unsigned char *input, size_t packed, unsigned char *output,
                      size_t capacity, size_t *used);
/* zoo method 1 LZW, 9 to 13 bit codes. */
int tb_lzd_decode(const unsigned char *input, size_t packed, unsigned char *output, size_t size);
#endif
