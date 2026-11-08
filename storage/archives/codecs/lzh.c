/* Public domain. No rights reserved. Wholly AI-authored. */
/* LZH blocks (ARJ methods 1-3, zoo method 2): a 16-bit symbol count, then code-length,
   literal/length and distance trees, then symbols. A zero block count represents 65536
   symbols rather than an end marker. */
#include "storage/archives/codecs/codecs.h"
#include <limits.h>

static int lzh_pt_tree(TbBitReader *bits, TbHuffman *tree, unsigned alphabet, unsigned width,
                       unsigned special)
{
    unsigned n = tb_bits_msb(bits, width);
    if (!n)
    {
        tree->single = (int)tb_bits_msb(bits, width);
        return bits->valid && (unsigned)tree->single < alphabet;
    }
    if (n > alphabet)
        return 0;
    unsigned char lengths[19] = {0};
    unsigned at = 0;
    while (at < n && bits->valid)
    {
        unsigned length = tb_bits_msb(bits, 3);
        if (length == 7)
            while (tb_bits_msb(bits, 1))
                if (++length > 16)
                    return 0;
        lengths[at++] = (unsigned char)length;
        if (at == special)
        {
            unsigned zeros = tb_bits_msb(bits, 2);
            if (zeros > alphabet - at)
                return 0;
            at += zeros;
        }
    }
    return bits->valid && tb_huffman_build(tree, lengths, alphabet);
}

static int lzh_literal_tree(TbBitReader *bits, TbHuffman *tree, const TbHuffman *length_tree)
{
    unsigned n = tb_bits_msb(bits, 9);
    if (!n)
    {
        tree->single = (int)tb_bits_msb(bits, 9);
        return bits->valid && tree->single < 510;
    }
    if (n > 510)
        return 0;
    unsigned char lengths[510] = {0};
    unsigned at = 0;
    while (at < n && bits->valid)
    {
        int code = tb_huffman_symbol(bits, length_tree);
        if (code < 0)
            return 0;
        if (code <= 2)
        {
            unsigned zeros = code == 0   ? 1
                             : code == 1 ? tb_bits_msb(bits, 4) + 3
                                         : tb_bits_msb(bits, 9) + 20;
            if (zeros > n - at)
                return 0;
            at += zeros;
        }
        else
        {
            if (code > 18)
                return 0;
            lengths[at++] = (unsigned char)(code - 2);
        }
    }
    return bits->valid && tb_huffman_build(tree, lengths, 510);
}

int tb_lzh_decode(const unsigned char *input, size_t packed, unsigned char *output, size_t expected,
                  unsigned dictionary_bits)
{
    TbBitReader bits = {input, packed, 0, 1};
    TbHuffman lengths, literals, distances;
    unsigned remaining = 0;
    size_t at = 0;
    while (at < expected && bits.valid)
    {
        if (!remaining)
        {
            remaining = tb_bits_msb(&bits, 16);
            if (!remaining)
                remaining = 65536;
            if (!lzh_pt_tree(&bits, &lengths, 19, 5, 3) ||
                !lzh_literal_tree(&bits, &literals, &lengths) ||
                !lzh_pt_tree(&bits, &distances, dictionary_bits + 1, dictionary_bits <= 13 ? 4 : 5,
                             UINT_MAX))
                return 0;
        }
        remaining--;
        int code = tb_huffman_symbol(&bits, &literals);
        if (code < 0 || !bits.valid)
            return 0;
        if (code < 256)
            output[at++] = (unsigned char)code;
        else
        {
            unsigned length = (unsigned)code - 253;
            int symbol = tb_huffman_symbol(&bits, &distances);
            if (symbol < 0 || symbol > (int)dictionary_bits)
                return 0;
            unsigned distance =
                symbol ? (1u << (symbol - 1)) + tb_bits_msb(&bits, (unsigned)symbol - 1) : 0;
            distance++;
            if (!bits.valid || distance > at || length > expected - at)
                return 0;
            for (unsigned i = 0; i < length; i++, at++)
                output[at] = output[at - distance];
        }
    }
    return bits.valid && at == expected;
}
