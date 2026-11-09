/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded clipboard PNG and BMP pixel conversion. */
#include "host/clipboard.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

enum
{
    /* Larger clipboard images are refused. */
    MAX_IMAGE_SIDE = 16384,
    MAX_IMAGE_PIXELS = 16777216,
    MAX_IMAGE_BYTES = 64 * 1024 * 1024,
    RGBA_BYTES = 4,

    /* PNG: 8-byte signature, then the IHDR chunk's length, type, width and height. */
    PNG_SIGNATURE_BYTES = 8,
    PNG_IHDR_LENGTH_OFFSET = 8,
    PNG_IHDR_DATA_LENGTH = 13,
    PNG_IHDR_TYPE_OFFSET = 12,
    PNG_IHDR_WIDTH_OFFSET = 16,
    PNG_IHDR_HEIGHT_OFFSET = 20,
    PNG_MIN_BYTES = 33, /* signature and the complete IHDR chunk */

    /* BMP: 14-byte file header, then a BITMAPCOREHEADER (12) or BITMAPINFOHEADER (40+). */
    BMP_INFO_OFFSET = 14,
    BMP_CORE_HEADER_BYTES = 12,
    BMP_CORE_WIDTH_OFFSET = 18,
    BMP_CORE_HEIGHT_OFFSET = 20,
    BMP_INFO_HEADER_BYTES = 40,
    BMP_INFO_WIDTH_OFFSET = 18,
    BMP_INFO_HEIGHT_OFFSET = 22,
    BMP_INFO_MIN_FILE_BYTES = BMP_INFO_OFFSET + BMP_INFO_HEADER_BYTES,
    MIN_IMAGE_BYTES = BMP_INFO_OFFSET + BMP_CORE_HEADER_BYTES
};

void *lx_host_clipboard_encode_image(const char *mime, const void *rgba, unsigned width,
                                     unsigned height, size_t *size)
{
    if (!mime || !rgba || !size || !width || !height || width > MAX_IMAGE_SIDE ||
        height > MAX_IMAGE_SIDE || (uint64_t)width * height > MAX_IMAGE_PIXELS)
        return NULL;
    SDL_Surface *surface = SDL_CreateSurfaceFrom((int)width, (int)height, SDL_PIXELFORMAT_RGBA32,
                                                 (void *)rgba, (int)width * RGBA_BYTES);
    SDL_IOStream *stream = SDL_IOFromDynamicMem();
    void *result = NULL;
    if (surface && stream)
    {
        int ok = !strcmp(mime, "image/png")   ? SDL_SavePNG_IO(surface, stream, false)
                 : !strcmp(mime, "image/bmp") ? SDL_SaveBMP_IO(surface, stream, false)
                                              : 0;
        Sint64 length = SDL_GetIOSize(stream);
        if (ok && length > 0 && length <= MAX_IMAGE_BYTES)
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
    if (!mime || !bytes || !width || !height || size < MIN_IMAGE_BYTES || size > MAX_IMAGE_BYTES)
        return NULL;
    const uint8_t *p = bytes;
    uint32_t w = 0, h = 0;
    int png = !strcmp(mime, "image/png");
    if (png && size >= PNG_MIN_BYTES && !memcmp(p, "\x89PNG\r\n\x1a\n", PNG_SIGNATURE_BYTES) &&
        big32(p + PNG_IHDR_LENGTH_OFFSET) == PNG_IHDR_DATA_LENGTH &&
        !memcmp(p + PNG_IHDR_TYPE_OFFSET, "IHDR", 4))
        w = big32(p + PNG_IHDR_WIDTH_OFFSET), h = big32(p + PNG_IHDR_HEIGHT_OFFSET);
    else if (!strcmp(mime, "image/bmp") && p[0] == 'B' && p[1] == 'M')
    {
        uint32_t header = little32(p + BMP_INFO_OFFSET);
        if (header == BMP_CORE_HEADER_BYTES)
            w = p[BMP_CORE_WIDTH_OFFSET] | (unsigned)p[BMP_CORE_WIDTH_OFFSET + 1] << 8,
            h = p[BMP_CORE_HEIGHT_OFFSET] | (unsigned)p[BMP_CORE_HEIGHT_OFFSET + 1] << 8;
        else if (header >= BMP_INFO_HEADER_BYTES && size >= BMP_INFO_MIN_FILE_BYTES)
        {
            /* A negative height marks a top-down bitmap. */
            int32_t signed_height = (int32_t)little32(p + BMP_INFO_HEIGHT_OFFSET);
            w = little32(p + BMP_INFO_WIDTH_OFFSET);
            h = signed_height < 0 ? (uint32_t)-(int64_t)signed_height : (uint32_t)signed_height;
        }
    }
    if (!w || !h || w > MAX_IMAGE_SIDE || h > MAX_IMAGE_SIDE || (uint64_t)w * h > MAX_IMAGE_PIXELS)
        return NULL;
    SDL_IOStream *stream = SDL_IOFromConstMem(bytes, size);
    SDL_Surface *surface =
        stream ? (png ? SDL_LoadPNG_IO(stream, true) : SDL_LoadBMP_IO(stream, true)) : NULL;
    SDL_Surface *converted = surface ? SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32) : NULL;
    void *result = NULL;
    if (converted && converted->w == (int)w && converted->h == (int)h)
    {
        result = malloc((size_t)w * h * RGBA_BYTES);
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
