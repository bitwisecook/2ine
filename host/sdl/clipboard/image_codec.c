/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded clipboard PNG and BMP pixel conversion. */
#include "host/clipboard.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

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
