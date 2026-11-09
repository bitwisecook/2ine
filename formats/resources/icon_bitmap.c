/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 icon files retain an AND mask and a monochrome or color XOR image. */
#include "formats/resources/icon.h"
#include "formats/encoding/base64.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>

/* OS/2 bitmap file values; names and values follow the Toolkit's pmbitmap.h. */
enum
{
    BFT_ICON = 0x4349,
    BFT_POINTER = 0x5450,
    BFT_COLORICON = 0x4943,
    BFT_COLORPOINTER = 0x5043,
    BFT_BITMAPARRAY = 0x4142
};

/* OS/2 bitmap file layout. A BITMAPFILEHEADER(2) is followed by a BITMAPINFOHEADER of
   12 bytes or a BITMAPINFOHEADER2 of 40 or 64 bytes; a bitmap array prefixes each member
   with a BITMAPARRAYFILEHEADER(2). */
enum
{
    FILE_HEADER_SIZE = 14,
    FILE_HEADER_BITS_OFFSET = 10, /* offBits */
    ARRAY_HEADER_SIZE = 14,
    ARRAY_HEADER_NEXT_OFFSET = 6, /* offNext */
    INFO_HEADER_SIZE = 12,
    INFO_HEADER2_SIZE = 40,
    INFO_HEADER2_FULL_SIZE = 64,
    /* The smallest file: a file header and a 12-byte info header. */
    MIN_BITMAP_FILE_SIZE = FILE_HEADER_SIZE + INFO_HEADER_SIZE,
    MIN_ARRAY_MEMBER_SIZE = ARRAY_HEADER_SIZE + MIN_BITMAP_FILE_SIZE,
    /* Members visited in one bitmap array. */
    MAX_ARRAY_MEMBERS = 128
};

/* Field offsets within a BITMAPINFOHEADER (12-byte) and BITMAPINFOHEADER2. */
enum
{
    INFO_CX = 4,
    INFO_CY = 6,
    INFO_PLANES = 8,
    INFO_BITCOUNT = 10,
    INFO2_CX = 4,
    INFO2_CY = 8,
    INFO2_PLANES = 12,
    INFO2_BITCOUNT = 14,
    INFO2_COMPRESSION = 16,
    INFO2_CCLRUSED = 32,
    RGB_ENTRY_SIZE = 3,  /* RGB */
    RGB2_ENTRY_SIZE = 4, /* RGB2, and the Windows RGBQUAD */
    /* Images larger than this are rejected as implausible icons. */
    MAX_ICON_WIDTH = 1024,
    MAX_ICON_HEIGHT = 2048
};

/* Windows .ico layout, accepted for icons copied from Windows. */
enum
{
    ICO_HEADER_SIZE = 6,
    ICO_HEADER_COUNT_OFFSET = 4,
    ICO_TYPE_ICON = 1,
    ICO_ENTRY_SIZE = 16,
    ICO_ENTRY_LENGTH_OFFSET = 8,
    ICO_ENTRY_IMAGE_OFFSET = 12,
    ICO_MAX_ENTRIES = 256
};

/* Rows of a DIB are padded to 32 bits. */
static size_t row_stride(size_t width, unsigned depth)
{
    return (width * depth + 31) / 32 * 4;
}

typedef struct IconBitmap
{
    unsigned width, height, depth, colors, palette_bytes;
    size_t header, bits, stride;
} IconBitmap;

static int bitmap(const uint8_t *bytes, size_t size, size_t offset, IconBitmap *out)
{
    if (offset > size || size - offset < MIN_BITMAP_FILE_SIZE)
        return 0;
    const uint8_t *p = bytes + offset + FILE_HEADER_SIZE;
    unsigned header = tb_le32(p);
    if ((header != INFO_HEADER_SIZE && header != INFO_HEADER2_SIZE &&
         header != INFO_HEADER2_FULL_SIZE) ||
        header > size - offset - FILE_HEADER_SIZE)
        return 0;
    unsigned planes;
    if (header == INFO_HEADER_SIZE)
    {
        out->width = tb_le16(p + INFO_CX);
        out->height = tb_le16(p + INFO_CY);
        planes = tb_le16(p + INFO_PLANES);
        out->depth = tb_le16(p + INFO_BITCOUNT);
        out->palette_bytes = RGB_ENTRY_SIZE;
        out->colors = out->depth <= 8 ? 1u << out->depth : 0;
    }
    else
    {
        out->width = tb_le32(p + INFO2_CX);
        out->height = tb_le32(p + INFO2_CY);
        planes = tb_le16(p + INFO2_PLANES);
        out->depth = tb_le16(p + INFO2_BITCOUNT);
        out->palette_bytes = RGB2_ENTRY_SIZE;
        out->colors = tb_le32(p + INFO2_CCLRUSED);
        /* Only uncompressed bitmaps are decoded. */
        if (tb_le32(p + INFO2_COMPRESSION))
            return 0;
        if (out->depth <= 8 && !out->colors)
            out->colors = 1u << out->depth;
    }
    if (!out->width || !out->height || out->width > MAX_ICON_WIDTH ||
        out->height > MAX_ICON_HEIGHT || planes != 1 ||
        (out->depth != 1 && out->depth != 4 && out->depth != 8 && out->depth != 24 &&
         out->depth != 32) ||
        (out->depth <= 8 ? out->colors > 1u << out->depth : out->colors != 0))
        return 0;
    out->header = offset + FILE_HEADER_SIZE + header;
    size_t palette = (size_t)out->colors * out->palette_bytes;
    if (palette > size - out->header)
        return 0;
    out->bits = tb_le32(bytes + offset + FILE_HEADER_BITS_OFFSET);
    out->stride = row_stride(out->width, out->depth);
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
    if (!bytes || size < MIN_BITMAP_FILE_SIZE)
        return NULL;
    if (offset > size || size - offset < MIN_BITMAP_FILE_SIZE)
        return NULL;
    unsigned type = tb_le16(bytes + offset);
    IconBitmap mask = {0}, color = {0};
    int colored = type == BFT_COLORICON || type == BFT_COLORPOINTER;
    if ((!colored && type != BFT_ICON && type != BFT_POINTER) ||
        !bitmap(bytes, size, offset, &mask) || mask.depth != 1 || mask.height % 2)
        return NULL;
    /* The monochrome bitmap stacks the XOR image above the AND mask. */
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
    unsigned count = size >= ICO_HEADER_SIZE ? tb_le16(bytes + ICO_HEADER_COUNT_OFFSET) : 0;
    if (!count || count > ICO_MAX_ENTRIES ||
        (size_t)count * ICO_ENTRY_SIZE > size - ICO_HEADER_SIZE)
        return NULL;
    TbIconImage *best = NULL;
    for (unsigned i = 0; i < count; i++)
    {
        const uint8_t *entry = bytes + ICO_HEADER_SIZE + i * ICO_ENTRY_SIZE;
        size_t offset = tb_le32(entry + ICO_ENTRY_IMAGE_OFFSET),
               length = tb_le32(entry + ICO_ENTRY_LENGTH_OFFSET);
        if (offset > size || length > size - offset || length < INFO_HEADER2_SIZE)
            continue;
        const uint8_t *dib = bytes + offset;
        IconBitmap color = {0};
        /* A Windows BITMAPINFOHEADER shares the BITMAPINFOHEADER2 field offsets used here. */
        color.width = tb_le32(dib + INFO2_CX);
        unsigned height = tb_le32(dib + INFO2_CY), planes = tb_le16(dib + INFO2_PLANES);
        color.depth = tb_le16(dib + INFO2_BITCOUNT);
        if (tb_le32(dib) != INFO_HEADER2_SIZE || !color.width || color.width > MAX_ICON_WIDTH ||
            !height || height > MAX_ICON_HEIGHT || height % 2 || planes != 1 ||
            tb_le32(dib + INFO2_COMPRESSION) ||
            (color.depth != 1 && color.depth != 4 && color.depth != 8 && color.depth != 24))
            continue;
        color.height = height / 2;
        color.colors = tb_le32(dib + INFO2_CCLRUSED);
        color.palette_bytes = RGB2_ENTRY_SIZE;
        if (!color.colors && color.depth <= 8)
            color.colors = 1u << color.depth;
        if (color.depth <= 8 ? color.colors > 1u << color.depth : color.colors != 0)
            continue;
        color.header = offset + INFO_HEADER2_SIZE;
        size_t palette = (size_t)color.colors * RGB2_ENTRY_SIZE;
        color.stride = row_stride(color.width, color.depth);
        size_t mask_stride = row_stride(color.width, 1), xor_bytes = color.stride * color.height,
               mask_bytes = mask_stride * color.height;
        if (palette > length - INFO_HEADER2_SIZE ||
            xor_bytes > length - INFO_HEADER2_SIZE - palette ||
            mask_bytes > length - INFO_HEADER2_SIZE - palette - xor_bytes)
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
    /* A Windows .ico starts with reserved zero and type 1. */
    if (size >= ICO_HEADER_SIZE && !tb_le16(bytes) && tb_le16(bytes + 2) == ICO_TYPE_ICON)
        return windows_icon(bytes, size);
    if (tb_le16(bytes) != BFT_BITMAPARRAY)
        return decode_one(bytes, size, 0);
    TbIconImage *best = NULL;
    size_t at = 0;
    for (unsigned i = 0; i < MAX_ARRAY_MEMBERS; i++)
    {
        if (at > size || size - at < MIN_ARRAY_MEMBER_SIZE ||
            tb_le16(bytes + at) != BFT_BITMAPARRAY)
            break;
        TbIconImage *image = decode_one(bytes, size, at + ARRAY_HEADER_SIZE);
        if (image && (!best || image->width * image->height > best->width * best->height))
        {
            tb_icon_free(best);
            best = image;
        }
        else
            tb_icon_free(image);
        uint32_t next = tb_le32(bytes + at + ARRAY_HEADER_NEXT_OFFSET);
        if (!next || next <= at)
            break;
        at = next;
    }
    return best;
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
            /* Inverting pixels (AND 1, XOR non-zero) have no RGBA form; show them half-opaque. */
            rgba[target + 3] = image->mask[source] ? (rgb ? 128 : 0) : 255;
        }
    char *result = tb_base64(rgba, count * 4);
    free(rgba);
    return result;
}
