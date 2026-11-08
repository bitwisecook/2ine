/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/ps/pmwin/pointer_image.h"

PmPointerImage *pmPointerImage(const uint8_t *bytes, size_t size)
{
    return tb_icon_bitmap(bytes, size);
}

void pmPointerImageFree(PmPointerImage *image)
{
    tb_icon_free(image);
}
