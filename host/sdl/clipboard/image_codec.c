/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded clipboard pixel and explicit OS/2 single-byte text conversion. */
#include "host/clipboard.h"
#include "host/codepage_tables.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void *lx_host_clipboard_html_wrap(const void *html, size_t size, size_t *length)
{
    static const char pattern[] =
        "Version:1.0\r\nStartHTML:%010u\r\nEndHTML:%010u\r\nStartFragment:%010u\r\nEndFragment:%010u\r\n";
    static const char start[] = "<!--StartFragment-->", end[] = "<!--EndFragment-->";
    if (!length || (!html && size) || size > 16 * 1024 * 1024)
        return NULL;
    int header = snprintf(NULL, 0, pattern, 0u, 0u, 0u, 0u);
    size_t total = (size_t)header + sizeof start - 1 + size + sizeof end - 1;
    char *out = malloc(total + 1);
    if (!out)
        return NULL;
    snprintf(out, (size_t)header + 1, pattern, (unsigned)header, (unsigned)total,
             (unsigned)header + (unsigned)sizeof start - 1,
             (unsigned)header + (unsigned)sizeof start - 1 + (unsigned)size);
    memcpy(out + header, start, sizeof start - 1);
    memcpy(out + header + sizeof start - 1, html, size);
    memcpy(out + total - sizeof end + 1, end, sizeof end - 1);
    out[total] = 0;
    *length = total;
    return out;
}

static int html_offset(const unsigned char *data, size_t size, const char *key, size_t *value)
{
    size_t key_length = strlen(key), limit = size < 4096 ? size : 4096;
    for (size_t i = 0; i + key_length < limit; i++)
    {
        if ((i && data[i - 1] != '\n' && data[i - 1] != '\r') || memcmp(data + i, key, key_length))
            continue;
        size_t at = i + key_length, number = 0;
        if (at + 2 < limit && data[at] == '-' && data[at + 1] == '1' &&
            (data[at + 2] == '\r' || data[at + 2] == '\n'))
        {
            *value = SIZE_MAX;
            return 1;
        }
        if (data[at] < '0' || data[at] > '9')
            return 0;
        while (at < limit && data[at] >= '0' && data[at] <= '9')
        {
            if (number > size / 10)
                return 0;
            number = number * 10 + data[at++] - '0';
        }
        if (number > size || at == limit || (data[at] != '\r' && data[at] != '\n'))
            return 0;
        *value = number;
        return 1;
    }
    return 0;
}

void *lx_host_clipboard_html_extract(const void *data, size_t size, size_t *length)
{
    if (!length || (!data && size) || size > 16 * 1024 * 1024)
        return NULL;
    const unsigned char *source = data;
    size_t start = 0, end = size;
    if (size >= 8 && !memcmp(source, "Version:", 8))
    {
        if (!html_offset(source, size, "StartHTML:", &start) ||
            !html_offset(source, size, "EndHTML:", &end))
            return NULL;
        if (start == SIZE_MAX || end == SIZE_MAX)
            if (start != SIZE_MAX || end != SIZE_MAX ||
                !html_offset(source, size, "StartFragment:", &start) ||
                !html_offset(source, size, "EndFragment:", &end))
                return NULL;
        if (start > end || end > size || start < 8)
            return NULL;
    }
    char *copy = malloc(end - start + 1);
    if (!copy)
        return NULL;
    memcpy(copy, source + start, end - start);
    copy[end - start] = 0;
    *length = end - start;
    return copy;
}

static int page_index(unsigned codepage)
{
    return codepage == 437 ? 0 : codepage == 850 ? 1 : codepage == 1004 ? 2 : -1;
}

void *lx_host_clipboard_text_utf8(unsigned codepage, const void *bytes, size_t size, size_t *length)
{
    int page = page_index(codepage);
    if (!length || page < 0 || (!bytes && size) || size > 16 * 1024 * 1024)
        return NULL;
    const uint8_t *input = bytes;
    uint8_t *output = malloc(size * 3 + 1);
    if (!output)
        return NULL;
    size_t at = 0;
    for (size_t i = 0; i < size; i++)
    {
        unsigned point = input[i] < 128 ? input[i] : lx_host_sbcs_unicode[page][input[i] - 128];
        /* Native clipboard text uses LF; OS/2 source may contain CRLF. */
        if (point == '\r' && i + 1 < size && input[i + 1] == '\n')
            continue;
        if (point < 128)
            output[at++] = (uint8_t)point;
        else if (point < 2048)
        {
            output[at++] = 0xc0 | (point >> 6);
            output[at++] = 0x80 | (point & 63);
        }
        else
        {
            output[at++] = 0xe0 | (point >> 12);
            output[at++] = 0x80 | ((point >> 6) & 63);
            output[at++] = 0x80 | (point & 63);
        }
    }
    output[at] = 0;
    *length = at;
    return output;
}

void *lx_host_clipboard_text_os2(unsigned codepage, const void *bytes, size_t size, size_t *length)
{
    int page = page_index(codepage);
    if (!length || page < 0 || (!bytes && size) || size > 16 * 1024 * 1024)
        return NULL;
    const uint8_t *input = bytes;
    uint8_t *output = malloc(size * 2 + 1);
    if (!output)
        return NULL;
    size_t at = 0;
    for (size_t i = 0; i < size;)
    {
        uint32_t point = input[i++];
        unsigned rest = 0, minimum = 0;
        if (point >= 0xc2 && point <= 0xdf)
            rest = 1, minimum = 128, point &= 31;
        else if (point >= 0xe0 && point <= 0xef)
            rest = 2, minimum = 2048, point &= 15;
        else if (point >= 0xf0 && point <= 0xf4)
            rest = 3, minimum = 65536, point &= 7;
        else if (point >= 128)
            goto invalid;
        if (rest > size - i)
            goto invalid;
        while (rest--)
        {
            unsigned next = input[i++];
            if ((next & 0xc0) != 0x80)
                goto invalid;
            point = point << 6 | (next & 63);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            goto invalid;
        unsigned value = point;
        if (point >= 128)
        {
            value = '?';
            for (unsigned n = 0; n < 128; n++)
                if (lx_host_sbcs_unicode[page][n] == point)
                {
                    value = n + 128;
                    break;
                }
        }
        if (value == '\n' && (at == 0 || output[at - 1] != '\r'))
            output[at++] = '\r';
        output[at++] = (uint8_t)value;
    }
    output[at] = 0;
    *length = at;
    return output;
invalid:
    free(output);
    return NULL;
}

void *lx_host_clipboard_encode_image(const char *mime, const void *rgba, unsigned width,
                                     unsigned height, size_t *size)
{
    if (!mime || !rgba || !size || !width || !height || width > 16384 || height > 16384 ||
        (uint64_t)width * height > 16777216)
        return NULL;
    SDL_Surface *surface = SDL_CreateSurfaceFrom((int)width, (int)height, SDL_PIXELFORMAT_RGBA32,
                                                 (void *)rgba, (int)width * 4);
    SDL_IOStream *stream = SDL_IOFromDynamicMem();
    void *result = NULL;
    if (surface && stream)
    {
        int ok = !strcmp(mime, "image/png")   ? SDL_SavePNG_IO(surface, stream, false)
                 : !strcmp(mime, "image/bmp") ? SDL_SaveBMP_IO(surface, stream, false)
                                              : 0;
        Sint64 length = SDL_GetIOSize(stream);
        if (ok && length > 0 && length <= 64 * 1024 * 1024)
        {
            result = malloc((size_t)length);
            void *source = SDL_GetPointerProperty(SDL_GetIOProperties(stream),
                                                  SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
            if (result && source)
            {
                memcpy(result, source, (size_t)length);
                *size = (size_t)length;
            }
            else
            {
                free(result);
                result = NULL;
            }
        }
    }
    if (stream)
        SDL_CloseIO(stream);
    if (surface)
        SDL_DestroySurface(surface);
    return result;
}

static uint32_t little32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t big32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void *lx_host_clipboard_decode_image(const char *mime, const void *bytes, size_t size,
                                     unsigned *width, unsigned *height)
{
    if (!mime || !bytes || !width || !height || size < 26 || size > 64 * 1024 * 1024)
        return NULL;
    const uint8_t *p = bytes;
    uint32_t w = 0, h = 0;
    int png = !strcmp(mime, "image/png");
    if (png && size >= 33 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8) && big32(p + 8) == 13 &&
        !memcmp(p + 12, "IHDR", 4))
        w = big32(p + 16), h = big32(p + 20);
    else if (!strcmp(mime, "image/bmp") && p[0] == 'B' && p[1] == 'M')
    {
        uint32_t header = little32(p + 14);
        if (header == 12)
            w = p[18] | (unsigned)p[19] << 8, h = p[20] | (unsigned)p[21] << 8;
        else if (header >= 40 && size >= 54)
        {
            int32_t signed_height = (int32_t)little32(p + 22);
            w = little32(p + 18);
            h = signed_height < 0 ? (uint32_t)-(int64_t)signed_height : (uint32_t)signed_height;
        }
    }
    if (!w || !h || w > 16384 || h > 16384 || (uint64_t)w * h > 16777216)
        return NULL;
    SDL_IOStream *stream = SDL_IOFromConstMem(bytes, size);
    SDL_Surface *surface =
        stream ? (png ? SDL_LoadPNG_IO(stream, true) : SDL_LoadBMP_IO(stream, true)) : NULL;
    SDL_Surface *converted = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32) : NULL;
    void *result = NULL;
    if (converted && converted->w == (int)w && converted->h == (int)h)
    {
        result = malloc((size_t)w * h * 4);
        if (result)
        {
            for (unsigned y = 0; y < h; y++)
                memcpy((uint8_t *)result + (size_t)y * w * 4,
                       (const uint8_t *)converted->pixels + (size_t)y * converted->pitch, w * 4);
            *width = w;
            *height = h;
        }
    }
    if (converted)
        SDL_DestroySurface(converted);
    if (surface)
        SDL_DestroySurface(surface);
    return result;
}
