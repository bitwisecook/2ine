/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_POINTER_IMAGE_H
#define TWOINE_PM_POINTER_IMAGE_H
#include "os2libs/pmwin/graphics.h"
typedef struct PmPointerImage
{
    unsigned width, height;
    uint32 *pixels;
    uint8 *mask;
} PmPointerImage;
PmPointerImage *pmPointerImage(const uint8 *bytes, size_t size);
void pmPointerImageFree(PmPointerImage *image);
#endif
