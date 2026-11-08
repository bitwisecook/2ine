/* Public domain. No rights reserved. Wholly AI-authored. */
/* Packed RGB round trips, planar chroma layout, and bounded image extents. */
#include "os2libs/dive.h"
#include "os2libs/dive_pixels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    uint32_t palette[4096];
    for (unsigned n = 0; n < 4096; n++)
        palette[n] = n < 256 ? n * 0x010101u : 0;
    palette[1] = 0xff0000;
    palette[2] = 0x00ff00;
    palette[3] = 0x0000ff;
    for (size_t n = 0; n < DiveOutputFormatCount; n++)
    {
        DiveImage image;
        CHECK(dive_image_layout(&image, DiveOutputFormats[n], 8, 3, 0));
        image.pixels = malloc(image.bytes + 16);
        CHECK(image.pixels);
        memset(image.pixels, 0, image.bytes);
        memset(image.pixels + image.bytes, 0xa5, 16);
        uint32_t colors[] = {0, 0xffffff, 0xff0000, 0x00ff00, 0x0000ff, 0, 0xffffff, 0xff0000};
        if (image.format == DIVE_FOURCC_VGA)
        {
            colors[2] = colors[7] = 0xaa0000;
            colors[3] = 0x00aa00;
            colors[4] = 0x0000aa;
        }
        for (unsigned y = 0; y < 3; y++)
            for (unsigned x = 0; x < 8; x++)
                dive_image_store(&image, x, y, colors[x], palette, 0);
        for (unsigned y = 0; y < 3; y++)
            for (unsigned x = 0; x < 8; x++)
            {
                uint32_t rgb = dive_image_rgb(&image, x, y, palette);
                if (image.format == DIVE_FOURCC_MONO)
                    CHECK(rgb == (x == 1 || x == 3 || x == 6 ? 0xffffff : 0));
                else
                    CHECK(rgb == colors[x]);
            }
        for (unsigned n = 0; n < 16; n++)
            CHECK(image.pixels[image.bytes + n] == 0xa5);
        free(image.pixels);
    }
    const uint32_t planar[] = {DIVE_FOURCC_Y888, DIVE_FOURCC_Y2X2, DIVE_FOURCC_Y4X4,
                               DIVE_FOURCC_YUV9};
    for (unsigned n = 0; n < 4; n++)
    {
        DiveImage image;
        CHECK(dive_image_layout(&image, planar[n], 5, 7, 8));
        image.pixels = malloc(image.bytes);
        CHECK(image.pixels);
        memset(image.pixels, 128, image.bytes);
        CHECK(image.plane[0] == 56 && image.plane[1] > image.plane[0]);
        for (unsigned y = 0; y < 7; y++)
            for (unsigned x = 0; x < 5; x++)
                CHECK(dive_image_rgb(&image, x, y, NULL) == 0x808080);
        image.pixels[image.plane[0]] = 255;
        CHECK((dive_image_rgb(&image, 0, 0, NULL) >> 16) == 255);
        free(image.pixels);
    }
    const uint32_t packed[] = {DIVE_FOURCC_Y422, DIVE_FOURCC_Y42B, DIVE_FOURCC_Y42D};
    for (unsigned n = 0; n < 3; n++)
    {
        DiveImage image;
        CHECK(dive_image_layout(&image, packed[n], 2, 1, 4));
        uint8_t pixels[] = {128, 128, 128, 128};
        image.pixels = pixels;
        CHECK(dive_image_rgb(&image, 0, 0, NULL) == 0x808080);
        CHECK(dive_image_rgb(&image, 1, 0, NULL) == 0x808080);
        CHECK(dive_image_yuv(&image, 0, 0, 0) == 0x808080);
        CHECK(!dive_image_layout(&image, packed[n], 3, 1, 0));
    }
    DiveImage saturated;
    CHECK(dive_image_layout(&saturated, DIVE_FOURCC_Y422, 2, 1, 4));
    uint8_t yuv[] = {0, 0, 0, 255};
    saturated.pixels = yuv;
    CHECK(dive_image_value(&saturated, 0, 0) == 0x0000ff);
    CHECK(dive_rgb_yuv(dive_image_rgb(&saturated, 0, 0, NULL)) != 0x0000ff);
    DiveImage image;
    CHECK(!dive_image_layout(&image, DIVE_FOURCC_RGB4, 3, 1, 11));
    CHECK(!dive_image_layout(&image, DIVE_FOURCC_Y888, 16384, 16384, UINT32_MAX));
    CHECK(!dive_image_layout(&image, 0xdeadbeef, 1, 1, 0));
    puts("DIVE packed RGB/indexed conversions, planar YUV and bounded layouts passed");
    return 0;
}
