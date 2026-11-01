/* Public domain. No rights reserved. Wholly AI-authored. */
/* Font indices identify family/style slots in the selected host catalogue. Each font owns its face and size. */
#ifndef TWOINE_HAL_FONT_H
#define TWOINE_HAL_FONT_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostFont LxHostFont;
typedef struct LxHostFontMetrics
{
    char family[32], face[32];
    unsigned bold, italic, fixed;
    int em, ascent, descent, leading, average_width, maximum_width, x_height;
    int underline_size, underline_position, strikeout_size, strikeout_position;
} LxHostFontMetrics;
typedef struct LxHostGlyph
{
    int width, height, left, top, advance;
    uint8_t *coverage;
} LxHostGlyph;
unsigned lx_host_font_count(void);
LxHostFont *lx_host_font_open(unsigned index, unsigned pixels);
void lx_host_font_close(LxHostFont *font);
int lx_host_font_metrics(LxHostFont *font, LxHostFontMetrics *metrics);
int lx_host_font_advance(LxHostFont *font, uint32_t codepoint);
int lx_host_font_kerning(LxHostFont *font, uint32_t left, uint32_t right);
int lx_host_font_glyph(LxHostFont *font, uint32_t codepoint, LxHostGlyph *glyph);
void lx_host_glyph_free(LxHostGlyph *glyph);
#endif
