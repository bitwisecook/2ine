/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private graphics engine interfaces: PMMERGE's presentation spaces, regions, bit maps, raster
   access, rasteriser and raster operations, which PMGPI, PMWINX, PMCTLS and the multimedia DLLs
   draw through. */
#ifndef _INCL_PMGREP_H_
#define _INCL_PMGREP_H_
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "host/font.h"
#include "host/window.h"
#include "host/input.h"
#include "host/desktop.h"
#include "emulator/handles.h"
#include "formats/resources/icon.h"
HPOINTER lx_pm_pointer_file(const char *filename);
BOOL lx_pm_pointer_dimensions(HPOINTER handle, LONG *width, LONG *height);
BOOL lx_pm_pointer_draw(HPS space, LONG x, LONG y, HPOINTER handle);
/* Deliver a host event on its window's PM thread, staging it when called elsewhere. */
BOOL lx_pm_host_event(const LxHostEvent *event);

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
struct RGNRECT;
BOOL lx_pm_region_query_rects(HPS ps, ULONG region, PRECTL bounds, struct RGNRECT *control,
                              PRECTL rects);
BOOL lx_pm_window_clip(HPS ps, const RECTL *bounds, PmRegion *visible);
LONG lx_pm_visible_box(HPS ps, RECTL *rectangle);
typedef HWND (*LxPmDialogClassFactory)(HWND parent, const char *name, const char *text, ULONG style,
                                       LONG x, LONG y, LONG width, LONG height, ULONG id);
void lx_pm_set_dialog_class_factory(LxPmDialogClassFactory factory);
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
struct PresentationSpace;

typedef struct PmPalette
{
    ULONG count, colors[];
} PmPalette;

BOOL lx_pm_graphics_clip(struct PresentationSpace *ps, LONG x, LONG y);
void lx_pm_boundary(struct PresentationSpace *ps, LONG x, LONG y, LONG width, LONG height);
/* Intersects a device rectangle with the graphics field and viewing limits. */
void lx_pm_graphics_limits(struct PresentationSpace *ps, RECTL *rect);
/* Returns GPI_HITS once for primitives that crossed the pick aperture, else GPI_OK. */
LONG lx_pm_hits(struct PresentationSpace *ps);
void lx_pm_pick_line(struct PresentationSpace *ps, const POINTL *from, const POINTL *to);
/* Character-extra spacing converted from world units to device pels. */
LONG lx_pm_char_extra(struct PresentationSpace *ps);

/* Bounds on one raster surface or transfer, so that pel buffers stay within 64 MiB. */
enum
{
    LX_PM_RASTER_MAX_DIMENSION = 16384,
    LX_PM_RASTER_MAX_PELS = 16777216
};

typedef struct PmBitmap
{
    void *anchor;
    struct PmBitmap *next;
    ULONG handle;
    unsigned width, height, planes, depth, selected;
    uint32_t *pixels;
    unsigned palette_count;
    uint32_t palette[256];
} PmBitmap;

PmBitmap *lx_pm_bitmap(ULONG handle, LxHandleBorrow *borrow);
ULONG lx_pm_bitmap_create(unsigned width, unsigned height, unsigned planes, unsigned depth);
ULONG lx_pm_bitmap_create_data(HPS ps, PVOID header, ULONG options, PVOID bits, PVOID info);
ULONG lx_pm_bitmap_load(HPS ps, HMODULE module, ULONG id, LONG width, LONG height);
BOOL lx_pm_bitmap_delete(ULONG handle);
ULONG lx_pm_bitmap_select(HPS ps, ULONG bitmap);
void lx_pm_bitmap_cleanup(void *anchor);
void *lx_pm_dive_target(LONG x, LONG y, unsigned width, unsigned height);
void lx_pm_dive_release(void *context);
BOOL lx_pm_dive_flush(void *context);
BOOL lx_pm_dive_read(void *context, LONG x, LONG y, unsigned width, unsigned height,
                     uint32_t *pixels);
BOOL lx_pm_dive_blit(void *context, LONG x, LONG y, unsigned width, unsigned height,
                     const uint32_t *pixels, ULONG count, const RECTL *rectangles);
BOOL lx_pm_raster_read(HPS ps, LONG x, LONG y, unsigned width, unsigned height, uint32_t *pixels);
BOOL lx_pm_raster_write(HPS ps, LONG x, LONG y, unsigned width, unsigned height,
                        const uint32_t *pixels);
struct PresentationSpace;
SIZEL lx_pm_page_dimensions(struct PresentationSpace *ps);
BOOL lx_pm_record_point(struct PresentationSpace *ps, const POINTL *point, BOOL move);
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
/* Reports the lines lx_pm_draw_text would draw, in PM coordinates, without drawing them. */
typedef void (*LxPmTextLine)(void *context, LONG x, LONG baseline, const char *text, LONG length);
LONG lx_pm_draw_text_lines(HPS ps, LONG count, PCH text, PRECTL rect, ULONG flags,
                           LxPmTextLine sink, void *context);
LONG lx_pm_draw_text(HPS ps, LONG count, PCH text, PRECTL rect, LONG foreground, LONG background,
                     ULONG flags, BOOL clip, BOOL opaque_runs);
BOOL lx_pm_valid_ps(HPS hps);
HDC lx_pm_open_dc(HAB anchor, LONG type, HDC compatible);
HDC lx_pm_open_printer_dc(HAB anchor, LONG type, unsigned kind);
unsigned lx_pm_dc_dpi(HDC dc);
LONG lx_pm_display_scale(LONG pixels);
unsigned lx_pm_printer_kind(HDC dc);
BOOL lx_pm_printer_size(HDC dc, unsigned *width, unsigned *height);
BOOL lx_pm_printer_page(HDC dc, unsigned width, unsigned height);
/* Job properties; they apply to the running job and to jobs started later. */
BOOL lx_pm_printer_options(HDC dc, unsigned copies, unsigned duplex, BOOL draft);
LONG lx_pm_print_escape(HDC dc, LONG code, LONG input_count, const void *input, PLONG output_count,
                        void *output);
LONG lx_pm_close_dc(HDC dc);
BOOL lx_pm_dc_valid(HDC dc);
HDC lx_pm_query_dc(HPS ps);
BOOL lx_pm_associate_dc(HPS ps, HDC dc);
BOOL lx_pm_set_cp(HPS ps, ULONG codepage);
ULONG lx_pm_query_cp(HPS ps);
BOOL lx_pm_color_table(HPS ps, ULONG options, LONG format, LONG start, LONG count,
                       const LONG *colors);
LONG lx_pm_resolve_color(HPS ps, LONG color);

/* Scan conversion */

typedef struct PmScanPoint
{
    double x, y;
} PmScanPoint;

enum
{
    /* Even-odd rule: GPI FPATH_ALTERNATE, Win32 ALTERNATE. */
    LX_PM_SCAN_ALTERNATE = 0,
    /* Non-zero rule: GPI FPATH_WINDING, Win32 WINDING. */
    LX_PM_SCAN_WINDING = 1,
    /* GPI inclusive fills (FPATH_INCL) also cover the top scan line and the right pel of each
       span; Win32 fills exclude both. */
    LX_PM_SCAN_INCLUSIVE = 2
};

/* Receives one span of pels [left, right) on row y, left < right. FALSE stops the scan. */
typedef BOOL (*LxPmSpanSink)(void *context, LONG y, LONG left, LONG right);

/* Reports, row by row from the lowest y, the pels whose centres lie inside the contours. The
   contours are consecutive runs of points (counts[i] points each) and close implicitly. A row
   y samples y + 0.5. Rows outside [first_row, end_row) are skipped. FALSE when memory runs out
   or the sink stops. */
BOOL lx_pm_scan_polygons(const PmScanPoint *points, const size_t *counts, size_t contours,
                         unsigned rule, LONG first_row, LONG end_row, LxPmSpanSink sink,
                         void *context);

/* Receives one pel of a line. FALSE stops the walk. */
typedef BOOL (*LxPmPelVisitor)(void *context, LONG x, LONG y);

/* Visits the pels of the Bresenham line from (x0, y0) towards (x1, y1). GPI lines include
   their end point; Win32 LineTo excludes it. FALSE when the visitor stopped the walk. */
BOOL lx_pm_walk_line(LONG x0, LONG y0, LONG x1, LONG y1, BOOL include_end, LxPmPelVisitor visit,
                     void *context);

/* Raster operations */

/* Applies a ternary raster operation (GPI ROP_*, bits 16-23 of a Win32 one) to one pel: bit
   (pattern << 2 | source << 1 | destination) of rop gives each result bit. */
uint32_t lx_pm_rop3(uint32_t pattern, uint32_t source, uint32_t destination, unsigned rop);

/* Whether a raster operation's result depends on the source, or on the pattern. */
static inline BOOL lx_pm_rop_uses_source(unsigned rop)
{
    return ((rop ^ (rop >> 2)) & 0x33) != 0;
}

static inline BOOL lx_pm_rop_uses_pattern(unsigned rop)
{
    return ((rop ^ (rop >> 4)) & 0x0f) != 0;
}

enum
{
    /* The transfer ignores the destination; the pels start as 0. */
    LX_PM_BLIT_NO_READ = 0,
    /* The whole rectangle is read; the transfer fails when it cannot be. */
    LX_PM_BLIT_READ = 1,
    /* GPI: only the part inside the bit map or window is read; the rest starts as 0. */
    LX_PM_BLIT_READ_VISIBLE = 2
};

enum
{
    LX_PM_PEL_KEEP,
    LX_PM_PEL_SET,
    LX_PM_PEL_FAIL
};

/* Computes one pel of a raster transfer at device (x, y). *pel holds the destination; the
   callback stores the result and returns LX_PM_PEL_SET, leaves it (LX_PM_PEL_KEEP), or stops
   the transfer (LX_PM_PEL_FAIL). */
typedef int (*LxPmBlitPel)(void *context, LONG x, LONG y, uint32_t *pel);

/* The read-modify-write raster transfer shared by GPI and Win32 blits. The device rectangle
   [xLeft, xRight) x [yBottom, yTop) is read as requested, every pel passes through pel from
   the bottom row up, and the rectangle is written back through the clip. FALSE when a read,
   allocation, write or pel fails; nothing is written then. */
BOOL lx_pm_blit_rect(HPS ps, const RECTL *rect, unsigned read, LxPmBlitPel pel, void *context);

/* Pointer and icon resource images */
typedef TbIconImage PmPointerImage;
PmPointerImage *pmPointerImage(const uint8_t *bytes, size_t size);
void pmPointerImageFree(PmPointerImage *image);
#endif
