/* Public domain. No rights reserved. Wholly AI-authored. */
/* Font indices identify family/style slots in the selected host catalogue. Each font owns its face and size. */
#ifndef TWOINE_HOST_FONT_H
#define TWOINE_HOST_FONT_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostFont LxHostFont;

typedef struct LxHostFontMetrics
{
    char family[32], face[32];
    unsigned bold, italic, fixed;
    int em, ascent, descent, leading, average_width, maximum_width, x_height;
    int lowercase_ascent, lowercase_descent;
    int underline_size, underline_position, strikeout_size, strikeout_position;
    int subscript_x_size, subscript_y_size, subscript_x_offset, subscript_y_offset;
    int superscript_x_size, superscript_y_size, superscript_x_offset, superscript_y_offset;
    uint8_t panose[10];
    unsigned sfnt;
} LxHostFontMetrics;

typedef struct LxHostGlyph
{
    int width, height, left, top, advance;
    uint8_t *coverage;
} LxHostGlyph;

/* Loaded font files follow the bundled faces. An index stays valid for the
   process: after the last unload, lx_host_font_open fails for that slot. */
unsigned lx_host_font_count(void);
int lx_host_font_add_file(const char *path);
int lx_host_font_remove_file(const char *path);
LxHostFont *lx_host_font_open(unsigned index, unsigned pixels);
void lx_host_font_close(LxHostFont *font);
int lx_host_font_metrics(LxHostFont *font, LxHostFontMetrics *metrics);
int lx_host_font_advance(LxHostFont *font, uint32_t codepoint);
int lx_host_font_kerning(LxHostFont *font, uint32_t left, uint32_t right);
int lx_host_font_glyph(LxHostFont *font, uint32_t codepoint, LxHostGlyph *glyph);
/* Renders through the 2x2 matrix {xx, xy, yx, yy}, in y-up glyph space. */
int lx_host_font_glyph_matrix(LxHostFont *font, uint32_t codepoint, const double matrix[4],
                              LxHostGlyph *glyph);
void lx_host_glyph_free(LxHostGlyph *glyph);
#endif
