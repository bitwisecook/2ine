/* Public domain. No rights reserved. Wholly AI-authored. */
/* Canonical Huffman codes for the LZH decoder. */
#include "storage/archives/codecs/codecs.h"
#include <string.h>

int tb_huffman_build(TbHuffman *tree, const unsigned char *lengths, unsigned count)
{
    memset(tree, 0, sizeof *tree);
    tree->single = -1;
    for (unsigned i = 0; i < count; i++)
    {
        if (lengths[i] > 16)
            return 0;
        tree->counts[lengths[i]]++;
    }
    tree->counts[0] = 0;
    int available = 1;
    unsigned positions[17] = {0};
    for (unsigned length = 1; length <= 16; length++)
    {
        available = available * 2 - (int)tree->counts[length];
        if (available < 0)
            return 0;
        if (length > 1)
            positions[length] = positions[length - 1] + tree->counts[length - 1];
    }
    for (unsigned i = 0; i < count; i++)
        if (lengths[i])
            tree->symbols[positions[lengths[i]]++] = i;
    return 1;
}

int tb_huffman_symbol(TbBitReader *bits, const TbHuffman *tree)
{
    if (tree->single >= 0)
        return tree->single;
    unsigned code = 0, first = 0, index = 0;
    for (unsigned length = 1; length <= 16; length++)
    {
        code = (code << 1) | tb_bits_msb(bits, 1);
        if (!bits->valid)
            return -1;
        if (code >= first && code - first < tree->counts[length])
            return (int)tree->symbols[index + code - first];
        index += tree->counts[length];
        first = (first + tree->counts[length]) << 1;
    }
    bits->valid = 0;
    return -1;
}
