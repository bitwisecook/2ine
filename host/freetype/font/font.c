/* Public domain. No rights reserved. Wholly AI-authored. */
/* FreeType faces are private to one caller; callers serialize access to a font object. */
#include "host/font.h"
#include "host/system.h"
#include "host/fs.h"
#include "host/sync.h"
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

static const char *warp_files[] = {"HELV.PFB", "HELVB.PFB", "HELVI.PFB", "HELVBI.PFB",
                                   "TNR.PFB",  "TNRB.PFB",  "TNRI.PFB",  "TNRBI.PFB",
                                   "COUR.PFB", "COURB.PFB", "COURI.PFB", "COURBI.PFB"};

typedef struct LoadedFont
{
    char *path;
    unsigned references;
} LoadedFont;

enum
{
    BUNDLED_FONTS = sizeof files / sizeof files[0]
};

static LxHostOnce loaded_once = LX_HOST_ONCE_INIT;
static LxHostMutex *loaded_mutex;
static LoadedFont *loaded;
static unsigned loaded_count;

static void loaded_initialize(void)
{
    loaded_mutex = lx_host_mutex_create(0, 0);
}

static void loaded_lock(void)
{
    lx_host_once(&loaded_once, loaded_initialize);
    lx_host_mutex_lock(loaded_mutex, UINT32_MAX);
}

unsigned lx_host_font_count(void)
{
    loaded_lock();
    unsigned count = BUNDLED_FONTS + loaded_count;
    lx_host_mutex_unlock(loaded_mutex);
    return count;
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

static LxHostFont *open_path(const char *path, unsigned pixels)
{
    LxHostFont *font = calloc(1, sizeof *font);
    if (!font)
        return NULL;
    int failed = FT_Init_FreeType(&font->library) ||
                 FT_New_Face(font->library, path, 0, &font->face) ||
                 FT_Select_Charmap(font->face, FT_ENCODING_UNICODE) ||
                 FT_Set_Pixel_Sizes(font->face, 0, pixels);
    if (failed)
    {
        lx_host_font_close(font);
        return NULL;
    }
    font->pixels = pixels;
    return font;
}

static LxHostFont *open_loaded(unsigned slot, unsigned pixels)
{
    loaded_lock();
    char *path = slot < loaded_count && loaded[slot].path ? strdup(loaded[slot].path) : NULL;
    lx_host_mutex_unlock(loaded_mutex);
    LxHostFont *font = path ? open_path(path, pixels) : NULL;
    free(path);
    return font;
}

LxHostFont *lx_host_font_open(unsigned index, unsigned pixels)
{
    if (!pixels || pixels > 512)
        return NULL;
    if (index >= BUNDLED_FONTS)
        return open_loaded(index - BUNDLED_FONTS, pixels);
    const char *warp_directory = getenv("TWOINE_WARP_FONT_DIRECTORY");
    const int warp = warp_directory && *warp_directory;
    const char *file = warp ? warp_files[index] : files[index];
    char *directory = warp ? strdup(warp_directory) : font_directory();
    if (!directory)
        return NULL;
    size_t length = strlen(directory) + strlen(file) + 2;
    char *path = malloc(length);
    if (!path)
    {
        free(directory);
        return NULL;
    }
    snprintf(path, length, "%s/%s", directory, file);
    free(directory);
    LxHostFont *font = open_path(path, pixels);
    free(path);
    return font;
}

int lx_host_font_add_file(const char *path)
{
    LxHostFont *probe = path ? open_path(path, 16) : NULL;
    if (!probe)
        return 0;
    lx_host_font_close(probe);
    int added = 0;
    loaded_lock();
    for (unsigned i = 0; i < loaded_count && !added; i++)
        if (loaded[i].path && !strcmp(loaded[i].path, path))
        {
            loaded[i].references++;
            added = 1;
        }
    if (!added)
    {
        char *copy = strdup(path);
        LoadedFont *grown = copy ? realloc(loaded, (loaded_count + 1) * sizeof *loaded) : NULL;
        if (grown)
        {
            loaded = grown;
            loaded[loaded_count].path = copy;
            loaded[loaded_count].references = 1;
            loaded_count++;
            added = 1;
        }
        else
            free(copy);
    }
    lx_host_mutex_unlock(loaded_mutex);
    return added;
}

int lx_host_font_remove_file(const char *path)
{
    int removed = 0;
    loaded_lock();
    for (unsigned i = 0; path && i < loaded_count && !removed; i++)
        if (loaded[i].path && !strcmp(loaded[i].path, path))
        {
            removed = 1;
            if (!--loaded[i].references)
            {
                free(loaded[i].path);
                loaded[i].path = NULL;
            }
        }
    lx_host_mutex_unlock(loaded_mutex);
    return removed;
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
    metrics->sfnt = FT_IS_SFNT(face) != 0;
    metrics->em = (int)font->pixels;
    metrics->ascent = (int)(m->ascender + 63) / 64;
    metrics->descent = (int)(-m->descender + 63) / 64;
    metrics->leading = (int)(m->height / 64) - metrics->ascent - metrics->descent;
    if (metrics->leading < 0)
        metrics->leading = 0;
    for (unsigned point = 'a'; point <= 'z'; point++)
    {
        if (FT_Load_Char(face, point, FT_LOAD_DEFAULT))
            return 0;
        FT_Glyph_Metrics *glyph = &face->glyph->metrics;
        int ascent = (int)(glyph->horiBearingY + 63) / 64;
        int descent = (int)(glyph->height - glyph->horiBearingY + 63) / 64;
        if (ascent > metrics->lowercase_ascent)
            metrics->lowercase_ascent = ascent;
        if (descent > metrics->lowercase_descent)
            metrics->lowercase_descent = descent;
    }
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
        metrics->subscript_x_size = scaled(font, os2->ySubscriptXSize);
        metrics->subscript_y_size = scaled(font, os2->ySubscriptYSize);
        metrics->subscript_x_offset = scaled(font, os2->ySubscriptXOffset);
        metrics->subscript_y_offset = scaled(font, os2->ySubscriptYOffset);
        metrics->superscript_x_size = scaled(font, os2->ySuperscriptXSize);
        metrics->superscript_y_size = scaled(font, os2->ySuperscriptYSize);
        metrics->superscript_x_offset = scaled(font, os2->ySuperscriptXOffset);
        metrics->superscript_y_offset = scaled(font, os2->ySuperscriptYOffset);
        memcpy(metrics->panose, os2->panose, sizeof metrics->panose);
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

int lx_host_font_glyph_matrix(LxHostFont *font, uint32_t codepoint, const double matrix[4],
                              LxHostGlyph *glyph)
{
    if (!font || !matrix)
        return 0;
    FT_Matrix transform = {(FT_Fixed)(matrix[0] * 65536), (FT_Fixed)(matrix[1] * 65536),
                           (FT_Fixed)(matrix[2] * 65536), (FT_Fixed)(matrix[3] * 65536)};
    FT_Set_Transform(font->face, &transform, NULL);
    int result = lx_host_font_glyph(font, codepoint, glyph);
    FT_Set_Transform(font->face, NULL, NULL);
    return result;
}

void lx_host_glyph_free(LxHostGlyph *glyph)
{
    if (glyph)
    {
        free(glyph->coverage);
        memset(glyph, 0, sizeof *glyph);
    }
}
