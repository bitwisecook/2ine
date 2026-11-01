/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared presentation-space access for PMWIN and PMGPI. No host renderer types. */
#ifndef TWOINE_PM_GRAPHICS_H
#define TWOINE_PM_GRAPHICS_H
#include "os2libs/pmwin.h"
#include "hal/font.h"
#include "guest/handles.h"
typedef struct PmRegion
{
    void *anchor;
    struct PmRegion *next;
    ULONG handle;
    unsigned selected;
    RECTL *rects;
    size_t count;
} PmRegion;
BOOL regionBoolean(PmRegion *out, const PmRegion *a, const PmRegion *b, LONG mode);
BOOL lx_pm_window_clip(HPS ps, const RECTL *bounds, PmRegion *visible);
ULONG lx_pm_region_create(HPS ps, LONG count, const RECTL *rectangles);
BOOL lx_pm_region_destroy(HPS ps, ULONG region);
LONG lx_pm_region_set(HPS ps, ULONG region, LONG count, const RECTL *rectangles);
LONG lx_pm_region_combine(HPS ps, ULONG destination, ULONG first, ULONG second, LONG mode);
LONG lx_pm_region_box(HPS ps, ULONG region, RECTL *rect);
LONG lx_pm_region_offset(HPS ps, ULONG region, const POINTL *point);
LONG lx_pm_set_clip(HPS ps, ULONG region, ULONG *previous);
LONG lx_pm_rect_visible(HPS ps, const RECTL *rect);
BOOL lx_pm_clip_point(HPS ps, LONG x, LONG y);
BOOL lx_pm_mask(HPS ps, LONG x, LONG y, int width, int height, const uint8_t *coverage, LONG rgb,
                const RECTL *bounds);
void lx_pm_region_cleanup(void *anchor);
typedef struct PmBitmap
{
    void *anchor;
    struct PmBitmap *next;
    ULONG handle;
    unsigned width, height, planes, depth, selected;
    uint32_t *pixels;
} PmBitmap;
PmBitmap *lx_pm_bitmap(ULONG handle, LxHandleBorrow *borrow);
ULONG lx_pm_bitmap_create(unsigned width, unsigned height, unsigned planes, unsigned depth);
BOOL lx_pm_bitmap_delete(ULONG handle);
ULONG lx_pm_bitmap_select(HPS ps, ULONG bitmap);
void lx_pm_bitmap_cleanup(void *anchor);
BOOL lx_pm_raster_read(HPS ps, LONG x, LONG y, unsigned width, unsigned height, uint32_t *pixels);
BOOL lx_pm_raster_write(HPS ps, LONG x, LONG y, unsigned width, unsigned height,
                        const uint32_t *pixels);
struct PresentationSpace;
SIZEL lx_pm_page_dimensions(struct PresentationSpace *ps);
BOOL lx_pm_transform_point(struct PresentationSpace *ps, POINTL *point, BOOL to_device);
BOOL lx_pm_convert_points(HPS ps, LONG source, LONG target, LONG count, POINTL *points);
struct PresentationSpace *lx_pm_font_space(HPS hps);
LxHostFont *lx_pm_font(HPS hps);
unsigned lx_pm_font_index(HPS hps);
HPS lx_pm_create_ps(HAB hab, HDC dc);
BOOL lx_pm_erase(HPS hps);
BOOL lx_pm_destroy_ps(HPS hps);
LONG lx_pm_color(HPS hps, LONG color);
BOOL lx_pm_move(HPS hps, const POINTL *point);
LONG lx_pm_line(HPS hps, const POINTL *point, int pel);
uint32_t lx_pm_mix_pixel(uint32_t source, uint32_t destination, LONG mix);
BOOL lx_pm_pattern_pixel(HPS ps, LONG x, LONG y, uint32_t *rgb, BOOL *foreground);
BOOL lx_pm_fill_span(HPS ps, LONG x, LONG y, LONG count, unsigned primitive);
LONG lx_pm_rgb(LONG index);
LONG lx_pm_text(HPS ps, const POINTL *point, LONG count, const char *text, LONG color,
                const RECTL *clip);
LONG lx_pm_draw_text(HPS ps, LONG count, PCH text, PRECTL rect, LONG foreground, LONG background,
                     ULONG flags, BOOL clip, BOOL opaque_runs);
BOOL lx_pm_valid_ps(HPS hps);
HDC lx_pm_open_dc(HAB anchor, LONG type, HDC compatible);
LONG lx_pm_close_dc(HDC dc);
BOOL lx_pm_dc_valid(HDC dc);
HDC lx_pm_query_dc(HPS ps);
BOOL lx_pm_associate_dc(HPS ps, HDC dc);
BOOL lx_pm_set_cp(HPS ps, ULONG codepage);
ULONG lx_pm_query_cp(HPS ps);
BOOL lx_pm_color_table(HPS ps, ULONG options, LONG format, LONG start, LONG count,
                       const LONG *colors);
LONG lx_pm_resolve_color(HPS ps, LONG color);
#endif
