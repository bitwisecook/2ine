/* Public domain. No rights reserved. Wholly AI-authored. */
/* FreeType faces are private to one caller; callers serialize access to a font object. */
#include "hal/font.h"
#include "hal/host.h"
#include "hal/host_fs.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LxHostFont
{
    FT_Library library;
    FT_Face face;
    unsigned pixels;
};
static const char *files[] = {"LiberationSans-Regular.ttf",  "LiberationSans-Bold.ttf",
                              "LiberationSans-Italic.ttf",   "LiberationSans-BoldItalic.ttf",
                              "LiberationSerif-Regular.ttf", "LiberationSerif-Bold.ttf",
                              "LiberationSerif-Italic.ttf",  "LiberationSerif-BoldItalic.ttf",
                              "LiberationMono-Regular.ttf",  "LiberationMono-Bold.ttf",
                              "LiberationMono-Italic.ttf",   "LiberationMono-BoldItalic.ttf"};

unsigned lx_host_font_count(void)
{
    return sizeof files / sizeof files[0];
}
static char *font_directory(void)
{
    const char *override = getenv("TWOINE_FONT_DIRECTORY");
    if (override && *override)
        return strdup(override);
    char *executable = lx_host_executable_path();
    if (executable)
    {
        char *slash = strrchr(executable, '/');
        char *backslash = strrchr(executable, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        if (slash)
        {
            slash[1] = 0;
            size_t size = strlen(executable) + 6;
            char *directory = malloc(size);
            if (directory)
            {
                snprintf(directory, size, "%sfonts", executable);
                if (lx_host_path_exists(directory))
                {
                    free(executable);
                    return directory;
                }
                free(directory);
            }
        }
        free(executable);
    }
    return strdup(TWOINE_FONT_DIRECTORY);
}
LxHostFont *lx_host_font_open(unsigned index, unsigned pixels)
{
    if (index >= lx_host_font_count() || !pixels || pixels > 512)
        return NULL;
    char *directory = font_directory();
    if (!directory)
        return NULL;
    size_t length = strlen(directory) + strlen(files[index]) + 2;
    char *path = malloc(length);
    LxHostFont *font = calloc(1, sizeof *font);
    if (!path || !font)
    {
        free(path);
        free(font);
        free(directory);
        return NULL;
    }
    snprintf(path, length, "%s/%s", directory, files[index]);
    free(directory);
    int failed = FT_Init_FreeType(&font->library) ||
                 FT_New_Face(font->library, path, 0, &font->face) ||
                 FT_Select_Charmap(font->face, FT_ENCODING_UNICODE) ||
                 FT_Set_Pixel_Sizes(font->face, 0, pixels);
    free(path);
    if (failed)
    {
        lx_host_font_close(font);
        return NULL;
    }
    font->pixels = pixels;
    return font;
}
void lx_host_font_close(LxHostFont *font)
{
    if (!font)
        return;
    if (font->face)
        FT_Done_Face(font->face);
    if (font->library)
        FT_Done_FreeType(font->library);
    free(font);
}
static int scaled(LxHostFont *font, FT_Pos value)
{
    return (int)FT_MulFix(value, font->face->size->metrics.y_scale) / 64;
}
int lx_host_font_metrics(LxHostFont *font, LxHostFontMetrics *metrics)
{
    if (!font || !metrics)
        return 0;
    memset(metrics, 0, sizeof *metrics);
    FT_Face face = font->face;
    const FT_Size_Metrics *m = &face->size->metrics;
    snprintf(metrics->family, sizeof metrics->family, "%s",
             face->family_name ? face->family_name : "");
    const char *postscript = FT_Get_Postscript_Name(face);
    snprintf(metrics->face, sizeof metrics->face, "%s", postscript ? postscript : metrics->family);
    metrics->bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    metrics->italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
    metrics->fixed = FT_IS_FIXED_WIDTH(face) != 0;
    metrics->em = (int)font->pixels;
    metrics->ascent = (int)(m->ascender + 63) / 64;
    metrics->descent = (int)(-m->descender + 63) / 64;
    metrics->leading = (int)(m->height / 64) - metrics->ascent - metrics->descent;
    if (metrics->leading < 0)
        metrics->leading = 0;
    metrics->maximum_width = (int)(m->max_advance + 63) / 64;
    metrics->underline_size = scaled(font, face->underline_thickness);
    if (metrics->underline_size < 1)
        metrics->underline_size = 1;
    metrics->underline_position = scaled(font, face->underline_position);
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2)
    {
        metrics->average_width = scaled(font, os2->xAvgCharWidth);
        metrics->strikeout_size = scaled(font, os2->yStrikeoutSize);
        metrics->strikeout_position = scaled(font, os2->yStrikeoutPosition);
        if (os2->version >= 2 && os2->version != 0xffff)
            metrics->x_height = scaled(font, os2->sxHeight);
    }
    if (!metrics->average_width)
        metrics->average_width = lx_host_font_advance(font, 'n');
    if (!metrics->x_height && !FT_Load_Char(face, 'x', FT_LOAD_DEFAULT))
        metrics->x_height = (int)(face->glyph->metrics.height + 63) / 64;
    return 1;
}
int lx_host_font_advance(LxHostFont *font, uint32_t codepoint)
{
    if (!font || codepoint > 0x10ffff || FT_Load_Char(font->face, codepoint, FT_LOAD_DEFAULT))
        return -1;
    return (int)(font->face->glyph->advance.x + 32) / 64;
}
int lx_host_font_kerning(LxHostFont *font, uint32_t left, uint32_t right)
{
    if (!font || left > 0x10ffff || right > 0x10ffff)
        return 0;
    FT_Vector delta = {0};
    FT_Get_Kerning(font->face, FT_Get_Char_Index(font->face, left),
                   FT_Get_Char_Index(font->face, right), FT_KERNING_DEFAULT, &delta);
    return (int)(delta.x / 64);
}
int lx_host_font_glyph(LxHostFont *font, uint32_t codepoint, LxHostGlyph *glyph)
{
    if (!glyph)
        return 0;
    memset(glyph, 0, sizeof *glyph);
    if (!font || codepoint > 0x10ffff || FT_Load_Char(font->face, codepoint, FT_LOAD_RENDER))
        return 0;
    FT_GlyphSlot slot = font->face->glyph;
    const FT_Bitmap *bitmap = &slot->bitmap;
    if (bitmap->width > 4096 || bitmap->rows > 4096 || bitmap->pixel_mode != FT_PIXEL_MODE_GRAY)
        return 0;
    glyph->width = (int)bitmap->width;
    glyph->height = (int)bitmap->rows;
    glyph->left = slot->bitmap_left;
    glyph->top = slot->bitmap_top;
    glyph->advance = (int)(slot->advance.x + 32) / 64;
    size_t size = (size_t)glyph->width * glyph->height;
    if (size)
    {
        glyph->coverage = malloc(size);
        if (!glyph->coverage)
            return 0;
        for (int y = 0; y < glyph->height; y++)
            memcpy(glyph->coverage + (size_t)y * glyph->width,
                   bitmap->buffer + (ptrdiff_t)y * bitmap->pitch, glyph->width);
    }
    return 1;
}
void lx_host_glyph_free(LxHostGlyph *glyph)
{
    if (glyph)
    {
        free(glyph->coverage);
        memset(glyph, 0, sizeof *glyph);
    }
}
