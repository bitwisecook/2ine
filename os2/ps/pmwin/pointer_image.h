/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_POINTER_IMAGE_H
#define TWOINE_PM_POINTER_IMAGE_H
#include "os2/ps/pmwin/graphics.h"
#include "formats/resources/icon.h"
typedef TbIconImage PmPointerImage;
PmPointerImage *pmPointerImage(const uint8_t *bytes, size_t size);
void pmPointerImageFree(PmPointerImage *image);
#endif
