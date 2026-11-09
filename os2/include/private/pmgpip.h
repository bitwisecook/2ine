/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Private GPI interfaces shared by 2ine's DLLs: the engine state of presentation spaces (their
   attributes, transforms, paths and saved states) and device contexts, and metafile objects. */
#ifndef _INCL_PMGPIP_H_
#define _INCL_PMGPIP_H_
#include "os2/include/pmgpi.h"
#include "formats/executable/types.h"
#include "emulator/handles.h"
#include "host/font.h"

struct Window; /* pmwinp.h */

/* Wire matrices use six 16.16 coefficients, two integral translations and a
   final integral homogeneous coefficient. */
typedef struct PmGpiState
{
    BOOL initialized;
    BOOL display_enabled, boundary_enabled;
    LONG model[9], view[9];
    LONG arc[4], line_end, line_join, line_width, char_extra, stop_draw;
    RECTL graphics_field, viewing_limits, default_limits, boundary;
    BOOL graphics_set, viewing_set, default_set, boundary_set;
    BOOL viewing_custom, correlate, pick_hit;
    POINTL pick_position;
    SIZEL pick_size;
    ULONG palette;
} PmGpiState;

/* Paths bind to device space when defined; point is refreshed in world space for stroking. */
typedef struct PmPathPoint
{
    POINTL point;
    BOOL move;
    POINTL device;
} PmPathPoint;

typedef struct PmPath
{
    struct PmPath *next;
    LONG id;
    PmPathPoint *points;
    size_t count, capacity;
} PmPath;

typedef struct PresentationSpace
{
    HPS hps;
    PmGpiState gpi;
    PmPath *paths, *record_path;
    ULONG area_options;
    BOOL area_recording;
    LxHandleBorrow palette_lease;
    struct Window *window;
    POINTL position;
    LONG color;
    LONG back_color, mix, back_mix, line_type;
    LONG pattern_set, pattern;
    POINTL pattern_reference;
    LONG primitive_color[5], primitive_back[5];
    LONG primitive_mix[5], primitive_back_mix[5];
    RECTL viewport;
    BOOL viewport_set;
    RECTL paint_clip;
    BOOL paint_clip_set;
    ULONG page_units, ps_options;
    SIZEL page_size;
    struct PmSavedSpace *saved;
    LONG save_id;
    LxHandleBorrow bitmap_lease, clip_lease;
    int persistent;
    ULONG clip_flags; /* PSF_CLIPSIBLINGS and PSF_CLIPCHILDREN requested for this PS. */
    LxHostFont *font;
    unsigned font_index, font_pixels;

    struct
    {
        unsigned index, metric_index, pixels;
        BOOL defined;
    } logical_fonts[255];

    LxHandleBorrow bitmap_ids[255];
    char font_names[255][8];
    POINTL char_angle, char_shear;
    LONG char_direction, text_horizontal, text_vertical;
    LONG charset, char_mode;
    LONG char_width, char_height;
    HDC dc;
    LxHandleBorrow dc_lease;
    ULONG codepage;
    BOOL rgb_colors;
    LONG color_format;
    LONG colors[256];
    uint8 color_valid[256];
} PresentationSpace;

typedef struct PmSavedSpace
{
    struct PmSavedSpace *next;
    LONG id;
    PmGpiState gpi;
    LxHandleBorrow palette_lease;
    POINTL position;
    LONG color, back_color, mix, back_mix, line_type;
    LONG pattern_set, pattern;
    POINTL pattern_reference;
    LONG primitive_color[5], primitive_back[5];
    LONG primitive_mix[5], primitive_back_mix[5];
    POINTL char_angle, char_shear;
    LONG char_direction, text_horizontal, text_vertical;
    LONG charset, char_mode, char_width, char_height;
    RECTL viewport;
    BOOL viewport_set;
    SIZEL page_size;
    BOOL rgb_colors;
    LONG color_format;
    LONG colors[256];
    uint8 color_valid[256];
} PmSavedSpace;

typedef struct PmDeviceContext
{
    void *anchor;
    HDC handle;
    LONG type;
    unsigned spaces;
    unsigned printer_kind;
    ULONG printer_bitmap;
    struct LxPrintJob *print_job;
    unsigned print_pages, print_copies, print_duplex;
    BOOL print_draft;
    struct PmDeviceContext *next;
} PmDeviceContext;

/* A native metafile object holding a copy of the given bytes. */
OS2EXPORT ULONG lx_pm_metafile_create(const void *data, size_t length);
#endif
