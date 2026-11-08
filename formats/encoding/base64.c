/* Public domain. No rights reserved. Wholly AI-authored. */
#include "formats/encoding/base64.h"
#include <stdlib.h>

char *tb_base64(const uint8_t *bytes, size_t size)
{
    if (size > 64 * 1024 * 1024)
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
        out[j++] = alphabet[(v >> 12) & 63];
        out[j++] = i + 1 < size ? alphabet[(v >> 6) & 63] : '=';
        out[j++] = i + 2 < size ? alphabet[v & 63] : '=';
    }
    out[n] = 0;
    return out;
}
