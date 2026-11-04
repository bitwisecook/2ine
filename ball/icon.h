/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_ICON_H
#define TB_ICON_H
#include <stddef.h>
#include <stdint.h>
typedef struct TbIconImage
{
    unsigned width, height;
    uint32_t *pixels;
    uint8_t *mask;
} TbIconImage;
TbIconImage *tb_icon_bitmap(const uint8_t *bytes, size_t size);
TbIconImage *tb_icon_executable(const uint8_t *bytes, size_t size);
TbIconImage *tb_icon_executable_resource(const uint8_t *bytes, size_t size, unsigned id);
/* First ordinal-zero nonresident name: the LX/NE module description. */
char *tb_executable_description(const uint8_t *bytes, size_t size);
void tb_icon_free(TbIconImage *image);
char *tb_icon_rgba_base64(const TbIconImage *image);
char *tb_base64(const uint8_t *bytes, size_t size);
#endif
