/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hex encoding shared by the image, device and container virtual paths. */
#include "storage/paths/hex_path.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *tb_hex_path_encode(const char *prefix, const char *text)
{
    static const char digits[] = "0123456789abcdef";
    size_t n = strlen(text), start = strlen(prefix);
    if (n > (SIZE_MAX - start - 1) / 2)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    char *result = malloc(start + n * 2 + 1);
    if (!result)
        return NULL;
    memcpy(result, prefix, start);
    for (size_t i = 0; i < n; i++)
    {
        result[start + i * 2] = digits[(unsigned char)text[i] >> 4];
        result[start + i * 2 + 1] = digits[(unsigned char)text[i] & 15];
    }
    result[start + n * 2] = 0;
    return result;
}

static int digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

char *tb_hex_path_decode(const char *hex, size_t length)
{
    if (length % 2)
    {
        errno = EINVAL;
        return NULL;
    }
    char *result = malloc(length / 2 + 1);
    if (!result)
        return NULL;
    for (size_t i = 0; i < length; i += 2)
    {
        int high = digit(hex[i]), low = digit(hex[i + 1]);
        if (high < 0 || low < 0 || !(high | low))
        {
            free(result);
            errno = EINVAL;
            return NULL;
        }
        result[i / 2] = (char)((high << 4) | low);
    }
    result[length / 2] = 0;
    return result;
}
