/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 icon files retain an AND mask and a monochrome or color XOR image. */
#include "os2libs/os2native.h"
#include "os2libs/pmwin/pointer_image.h"
typedef struct IconBitmap
{
    unsigned width, height, depth, colors, palette_bytes;
    size_t header, bits, stride;
} IconBitmap;
static BOOL bitmap(const uint8 *bytes, size_t size, size_t offset, IconBitmap *out)
{
    if (offset > size || size - offset < 26)
        return FALSE;
    const uint8 *p = bytes + offset + 14;
    unsigned header = lx_guest_load_u32(p);
    if ((header != 12 && header != 40 && header != 64) || header > size - offset - 14)
        return FALSE;
    unsigned planes;
    if (header == 12)
    {
        out->width = lx_guest_load_u16(p + 4);
        out->height = lx_guest_load_u16(p + 6);
        planes = lx_guest_load_u16(p + 8);
        out->depth = lx_guest_load_u16(p + 10);
        out->palette_bytes = 3;
        out->colors = out->depth <= 8 ? 1u << out->depth : 0;
    }
    else
    {
        out->width = lx_guest_load_u32(p + 4);
        out->height = lx_guest_load_u32(p + 8);
        planes = lx_guest_load_u16(p + 12);
        out->depth = lx_guest_load_u16(p + 14);
        out->palette_bytes = 4;
        out->colors = lx_guest_load_u32(p + 32);
        if (lx_guest_load_u32(p + 16))
            return FALSE;
        if (out->depth <= 8 && !out->colors)
            out->colors = 1u << out->depth;
    }
    if (!out->width || !out->height || out->width > 1024 || out->height > 2048 || planes != 1 ||
        (out->depth != 1 && out->depth != 4 && out->depth != 8 && out->depth != 24 &&
         out->depth != 32) ||
        (out->depth <= 8 ? out->colors > 1u << out->depth : out->colors != 0))
        return FALSE;
    out->header = offset + 14 + header;
    size_t palette = (size_t)out->colors * out->palette_bytes;
    if (palette > size - out->header)
        return FALSE;
    out->bits = lx_guest_load_u32(bytes + offset + 10);
    out->stride = ((size_t)out->width * out->depth + 31) / 32 * 4;
    return out->bits >= out->header + palette && out->bits <= size &&
           out->stride * out->height <= size - out->bits;
}
static BOOL pixel(const uint8 *bytes, const IconBitmap *bitmap, unsigned x, unsigned y, uint32 *rgb)
{
    const uint8 *row = bytes + bitmap->bits + bitmap->stride * y;
    if (bitmap->depth <= 8)
    {
        unsigned bit = x * bitmap->depth;
        unsigned index =
            (row[bit / 8] >> (8 - bitmap->depth - bit % 8)) & ((1u << bitmap->depth) - 1);
        if (index >= bitmap->colors)
            return FALSE;
        const uint8 *color = bytes + bitmap->header + index * bitmap->palette_bytes;
        *rgb = (uint32)color[2] << 16 | (uint32)color[1] << 8 | color[0];
    }
    else
    {
        const uint8 *color = row + x * (bitmap->depth / 8);
        *rgb = (uint32)color[2] << 16 | (uint32)color[1] << 8 | color[0];
    }
    return TRUE;
}
void pmPointerImageFree(PmPointerImage *image)
{
    if (!image)
        return;
    free(image->pixels);
    free(image->mask);
    free(image);
}
PmPointerImage *pmPointerImage(const uint8 *bytes, size_t size)
{
    if (!bytes || size < 26)
        return NULL;
    size_t offset = lx_guest_load_u16(bytes) == 0x4142 ? 14 : 0;
    if (size - offset < 26)
        return NULL;
    unsigned type = lx_guest_load_u16(bytes + offset);
    IconBitmap mask = {0}, color = {0};
    BOOL colored = type == 0x4943 || type == 0x5043;
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
    PmPointerImage *image = calloc(1, sizeof *image);
    size_t count = (size_t)mask.width * height;
    if (image)
    {
        image->pixels = malloc(count * sizeof *image->pixels);
        image->mask = malloc(count);
    }
    if (!image || !image->pixels || !image->mask)
    {
        pmPointerImageFree(image);
        return NULL;
    }
    image->width = mask.width;
    image->height = height;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < mask.width; x++)
        {
            size_t at = (size_t)y * mask.width + x;
            const uint8 *and_row = bytes + mask.bits + (y + height) * mask.stride;
            image->mask[at] = (and_row[x / 8] >> (7 - x % 8)) & 1;
            if (colored)
            {
                if (!pixel(bytes, &color, x, y, &image->pixels[at]))
                {
                    pmPointerImageFree(image);
                    return NULL;
                }
            }
            else
            {
                const uint8 *xor_row = bytes + mask.bits + y * mask.stride;
                image->pixels[at] = xor_row[x / 8] & (0x80 >> (x % 8)) ? 0xffffff : 0;
            }
        }
    return image;
}
