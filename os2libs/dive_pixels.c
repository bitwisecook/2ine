/* Public domain. No rights reserved. Wholly AI-authored. */
/* FOURCC memory layouts use little-endian packed words and Y/Cr/Cb planes. */
#include "os2libs/dive.h"
#include "os2libs/dive_pixels.h"
#include <string.h>

const uint32_t DiveOutputFormats[] = {DIVE_FOURCC_LUT8, DIVE_FOURCC_BGR4, DIVE_FOURCC_R565,
                                      DIVE_FOURCC_R555, DIVE_FOURCC_R664, DIVE_FOURCC_RGB3,
                                      DIVE_FOURCC_BGR3, DIVE_FOURCC_RGB4, DIVE_FOURCC_LT12,
                                      DIVE_FOURCC_MONO, DIVE_FOURCC_VGA};
const size_t DiveOutputFormatCount = sizeof DiveOutputFormats / sizeof DiveOutputFormats[0];
const uint32_t DiveInputFormats[] = {
    DIVE_FOURCC_LUT8, DIVE_FOURCC_BGR4, DIVE_FOURCC_R565, DIVE_FOURCC_R555, DIVE_FOURCC_R664,
    DIVE_FOURCC_RGB3, DIVE_FOURCC_BGR3, DIVE_FOURCC_RGB4, DIVE_FOURCC_LT12, DIVE_FOURCC_MONO,
    DIVE_FOURCC_VGA,  DIVE_FOURCC_Y888, DIVE_FOURCC_Y2X2, DIVE_FOURCC_Y4X4, DIVE_FOURCC_YUV9,
    DIVE_FOURCC_Y644, DIVE_FOURCC_Y422, DIVE_FOURCC_Y42B, DIVE_FOURCC_Y42D, DIVE_FOURCC_Y411};
const size_t DiveInputFormatCount = sizeof DiveInputFormats / sizeof DiveInputFormats[0];

int dive_image_layout(DiveImage *image, uint32_t format, uint32_t width, uint32_t height,
                      uint32_t stride)
{
    if (!image || !width || !height || width > 16384 || height > 16384)
        return 0;
    unsigned bits = 8, subsample = 0;
    switch (format)
    {
        case DIVE_FOURCC_RGB4:
        case DIVE_FOURCC_BGR4:
            bits = 32;
            break;
        case DIVE_FOURCC_RGB3:
        case DIVE_FOURCC_BGR3:
            bits = 24;
            break;
        case DIVE_FOURCC_R565:
        case DIVE_FOURCC_R555:
        case DIVE_FOURCC_R664:
        case DIVE_FOURCC_LT12:
            bits = 16;
            break;
        case DIVE_FOURCC_LUT8:
            break;
        case DIVE_FOURCC_MONO:
            bits = 1;
            break;
        case DIVE_FOURCC_VGA:
            bits = 4;
            break;
        case DIVE_FOURCC_Y888:
            subsample = 1;
            break;
        case DIVE_FOURCC_Y2X2:
            subsample = 2;
            break;
        case DIVE_FOURCC_Y4X4:
        case DIVE_FOURCC_YUV9:
        case DIVE_FOURCC_Y644:
            subsample = 4;
            break;
        case DIVE_FOURCC_Y422:
        case DIVE_FOURCC_Y42B:
        case DIVE_FOURCC_Y42D:
            if (width % 2)
                return 0;
            bits = 16;
            break;
        case DIVE_FOURCC_Y411:
            if (width % 4)
                return 0;
            bits = 16;
            break;
        default:
            return 0;
    }
    uint32_t minimum = (width * bits + 7) / 8;
    if (!stride)
        stride = (minimum + 3) & ~3u;
    if (stride < minimum)
        return 0;
    if ((uint64_t)stride * height > 256u * 1024u * 1024u)
        return 0;
    DiveImage result = {format, width, height, stride, 0, 0, (size_t)stride * height, {0}, NULL};
    if (subsample)
    {
        result.chroma_stride = (stride + subsample - 1) / subsample;
        result.chroma_height = (height + subsample - 1) / subsample;
        result.plane[0] = result.bytes;
        result.plane[1] = result.bytes + (size_t)result.chroma_stride * result.chroma_height;
        result.bytes = result.plane[1];
        if (format != DIVE_FOURCC_Y644)
            result.bytes += (size_t)result.chroma_stride * result.chroma_height;
    }
    /* Allocation and wire borrowing share a bounded extent on both host ABIs. */
    if (result.bytes > 256u * 1024u * 1024u)
        return 0;
    *image = result;
    return 1;
}

static unsigned clamp(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : (unsigned)value;
}

static uint32_t from_yuv(int y, int u, int v)
{
    return clamp(y + v * 1370 / 1000) << 16 | clamp(y - u * 336 / 1000 - v * 698 / 1000) << 8 |
           clamp(y + u * 1730 / 1000);
}

uint32_t dive_rgb_yuv(uint32_t rgb)
{
    int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    int y = (299 * r + 587 * g + 114 * b + 500) / 1000;
    return (uint32_t)y << 16 | clamp(128 + (b - y) * 492 / 1000) << 8 |
           clamp(128 + (r - y) * 877 / 1000);
}

static uint32_t vga(unsigned index)
{
    static const uint32_t colors[16] = {0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa,
                                        0xaa5500, 0xaaaaaa, 0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
                                        0xff5555, 0xff55ff, 0xffff55, 0xffffff};
    return colors[index & 15];
}

uint32_t dive_image_yuv(const DiveImage *i, uint32_t x, uint32_t y, uint32_t rgb)
{
    const uint8_t *p = i->pixels + (size_t)y * i->stride;
    unsigned luma, u, v;
    switch (i->format)
    {
        case DIVE_FOURCC_Y422:
        case DIVE_FOURCC_Y42B:
        case DIVE_FOURCC_Y42D:
        {
            p += (x / 2) * 4;
            unsigned swap = i->format == DIVE_FOURCC_Y42B   ? 1
                            : i->format == DIVE_FOURCC_Y42D ? 3
                                                            : 0;
            luma = p[(x % 2 * 2) ^ swap];
            u = p[1 ^ swap];
            v = p[3 ^ swap];
            break;
        }
        case DIVE_FOURCC_Y888:
        case DIVE_FOURCC_Y2X2:
        case DIVE_FOURCC_Y4X4:
        case DIVE_FOURCC_YUV9:
        case DIVE_FOURCC_Y644:
        {
            unsigned sub = i->format == DIVE_FOURCC_Y888   ? 1
                           : i->format == DIVE_FOURCC_Y2X2 ? 2
                                                           : 4;
            size_t c = (size_t)(y / sub) * i->chroma_stride + x / sub;
            luma = p[x];
            if (i->format == DIVE_FOURCC_Y644)
            {
                unsigned uv = i->pixels[i->plane[0] + c];
                luma = (luma & 63) * 255 / 63;
                u = clamp(128 + ((int)(uv >> 4) - 5) * 6375 / 1000);
                v = clamp(128 + ((int)(uv & 15) - 6) * 7500 / 1000);
            }
            else
            {
                u = i->pixels[i->plane[1] + c];
                v = i->pixels[i->plane[0] + c];
            }
            break;
        }
        case DIVE_FOURCC_Y411:
            p += (x / 4) * 8;
            luma = p[(x % 4) * 2 + 1] & 254;
            u = (p[0] & 0xc0) | (p[2] & 0xc0) >> 2 | (p[4] & 0xc0) >> 4 | (p[6] & 0x80) >> 6;
            v = (p[0] & 0x30) << 2 | (p[2] & 0x30) | (p[4] & 0x30) >> 2 | (p[6] & 0x20) >> 4;
            break;
        default:
            return dive_rgb_yuv(rgb);
    }
    return luma << 16 | u << 8 | v;
}

uint32_t dive_image_value(const DiveImage *i, uint32_t x, uint32_t y)
{
    const uint8_t *row = i->pixels + (size_t)y * i->stride;
    if (i->format == DIVE_FOURCC_LUT8)
        return row[x];
    if (i->format == DIVE_FOURCC_LT12)
        return (row[x * 2] | row[x * 2 + 1] << 8) & 4095;
    if (i->format == DIVE_FOURCC_MONO)
        return (row[x / 8] >> (7 - x % 8)) & 1;
    if (i->format == DIVE_FOURCC_VGA)
        return (row[x / 2] >> (x % 2 ? 0 : 4)) & 15;
    if (i->plane[0] || i->format == DIVE_FOURCC_Y422 || i->format == DIVE_FOURCC_Y42B ||
        i->format == DIVE_FOURCC_Y42D || i->format == DIVE_FOURCC_Y411)
        return dive_image_yuv(i, x, y, 0);
    return dive_image_rgb(i, x, y, NULL);
}

uint32_t dive_image_rgb(const DiveImage *i, uint32_t x, uint32_t y, const uint32_t *palette)
{
    const uint8_t *p = i->pixels + (size_t)y * i->stride;
    uint32_t value;
    unsigned r, g, b;
    switch (i->format)
    {
        case DIVE_FOURCC_LUT8:
        case DIVE_FOURCC_LT12:
            value = dive_image_value(i, x, y);
            return palette ? palette[value] : 0;
        case DIVE_FOURCC_MONO:
            return dive_image_value(i, x, y) ? 0xffffff : 0;
        case DIVE_FOURCC_VGA:
            return vga(dive_image_value(i, x, y));
        case DIVE_FOURCC_RGB3:
        case DIVE_FOURCC_RGB4:
            p += x * (i->format == DIVE_FOURCC_RGB3 ? 3 : 4);
            return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        case DIVE_FOURCC_BGR3:
        case DIVE_FOURCC_BGR4:
            p += x * (i->format == DIVE_FOURCC_BGR3 ? 3 : 4);
            return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
        case DIVE_FOURCC_R565:
        case DIVE_FOURCC_R555:
        case DIVE_FOURCC_R664:
            value = p[x * 2] | p[x * 2 + 1] << 8;
            if (i->format == DIVE_FOURCC_R664)
                r = ((value >> 10) & 63) * 255 / 63, g = ((value >> 4) & 63) * 255 / 63,
                b = (value & 15) * 17;
            else
            {
                unsigned green = i->format == DIVE_FOURCC_R565 ? 63 : 31;
                r = ((value >> (green == 63 ? 11 : 10)) & 31) * 255 / 31;
                g = ((value >> 5) & green) * 255 / green;
                b = (value & 31) * 255 / 31;
            }
            return r << 16 | g << 8 | b;
        case DIVE_FOURCC_Y422:
        case DIVE_FOURCC_Y42B:
        case DIVE_FOURCC_Y42D:
        {
            p += (x / 2) * 4;
            unsigned swap = i->format == DIVE_FOURCC_Y42B   ? 1
                            : i->format == DIVE_FOURCC_Y42D ? 3
                                                            : 0;
            return from_yuv(p[(x % 2 * 2) ^ swap], p[1 ^ swap] - 128, p[3 ^ swap] - 128);
        }
        case DIVE_FOURCC_Y411:
        {
            p += (x / 4) * 8;
            unsigned u =
                (p[0] & 0xc0) | (p[2] & 0xc0) >> 2 | (p[4] & 0xc0) >> 4 | (p[6] & 0x80) >> 6;
            unsigned v =
                (p[0] & 0x30) << 2 | (p[2] & 0x30) | (p[4] & 0x30) >> 2 | (p[6] & 0x20) >> 4;
            return from_yuv(p[(x % 4) * 2 + 1] & 254, (int)u - 128, (int)v - 128);
        }
        default:
        {
            unsigned sub = i->format == DIVE_FOURCC_Y888   ? 1
                           : i->format == DIVE_FOURCC_Y2X2 ? 2
                                                           : 4;
            size_t c = (size_t)(y / sub) * i->chroma_stride + x / sub;
            if (i->format == DIVE_FOURCC_Y644)
            {
                unsigned uv = i->pixels[i->plane[0] + c];
                return from_yuv((p[x] & 63) * 255 / 63, ((int)(uv >> 4) - 5) * 6375 / 1000,
                                ((int)(uv & 15) - 6) * 7500 / 1000);
            }
            return from_yuv(p[x], i->pixels[i->plane[1] + c] - 128,
                            i->pixels[i->plane[0] + c] - 128);
        }
    }
}

void dive_image_store(DiveImage *i, uint32_t x, uint32_t y, uint32_t rgb, const uint32_t *palette,
                      int dither)
{
    uint8_t *p = i->pixels + (size_t)y * i->stride;
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255, value = 0;
    switch (i->format)
    {
        case DIVE_FOURCC_BGR3:
        case DIVE_FOURCC_BGR4:
            p += x * (i->format == DIVE_FOURCC_BGR3 ? 3 : 4);
            p[0] = b;
            p[1] = g;
            p[2] = r;
            if (i->format == DIVE_FOURCC_BGR4)
                p[3] = 0;
            return;
        case DIVE_FOURCC_RGB3:
        case DIVE_FOURCC_RGB4:
            p += x * (i->format == DIVE_FOURCC_RGB3 ? 3 : 4);
            p[0] = r;
            p[1] = g;
            p[2] = b;
            if (i->format == DIVE_FOURCC_RGB4)
                p[3] = 0;
            return;
        case DIVE_FOURCC_R565:
            value = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
            break;
        case DIVE_FOURCC_R555:
            value = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
            break;
        case DIVE_FOURCC_R664:
            value = (r >> 2) << 10 | (g >> 2) << 4 | (b >> 4);
            break;
        case DIVE_FOURCC_MONO:
            value = 1u << (7 - x % 8);
            if (r * 299 + g * 587 + b * 114 >= 128000)
                p[x / 8] |= value;
            else
                p[x / 8] &= ~value;
            return;
        default:
        {
            unsigned count = i->format == DIVE_FOURCC_LT12  ? 4096
                             : i->format == DIVE_FOURCC_VGA ? 16
                                                            : 256;
            unsigned best = UINT32_MAX;
            if (dither && i->format == DIVE_FOURCC_LUT8)
            {
                static const int matrix[4] = {-24, 8, 24, -8};
                int bias = matrix[(y % 2) * 2 + x % 2];
                r = clamp((int)r + bias);
                g = clamp((int)g + bias);
                b = clamp((int)b + bias);
            }
            for (unsigned n = 0; n < count; n++)
            {
                uint32_t c = i->format == DIVE_FOURCC_VGA ? vga(n) : palette[n];
                int dr = (int)r - (int)((c >> 16) & 255), dg = (int)g - (int)((c >> 8) & 255),
                    db = (int)b - (int)(c & 255);
                unsigned distance = dr * dr + dg * dg + db * db;
                if (distance < best)
                {
                    best = distance;
                    value = n;
                }
                if (!distance)
                    break;
            }
            if (i->format == DIVE_FOURCC_LUT8)
            {
                p[x] = value;
                return;
            }
            if (i->format == DIVE_FOURCC_VGA)
            {
                unsigned shift = x % 2 ? 0 : 4;
                p[x / 2] = (p[x / 2] & ~(15u << shift)) | value << shift;
                return;
            }
            break;
        }
    }
    p[x * 2] = value;
    p[x * 2 + 1] = value >> 8;
}
