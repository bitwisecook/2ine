/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 icon files retain an AND mask and a monochrome or color XOR image. */
#include "formats/resources/icon.h"
#include <stdlib.h>
#include <string.h>

static unsigned read16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)read16(p) | (uint32_t)read16(p + 2) << 16;
}

typedef struct IconBitmap
{
    unsigned width, height, depth, colors, palette_bytes;
    size_t header, bits, stride;
} IconBitmap;

static int bitmap(const uint8_t *bytes, size_t size, size_t offset, IconBitmap *out)
{
    if (offset > size || size - offset < 26)
        return 0;
    const uint8_t *p = bytes + offset + 14;
    unsigned header = read32(p);
    if ((header != 12 && header != 40 && header != 64) || header > size - offset - 14)
        return 0;
    unsigned planes;
    if (header == 12)
    {
        out->width = read16(p + 4);
        out->height = read16(p + 6);
        planes = read16(p + 8);
        out->depth = read16(p + 10);
        out->palette_bytes = 3;
        out->colors = out->depth <= 8 ? 1u << out->depth : 0;
    }
    else
    {
        out->width = read32(p + 4);
        out->height = read32(p + 8);
        planes = read16(p + 12);
        out->depth = read16(p + 14);
        out->palette_bytes = 4;
        out->colors = read32(p + 32);
        if (read32(p + 16))
            return 0;
        if (out->depth <= 8 && !out->colors)
            out->colors = 1u << out->depth;
    }
    if (!out->width || !out->height || out->width > 1024 || out->height > 2048 || planes != 1 ||
        (out->depth != 1 && out->depth != 4 && out->depth != 8 && out->depth != 24 &&
         out->depth != 32) ||
        (out->depth <= 8 ? out->colors > 1u << out->depth : out->colors != 0))
        return 0;
    out->header = offset + 14 + header;
    size_t palette = (size_t)out->colors * out->palette_bytes;
    if (palette > size - out->header)
        return 0;
    out->bits = read32(bytes + offset + 10);
    out->stride = ((size_t)out->width * out->depth + 31) / 32 * 4;
    return out->bits >= out->header + palette && out->bits <= size &&
           out->stride * out->height <= size - out->bits;
}

static int pixel(const uint8_t *bytes, const IconBitmap *bitmap, unsigned x, unsigned y,
                 uint32_t *rgb)
{
    const uint8_t *row = bytes + bitmap->bits + bitmap->stride * y;
    if (bitmap->depth <= 8)
    {
        unsigned bit = x * bitmap->depth;
        unsigned index =
            (row[bit / 8] >> (8 - bitmap->depth - bit % 8)) & ((1u << bitmap->depth) - 1);
        if (index >= bitmap->colors)
            return 0;
        const uint8_t *color = bytes + bitmap->header + index * bitmap->palette_bytes;
        *rgb = (uint32_t)color[2] << 16 | (uint32_t)color[1] << 8 | color[0];
    }
    else
    {
        const uint8_t *color = row + x * (bitmap->depth / 8);
        *rgb = (uint32_t)color[2] << 16 | (uint32_t)color[1] << 8 | color[0];
    }
    return 1;
}

void tb_icon_free(TbIconImage *image)
{
    if (!image)
        return;
    free(image->pixels);
    free(image->mask);
    free(image);
}

static TbIconImage *decode_one(const uint8_t *bytes, size_t size, size_t offset)
{
    if (!bytes || size < 26)
        return NULL;
    if (offset > size || size - offset < 26)
        return NULL;
    unsigned type = read16(bytes + offset);
    IconBitmap mask = {0}, color = {0};
    int colored = type == 0x4943 || type == 0x5043;
    if ((!colored && type != 0x4349 && type != 0x5450) || !bitmap(bytes, size, offset, &mask) ||
        mask.depth != 1 || mask.height % 2)
        return NULL;
    unsigned height = mask.height / 2;
    if (colored)
    {
        size_t next = mask.header + mask.colors * mask.palette_bytes;
        if (!bitmap(bytes, size, next, &color) || color.width != mask.width ||
            color.height != height)
            return NULL;
    }
    TbIconImage *image = calloc(1, sizeof *image);
    size_t count = (size_t)mask.width * height;
    if (image)
    {
        image->pixels = malloc(count * sizeof *image->pixels);
        image->mask = malloc(count);
    }
    if (!image || !image->pixels || !image->mask)
    {
        tb_icon_free(image);
        return NULL;
    }
    image->width = mask.width;
    image->height = height;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < mask.width; x++)
        {
            size_t at = (size_t)y * mask.width + x;
            const uint8_t *and_row = bytes + mask.bits + (y + height) * mask.stride;
            image->mask[at] = (and_row[x / 8] >> (7 - x % 8)) & 1;
            if (colored)
            {
                if (!pixel(bytes, &color, x, y, &image->pixels[at]))
                {
                    tb_icon_free(image);
                    return NULL;
                }
            }
            else
            {
                const uint8_t *xor_row = bytes + mask.bits + y * mask.stride;
                image->pixels[at] = xor_row[x / 8] & (0x80 >> (x % 8)) ? 0xffffff : 0;
            }
        }
    return image;
}

static TbIconImage *windows_icon(const uint8_t *bytes, size_t size)
{
    unsigned count = size >= 6 ? read16(bytes + 4) : 0;
    if (!count || count > 256 || (size_t)count * 16 > size - 6)
        return NULL;
    TbIconImage *best = NULL;
    for (unsigned i = 0; i < count; i++)
    {
        const uint8_t *entry = bytes + 6 + i * 16;
        size_t offset = read32(entry + 12), length = read32(entry + 8);
        if (offset > size || length > size - offset || length < 40)
            continue;
        const uint8_t *dib = bytes + offset;
        IconBitmap color = {0};
        color.width = read32(dib + 4);
        unsigned height = read32(dib + 8), planes = read16(dib + 12);
        color.depth = read16(dib + 14);
        if (read32(dib) != 40 || !color.width || color.width > 1024 || !height || height > 2048 ||
            height % 2 || planes != 1 || read32(dib + 16) ||
            (color.depth != 1 && color.depth != 4 && color.depth != 8 && color.depth != 24))
            continue;
        color.height = height / 2;
        color.colors = read32(dib + 32);
        color.palette_bytes = 4;
        if (!color.colors && color.depth <= 8)
            color.colors = 1u << color.depth;
        if (color.depth <= 8 ? color.colors > 1u << color.depth : color.colors != 0)
            continue;
        color.header = offset + 40;
        size_t palette = (size_t)color.colors * 4;
        color.stride = ((size_t)color.width * color.depth + 31) / 32 * 4;
        size_t mask_stride = ((size_t)color.width + 31) / 32 * 4,
               xor_bytes = color.stride * color.height, mask_bytes = mask_stride * color.height;
        if (palette > length - 40 || xor_bytes > length - 40 - palette ||
            mask_bytes > length - 40 - palette - xor_bytes)
            continue;
        color.bits = color.header + palette;
        size_t mask_bits = color.bits + xor_bytes;
        TbIconImage *image = calloc(1, sizeof *image);
        size_t pixels = (size_t)color.width * color.height;
        if (image)
        {
            image->pixels = malloc(pixels * sizeof *image->pixels);
            image->mask = malloc(pixels);
        }
        if (!image || !image->pixels || !image->mask)
        {
            tb_icon_free(image);
            break;
        }
        image->width = color.width;
        image->height = color.height;
        int valid = 1;
        for (unsigned y = 0; y < color.height && valid; y++)
            for (unsigned x = 0; x < color.width; x++)
            {
                size_t at = (size_t)y * color.width + x;
                image->mask[at] = (bytes[mask_bits + mask_stride * y + x / 8] >> (7 - x % 8)) & 1;
                if (!pixel(bytes, &color, x, y, &image->pixels[at]))
                {
                    valid = 0;
                    break;
                }
            }
        if (valid && (!best || pixels > (size_t)best->width * best->height))
        {
            tb_icon_free(best);
            best = image;
        }
        else
            tb_icon_free(image);
    }
    return best;
}

/* Bitmap-array links and bitmap data offsets are relative to the complete
   resource. Decode every valid member and prefer the largest useful image. */
TbIconImage *tb_icon_bitmap(const uint8_t *bytes, size_t size)
{
    if (!bytes || size < 2)
        return NULL;
    if (size >= 6 && !read16(bytes) && read16(bytes + 2) == 1)
        return windows_icon(bytes, size);
    if (read16(bytes) != 0x4142)
        return decode_one(bytes, size, 0);
    TbIconImage *best = NULL;
    size_t at = 0;
    for (unsigned i = 0; i < 128; i++)
    {
        if (at > size || size - at < 40 || read16(bytes + at) != 0x4142)
            break;
        TbIconImage *image = decode_one(bytes, size, at + 14);
        if (image && (!best || image->width * image->height > best->width * best->height))
        {
            tb_icon_free(best);
            best = image;
        }
        else
            tb_icon_free(image);
        uint32_t next = read32(bytes + at + 6);
        if (!next || next <= at)
            break;
        at = next;
    }
    return best;
}

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

char *tb_icon_rgba_base64(const TbIconImage *image)
{
    size_t count = (size_t)image->width * image->height;
    uint8_t *rgba = malloc(count * 4);
    if (!rgba)
        return NULL;
    for (unsigned y = 0; y < image->height; y++)
        for (unsigned x = 0; x < image->width; x++)
        {
            size_t source = (size_t)(image->height - 1 - y) * image->width + x,
                   target = ((size_t)y * image->width + x) * 4;
            uint32_t rgb = image->pixels[source];
            rgba[target] = rgb >> 16;
            rgba[target + 1] = rgb >> 8;
            rgba[target + 2] = rgb;
            rgba[target + 3] = image->mask[source] ? (rgb ? 128 : 0) : 255;
        }
    char *result = tb_base64(rgba, count * 4);
    free(rgba);
    return result;
}
