/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_DIVE_PIXELS_H
#define TWOINE_DIVE_PIXELS_H
#include <stddef.h>
#include <stdint.h>
typedef struct DiveImage
{
    uint32_t format, width, height, stride, chroma_stride, chroma_height;
    size_t bytes, plane[2];
    uint8_t *pixels;
} DiveImage;
extern const uint32_t DiveInputFormats[];
extern const size_t DiveInputFormatCount;
extern const uint32_t DiveOutputFormats[];
extern const size_t DiveOutputFormatCount;
int dive_image_layout(DiveImage *image, uint32_t format, uint32_t width, uint32_t height,
                      uint32_t stride);
uint32_t dive_image_rgb(const DiveImage *image, uint32_t x, uint32_t y, const uint32_t *palette);
uint32_t dive_image_value(const DiveImage *image, uint32_t x, uint32_t y);
uint32_t dive_rgb_yuv(uint32_t rgb);
uint32_t dive_image_yuv(const DiveImage *image, uint32_t x, uint32_t y, uint32_t rgb);
void dive_image_store(DiveImage *image, uint32_t x, uint32_t y, uint32_t rgb,
                      const uint32_t *palette, int dither);
#endif
