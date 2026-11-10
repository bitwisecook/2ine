/* Public domain. No rights reserved. Wholly AI-authored. */
#include "formats/encoding/base64.h"
#include <stdlib.h>

enum
{
    /* Larger inputs are refused rather than encoded into an unbounded JSON string. */
    MAX_INPUT_BYTES = 64 * 1024 * 1024,
    SEXTET_MASK = 63
};

char *tb_base64(const uint8_t *bytes, size_t size)
{
    if (size > MAX_INPUT_BYTES)
        return NULL;
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = ((size + 2) / 3) * 4;
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    for (size_t i = 0, j = 0; i < size; i += 3)
    {
        uint32_t v = (uint32_t)bytes[i] << 16;
        if (i + 1 < size)
            v |= (uint32_t)bytes[i + 1] << 8;
        if (i + 2 < size)
            v |= bytes[i + 2];
        out[j++] = alphabet[v >> 18];
        out[j++] = alphabet[(v >> 12) & SEXTET_MASK];
        out[j++] = i + 1 < size ? alphabet[(v >> 6) & SEXTET_MASK] : '=';
        out[j++] = i + 2 < size ? alphabet[v & SEXTET_MASK] : '=';
    }
    out[n] = 0;
    return out;
}
