/* Public domain. No rights reserved. Wholly AI-authored. */
/* SetWinMetaFileBits and GetWinMetaFileBits: conversion between Windows and enhanced metafiles. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include <math.h>
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"

/* WMF records are re-expressed as enhanced records, as PMWINX does by replaying the picture into
   an enhanced recording DC. The original bytes also stay in a GDICOMMENT_WINDOWS_METAFILE. */
enum
{
    /* Initial conversion buffer; it doubles as records are added. */
    CONVERSION_INITIAL_CAPACITY = 4096,
    /* Fallback picture: a 1024 x 768 device whose 270 x 203 mm screen gives 0.01 mm frames
       of 27000 x 20300. */
    REFERENCE_DEVICE_WIDTH = 1024,
    REFERENCE_DEVICE_HEIGHT = 768,
    REFERENCE_DEVICE_WIDTH_MM = 270,
    REFERENCE_DEVICE_HEIGHT_MM = 203,
    REFERENCE_FRAME_WIDTH = REFERENCE_DEVICE_WIDTH_MM * 100,
    REFERENCE_FRAME_HEIGHT = REFERENCE_DEVICE_HEIGHT_MM * 100,
    /* ENHMETAHEADER.nVersion of the enhanced metafiles Windows writes. */
    ENHMETA_VERSION = 0x10000,
    /* GDICOMMENT_WINDOWS_METAFILE nVersion: a Windows 3.0 metafile. */
    WINDOWS_METAFILE_VERSION = 0x300,
    /* EMR_STRETCHDIBITS: offsets of its fields, and its fixed part before the DIB. */
    STRETCHDIBITS_INFO_OFFSET = 48,
    STRETCHDIBITS_INFO_BYTES = 52,
    STRETCHDIBITS_BITS_OFFSET = 56,
    STRETCHDIBITS_BITS_BYTES = 60,
    STRETCHDIBITS_USAGE = 64,
    STRETCHDIBITS_OPERATION = 68,
    STRETCHDIBITS_SIZE = 80,
    /* EMR_EXTTEXTOUTA: graphics mode, then an EMRTEXT whose string follows the record. */
    EXTTEXTOUT_GRAPHICS_MODE = 24,
    EXTTEXTOUT_X = 36,
    EXTTEXTOUT_Y = 40,
    EXTTEXTOUT_CHARS = 44,
    EXTTEXTOUT_STRING_OFFSET = 48,
    EXTTEXTOUT_OPTIONS = 52,
    EXTTEXTOUT_RECTANGLE = 56,
    EXTTEXTOUT_ADVANCES_OFFSET = 72,
    EXTTEXTOUT_SIZE = 76
};

typedef struct EmfBuilder
{
    uint8 *data;
    size_t size, capacity;
    ULONG records;
    BOOL failed;
} EmfBuilder;

static uint8 *emf_add(EmfBuilder *e, ULONG type, size_t body)
{
    size_t bytes = (WIN32_EMR_SIZE + body + 3) & ~(size_t)3;
    if (e->failed || bytes > METAFILE_SIZE_MAX - e->size)
    {
        e->failed = TRUE;
        return NULL;
    }
    if (e->capacity < e->size + bytes)
    {
        size_t next = e->capacity ? e->capacity * 2 : CONVERSION_INITIAL_CAPACITY;
        while (next < e->size + bytes)
            next *= 2;
        uint8 *more = realloc(e->data, next);
        if (!more)
        {
            e->failed = TRUE;
            return NULL;
        }
        e->data = more;
        e->capacity = next;
    }
    uint8 *p = e->data + e->size;
    memset(p, 0, bytes);
    lx_guest_store_u32(p + WIN32_EMR_TYPE, type);
    lx_guest_store_u32(p + WIN32_EMR_BYTES, (ULONG)bytes);
    e->size += bytes;
    e->records++;
    return p;
}

static void emf_longs(EmfBuilder *e, ULONG type, unsigned count, const LONG *values)
{
    uint8 *p = emf_add(e, type, (size_t)count * 4);
    for (unsigned i = 0; p && i < count; i++)
        lx_guest_store_u32(p + WIN32_EMR_SIZE + i * 4, (ULONG)values[i]);
}

static void emf_stretch(EmfBuilder *e, const uint8 *dib, size_t limit, ULONG usage, ULONG operation,
                        const LONG geometry[8])
{
    size_t info_bytes, pixel_bytes;
    if (limit < WIN32_BITMAPINFOHEADER_SIZE ||
        !open32DibBuffers((PVOID)dib, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        info_bytes + pixel_bytes > limit)
        return;
    uint8 *p = emf_add(e, WIN32_EMR_STRETCHDIBITS,
                       STRETCHDIBITS_SIZE - WIN32_EMR_SIZE + info_bytes + pixel_bytes);
    if (!p)
        return;
    /* Destination x, y, width, height, then source x, y, width, height. */
    const unsigned offsets[] = {24, 28, 72, 76, 32, 36, 40, 44};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(p + offsets[i], (ULONG)geometry[i]);
    lx_guest_store_u32(p + STRETCHDIBITS_INFO_OFFSET, STRETCHDIBITS_SIZE);
    lx_guest_store_u32(p + STRETCHDIBITS_INFO_BYTES, (ULONG)info_bytes);
    lx_guest_store_u32(p + STRETCHDIBITS_BITS_OFFSET, STRETCHDIBITS_SIZE + (ULONG)info_bytes);
    lx_guest_store_u32(p + STRETCHDIBITS_BITS_BYTES, (ULONG)pixel_bytes);
    lx_guest_store_u32(p + STRETCHDIBITS_USAGE, usage);
    lx_guest_store_u32(p + STRETCHDIBITS_OPERATION, operation);
    memcpy(p + STRETCHDIBITS_SIZE, dib, info_bytes + pixel_bytes);
}

static void emf_text(EmfBuilder *e, LONG x, LONG y, ULONG options, const uint8 *rectangle,
                     const uint8 *text, ULONG n, const uint8 *advances)
{
    size_t padded = ((size_t)n + 3) & ~(size_t)3;
    uint8 *p = emf_add(e, WIN32_EMR_EXTTEXTOUTA,
                       EXTTEXTOUT_SIZE - WIN32_EMR_SIZE + padded + (size_t)n * 4);
    if (!p)
        return;
    lx_guest_store_u32(p + EXTTEXTOUT_GRAPHICS_MODE, WIN32_GM_COMPATIBLE);
    lx_guest_store_u32(p + EXTTEXTOUT_X, (ULONG)x);
    lx_guest_store_u32(p + EXTTEXTOUT_Y, (ULONG)y);
    lx_guest_store_u32(p + EXTTEXTOUT_CHARS, n);
    lx_guest_store_u32(p + EXTTEXTOUT_STRING_OFFSET, EXTTEXTOUT_SIZE);
    lx_guest_store_u32(p + EXTTEXTOUT_OPTIONS, options);
    for (unsigned i = 0; rectangle && i < 4; i++)
        lx_guest_store_u32(p + EXTTEXTOUT_RECTANGLE + i * 4,
                           (ULONG)metafile_word(rectangle + i * 2));
    if (n)
        memcpy(p + EXTTEXTOUT_SIZE, text, n);
    ULONG dx_at = EXTTEXTOUT_SIZE + (ULONG)padded;
    if (advances)
    {
        lx_guest_store_u32(p + EXTTEXTOUT_ADVANCES_OFFSET, dx_at);
        for (ULONG i = 0; i < n; i++)
            lx_guest_store_u32(p + dx_at + i * 4, (ULONG)metafile_word(advances + i * 2));
    }
}

static ULONG emf_slot(uint8 *slots, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        if (!slots[i])
        {
            slots[i] = 1;
            return i + 1;
        }
    return 0;
}

static void emf_record_from_wmf(EmfBuilder *e, const uint8 *q, uint8 *slots, ULONG slot_count,
                                LONG origin[4])
{
    ULONG bytes = lx_guest_load_u32(q + WIN32_METARECORD_WORDS) * 2;
    USHORT function = lx_guest_load_u16(q + WIN32_METARECORD_FUNCTION);
    const uint8 *p = q + WIN32_METARECORD_PARAMETERS;
    ULONG parameters = bytes - WIN32_METARECORD_PARAMETERS;
#define W(i) metafile_word(p + (i) * 2)

    static const struct
    {
        USHORT function;
        uint8 type, words;
    } points[] = {{WIN32_META_SETWINDOWORG, WIN32_EMR_SETWINDOWORGEX, 2},
                  {WIN32_META_SETWINDOWEXT, WIN32_EMR_SETWINDOWEXTEX, 2},
                  {WIN32_META_SETVIEWPORTORG, WIN32_EMR_SETVIEWPORTORGEX, 2},
                  {WIN32_META_SETVIEWPORTEXT, WIN32_EMR_SETVIEWPORTEXTEX, 2},
                  {WIN32_META_MOVETO, WIN32_EMR_MOVETOEX, 2},
                  {WIN32_META_LINETO, WIN32_EMR_LINETO, 2}};

    for (unsigned i = 0; i < sizeof points / sizeof points[0]; i++)
        if (points[i].function == function && parameters >= 4)
        {
            if (function == WIN32_META_SETWINDOWORG)
            {
                origin[0] = W(1);
                origin[1] = W(0);
            }
            if (function == WIN32_META_SETVIEWPORTORG)
            {
                origin[2] = W(1);
                origin[3] = W(0);
            }
            const LONG xy[] = {W(1), W(0)};
            emf_longs(e, points[i].type, 2, xy);
            return;
        }

    static const struct
    {
        USHORT function;
        uint8 type;
    } words[] = {{WIN32_META_SETMAPMODE, WIN32_EMR_SETMAPMODE},
                 {WIN32_META_SETBKMODE, WIN32_EMR_SETBKMODE},
                 {WIN32_META_SETROP2, WIN32_EMR_SETROP2},
                 {WIN32_META_SETPOLYFILLMODE, WIN32_EMR_SETPOLYFILLMODE},
                 {WIN32_META_SETSTRETCHBLTMODE, WIN32_EMR_SETSTRETCHBLTMODE},
                 {WIN32_META_SETTEXTALIGN, WIN32_EMR_SETTEXTALIGN},
                 {WIN32_META_RESTOREDC, WIN32_EMR_RESTOREDC}};

    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
        if (words[i].function == function && parameters >= 2)
        {
            const LONG value = W(0);
            emf_longs(e, words[i].type, 1, &value);
            return;
        }
    switch (function)
    {
        case WIN32_META_SAVEDC:
            emf_add(e, WIN32_EMR_SAVEDC, 0);
            return;
        case WIN32_META_REALIZEPALETTE:
            emf_add(e, WIN32_EMR_REALIZEPALETTE, 0);
            return;
        case WIN32_META_SETBKCOLOR:
        case WIN32_META_SETTEXTCOLOR:
            if (parameters >= 4)
            {
                const LONG color = (LONG)lx_guest_load_u32(p);
                emf_longs(e,
                          function == WIN32_META_SETBKCOLOR ? WIN32_EMR_SETBKCOLOR
                                                            : WIN32_EMR_SETTEXTCOLOR,
                          1, &color);
            }
            return;
        case WIN32_META_OFFSETWINDOWORG:
        case WIN32_META_OFFSETVIEWPORTORG:
            if (parameters >= 4)
            {
                LONG *pair = origin + (function == WIN32_META_OFFSETWINDOWORG ? 0 : 2);
                pair[0] += W(1);
                pair[1] += W(0);
                emf_longs(e,
                          function == WIN32_META_OFFSETWINDOWORG ? WIN32_EMR_SETWINDOWORGEX
                                                                 : WIN32_EMR_SETVIEWPORTORGEX,
                          2, pair);
            }
            return;
        case WMF_SCALEWINDOWEXT_RECORD:
        case WIN32_META_SCALEVIEWPORTEXT:
            if (parameters >= 8)
            {
                const LONG scale[] = {W(3), W(2), W(1), W(0)};
                emf_longs(e,
                          function == WMF_SCALEWINDOWEXT_RECORD ? WIN32_EMR_SCALEWINDOWEXTEX
                                                                : WIN32_EMR_SCALEVIEWPORTEXTEX,
                          4, scale);
            }
            return;
        case WIN32_META_EXCLUDECLIPRECT:
        case WIN32_META_INTERSECTCLIPRECT:
        case WIN32_META_ELLIPSE:
        case WIN32_META_RECTANGLE:
            if (parameters >= 8)
            {
                const LONG box[] = {W(3), W(2), W(1), W(0)};
                emf_longs(e,
                          function == WIN32_META_EXCLUDECLIPRECT     ? WIN32_EMR_EXCLUDECLIPRECT
                          : function == WIN32_META_INTERSECTCLIPRECT ? WIN32_EMR_INTERSECTCLIPRECT
                          : function == WIN32_META_ELLIPSE           ? WIN32_EMR_ELLIPSE
                                                                     : WIN32_EMR_RECTANGLE,
                          4, box);
            }
            return;
        case WIN32_META_ROUNDRECT:
            if (parameters >= 12)
            {
                const LONG box[] = {W(5), W(4), W(3), W(2), W(1), W(0)};
                emf_longs(e, WIN32_EMR_ROUNDRECT, 6, box);
            }
            return;
        case WIN32_META_ARC:
        case WIN32_META_PIE:
        case WIN32_META_CHORD:
            if (parameters >= 16)
            {
                const LONG box[] = {W(7), W(6), W(5), W(4), W(3), W(2), W(1), W(0)};
                emf_longs(e,
                          function == WIN32_META_ARC   ? WIN32_EMR_ARC
                          : function == WIN32_META_PIE ? WIN32_EMR_PIE
                                                       : WIN32_EMR_CHORD,
                          8, box);
            }
            return;
        case WIN32_META_SETPIXEL:
            if (parameters >= 8)
            {
                const LONG pixel[] = {W(3), W(2), (LONG)lx_guest_load_u32(p)};
                emf_longs(e, WIN32_EMR_SETPIXELV, 3, pixel);
            }
            return;
        case WIN32_META_POLYGON:
        case WIN32_META_POLYLINE:
            if (parameters >= 2 && lx_guest_load_u16(p) <= (parameters - 2) / 4)
            {
                ULONG n = lx_guest_load_u16(p);
                uint8 *r = emf_add(
                    e, function == WIN32_META_POLYGON ? WIN32_EMR_POLYGON : WIN32_EMR_POLYLINE,
                    WIN32_EMRPOLYLINE_POINTS - WIN32_EMR_SIZE + (size_t)n * 8);
                if (r)
                    lx_guest_store_u32(r + WIN32_EMRPOLYLINE_COUNT, n);
                for (ULONG i = 0; r && i < n * 2; i++)
                    lx_guest_store_u32(r + WIN32_EMRPOLYLINE_POINTS + i * 4,
                                       (ULONG)metafile_word(p + 2 + i * 2));
            }
            return;
        case WIN32_META_POLYPOLYGON:
            if (parameters >= 2 && lx_guest_load_u16(p) <= (parameters - 2) / 2)
            {
                ULONG polygons = lx_guest_load_u16(p), total = 0;
                for (ULONG i = 0; i < polygons; i++)
                    total += lx_guest_load_u16(p + 2 + i * 2);
                if (total > (parameters - 2 - polygons * 2) / 4)
                    return;
                uint8 *r = emf_add(e, WIN32_EMR_POLYPOLYGON,
                                   WIN32_EMRPOLYPOLYLINE_COUNTS - WIN32_EMR_SIZE +
                                       (size_t)polygons * 4 + (size_t)total * 8);
                if (!r)
                    return;
                lx_guest_store_u32(r + WIN32_EMRPOLYPOLYLINE_POLYGONS, polygons);
                lx_guest_store_u32(r + WIN32_EMRPOLYPOLYLINE_COUNT, total);
                for (ULONG i = 0; i < polygons; i++)
                    lx_guest_store_u32(r + WIN32_EMRPOLYPOLYLINE_COUNTS + i * 4,
                                       lx_guest_load_u16(p + 2 + i * 2));
                for (ULONG i = 0; i < total * 2; i++)
                    lx_guest_store_u32(r + WIN32_EMRPOLYPOLYLINE_COUNTS + polygons * 4 + i * 4,
                                       (ULONG)metafile_word(p + 2 + polygons * 2 + i * 2));
            }
            return;
        case WIN32_META_TEXTOUT:
            if (parameters >= 6)
            {
                ULONG n = lx_guest_load_u16(p), padded = (n + 1) & ~1u;
                if (padded + 6 <= parameters)
                    emf_text(e, metafile_word(p + 4 + padded), metafile_word(p + 2 + padded), 0,
                             NULL, p + 2, n, NULL);
            }
            return;
        case WIN32_META_EXTTEXTOUT:
            if (parameters >= 8)
            {
                ULONG n = lx_guest_load_u16(p + 4),
                      options = lx_guest_load_u16(p + 6) & (WIN32_ETO_OPAQUE | WIN32_ETO_CLIPPED);
                ULONG at = options ? 16 : 8, padded = (n + 1) & ~1u;
                if (at <= parameters && padded <= parameters - at)
                    emf_text(e, W(1), W(0), options, options ? p + 8 : NULL, p + at, n,
                             parameters - at - padded >= n * 2 && n ? p + at + padded : NULL);
            }
            return;
        case WIN32_META_CREATEPENINDIRECT:
            if (parameters >= 10)
            {
                ULONG slot = emf_slot(slots, slot_count);
                const LONG pen[] = {(LONG)slot, W(0) & PS_STYLE_MASK, W(1), 0,
                                    (LONG)lx_guest_load_u32(p + 6)};
                emf_longs(e, WIN32_EMR_CREATEPEN, 5, pen);
            }
            return;
        case WIN32_META_CREATEBRUSHINDIRECT:
            if (parameters >= 8)
            {
                ULONG slot = emf_slot(slots, slot_count);
                const LONG brush[] = {(LONG)slot, (LONG)lx_guest_load_u16(p),
                                      (LONG)lx_guest_load_u32(p + 2),
                                      (LONG)lx_guest_load_u16(p + 6)};
                emf_longs(e, WIN32_EMR_CREATEBRUSHINDIRECT, 4, brush);
            }
            return;
        case WIN32_META_CREATEFONTINDIRECT:
            if (parameters >= WIN32_LOGFONT16_FACE_NAME)
            {
                /* EMR_EXTCREATEFONTINDIRECTW: the handle, then a LOGFONTW whose face name
                   widens the WMF's ANSI characters. */
                ULONG slot = emf_slot(slots, slot_count);
                uint8 *r = emf_add(e, WIN32_EMR_EXTCREATEFONTINDIRECTW,
                                   WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT + WIN32_LOGFONTW_SIZE -
                                       WIN32_EMR_SIZE);
                if (!r)
                    return;
                lx_guest_store_u32(r + WIN32_EMR_SIZE, slot);
                for (unsigned i = 0; i < WIN32_LOGFONT16_ITALIC / 2; i++)
                    lx_guest_store_u32(r + WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT + i * 4,
                                       (ULONG)W(i));
                memcpy(r + WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT + WIN32_LOGFONT_ITALIC,
                       p + WIN32_LOGFONT16_ITALIC, WIN32_LOGFONT_FACE_NAME - WIN32_LOGFONT_ITALIC);
                for (ULONG i = 0;
                     i < WIN32_LF_FACESIZE - 1 && WIN32_LOGFONT16_FACE_NAME + i < parameters &&
                     p[WIN32_LOGFONT16_FACE_NAME + i];
                     i++)
                    lx_guest_store_u16(r + WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT +
                                           WIN32_LOGFONT_FACE_NAME + i * 2,
                                       p[WIN32_LOGFONT16_FACE_NAME + i]);
            }
            return;
        case WIN32_META_CREATEPALETTE:
            if (parameters >= 4 && lx_guest_load_u16(p + 2) <= (parameters - 4) / 4)
            {
                ULONG slot = emf_slot(slots, slot_count), n = lx_guest_load_u16(p + 2);
                uint8 *r = emf_add(e, WIN32_EMR_CREATEPALETTE, 8 + (size_t)n * 4);
                if (r)
                {
                    lx_guest_store_u32(r + 8, slot);
                    memcpy(r + 12, p, 4 + (size_t)n * 4);
                }
            }
            return;
        case WIN32_META_SELECTOBJECT:
        case WIN32_META_SELECTPALETTE:
            if (parameters >= 2)
            {
                ULONG index = lx_guest_load_u16(p);
                const LONG object =
                    (LONG)(index & WMF_STOCK_OBJECT_INDEX
                               ? (ULONG)WIN32_ENHMETA_STOCK_OBJECT | (index & 0x7fff)
                               : index + 1);
                emf_longs(e,
                          function == WIN32_META_SELECTOBJECT ? WIN32_EMR_SELECTOBJECT
                                                              : WIN32_EMR_SELECTPALETTE,
                          1, &object);
            }
            return;
        case WIN32_META_DELETEOBJECT:
            if (parameters >= 2 && lx_guest_load_u16(p) < slot_count)
            {
                ULONG index = lx_guest_load_u16(p);
                const LONG object = (LONG)index + 1;
                slots[index] = 0;
                emf_longs(e, WIN32_EMR_DELETEOBJECT, 1, &object);
            }
            return;
        case WIN32_META_PATBLT:
            if (parameters >= 12)
            {
                uint8 *r = emf_add(e, WIN32_EMR_BITBLT, 92);
                if (!r)
                    return;
                lx_guest_store_u32(r + 24, (ULONG)W(5));
                lx_guest_store_u32(r + 28, (ULONG)W(4));
                lx_guest_store_u32(r + 32, (ULONG)W(3));
                lx_guest_store_u32(r + 36, (ULONG)W(2));
                lx_guest_store_u32(r + 40, lx_guest_load_u32(p));
            }
            return;
        case WIN32_META_STRETCHDIB:
            if (parameters >= 22)
            {
                const LONG geometry[] = {W(10), W(9), W(8), W(7), W(6), W(5), W(4), W(3)};
                emf_stretch(e, p + 22, parameters - 22, lx_guest_load_u16(p + 4),
                            lx_guest_load_u32(p), geometry);
            }
            return;
        case WIN32_META_DIBSTRETCHBLT:
            if (parameters >= 60)
            {
                const LONG geometry[] = {W(9), W(8), W(7), W(6), W(5), W(4), W(3), W(2)};
                emf_stretch(e, p + 20, parameters - 20, 0, lx_guest_load_u32(p), geometry);
            }
            return;
        case WIN32_META_DIBBITBLT:
            if (parameters >= 56)
            {
                const LONG geometry[] = {W(7), W(6), W(5), W(4), W(3), W(2), W(5), W(4)};
                emf_stretch(e, p + 16, parameters - 16, 0, lx_guest_load_u32(p), geometry);
            }
            return;
        default:
            return;
    }
#undef W
}

ULONG SetWinMetaFileBits(ULONG size, PVOID data, ULONG dc, PVOID picture)
{
    if ((dc && !open32FindDC(dc)) || !metafile_valid_wmf(data, size))
    {
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    /* The picture's size comes from the WMF's own window, then METAFILEPICT, then the device. */
    LONG window_w = 0, window_h = 0;
    for (ULONG at = WIN32_METAHEADER_SIZE; at + WIN32_METARECORD_PARAMETERS <= size;)
    {
        const uint8 *record = (const uint8 *)data + at;
        ULONG words = lx_guest_load_u32(record + WIN32_METARECORD_WORDS);
        USHORT function = lx_guest_load_u16(record + WIN32_METARECORD_FUNCTION);
        if (!function)
            break;
        if (function == WIN32_META_SETWINDOWEXT && words == 5 && !window_w)
        {
            window_h = metafile_word(record + WIN32_METARECORD_PARAMETERS);
            window_w = metafile_word(record + WIN32_METARECORD_PARAMETERS + 2);
        }
        at += words * 2;
    }
    window_w = window_w < 0 ? -window_w : window_w;
    window_h = window_h < 0 ? -window_h : window_h;
    LONG frame_w = 0, frame_h = 0;
    if (picture)
    {
        frame_w = (LONG)lx_guest_load_u32((uint8 *)picture + WIN32_METAFILEPICT_X_EXTENT);
        frame_h = (LONG)lx_guest_load_u32((uint8 *)picture + WIN32_METAFILEPICT_Y_EXTENT);
    }
    if (!window_w || !window_h)
    {
        window_w = frame_w > 0
                       ? (LONG)((int64_t)frame_w * REFERENCE_DEVICE_WIDTH / REFERENCE_FRAME_WIDTH)
                       : REFERENCE_DEVICE_WIDTH;
        window_h = frame_h > 0
                       ? (LONG)((int64_t)frame_h * REFERENCE_DEVICE_HEIGHT / REFERENCE_FRAME_HEIGHT)
                       : REFERENCE_DEVICE_HEIGHT;
    }
    if (frame_w <= 0 || frame_h <= 0)
    {
        frame_w = (LONG)((int64_t)window_w * REFERENCE_FRAME_WIDTH / REFERENCE_DEVICE_WIDTH);
        frame_h = (LONG)((int64_t)window_h * REFERENCE_FRAME_HEIGHT / REFERENCE_DEVICE_HEIGHT);
    }
    ULONG slot_count = lx_guest_load_u16((const uint8 *)data + WIN32_METAHEADER_OBJECTS);
    uint8 *slots = calloc(slot_count ? slot_count : 1, 1);
    EmfBuilder e = {0};
    uint8 *header =
        slots ? emf_add(&e, WIN32_EMR_HEADER, WIN32_ENHMETAHEADER_SIZE - WIN32_EMR_SIZE) : NULL;
    if (!header)
    {
        free(slots);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    uint8 *comment =
        emf_add(&e, WIN32_EMR_GDICOMMENT,
                WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA - WIN32_EMR_SIZE + (size_t)size);
    if (comment)
    {
        lx_guest_store_u32(comment + WIN32_EMRGDICOMMENT_DATA_BYTES,
                           size + WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA -
                               WIN32_EMRGDICOMMENT_DATA);
        lx_guest_store_u32(comment + WIN32_EMRGDICOMMENT_DATA, WIN32_GDICOMMENT_IDENTIFIER);
        lx_guest_store_u32(comment + WIN32_EMRGDICOMMENT_TYPE, WIN32_GDICOMMENT_WINDOWS_METAFILE);
        lx_guest_store_u32(comment + WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_VERSION,
                           WINDOWS_METAFILE_VERSION);
        lx_guest_store_u32(comment + WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_BYTES, size);
        memcpy(comment + WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA, data, size);
    }
    const LONG mode = WIN32_MM_ANISOTROPIC, extent[] = {window_w, window_h};
    emf_longs(&e, WIN32_EMR_SETMAPMODE, 1, &mode);
    emf_longs(&e, WIN32_EMR_SETVIEWPORTEXTEX, 2, extent);
    emf_longs(&e, WIN32_EMR_SETWINDOWEXTEX, 2, extent);
    LONG origin[4] = {0};
    for (ULONG at = WIN32_METAHEADER_SIZE; at + WIN32_METARECORD_PARAMETERS <= size;)
    {
        const uint8 *record = (const uint8 *)data + at;
        if (!lx_guest_load_u16(record + WIN32_METARECORD_FUNCTION))
            break;
        emf_record_from_wmf(&e, record, slots, slot_count, origin);
        at += lx_guest_load_u32(record + WIN32_METARECORD_WORDS) * 2;
    }
    uint8 *last = emf_add(&e, WIN32_EMR_EOF, WIN32_EMREOF_SIZE - WIN32_EMR_SIZE);
    if (last)
        lx_guest_store_u32(last + WIN32_EMREOF_SIZE_LAST, WIN32_EMREOF_SIZE);
    free(slots);
    ULONG result = 0;
    if (!e.failed)
    {
        uint8 *emf = e.data;
        /* Handle 0 is reserved for the metafile itself, so the table has one extra slot. */
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_BOUNDS + WIN32_RECT_RIGHT, (ULONG)window_w);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_BOUNDS + WIN32_RECT_BOTTOM, (ULONG)window_h);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_FRAME + WIN32_RECT_RIGHT, (ULONG)frame_w);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_FRAME + WIN32_RECT_BOTTOM, (ULONG)frame_h);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_SIGNATURE, WIN32_ENHMETA_SIGNATURE);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_VERSION, ENHMETA_VERSION);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_FILE_BYTES, (ULONG)e.size);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_RECORDS, e.records);
        lx_guest_store_u16(emf + WIN32_ENHMETAHEADER_HANDLES, (uint16)(slot_count + 1));
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_DEVICE, REFERENCE_DEVICE_WIDTH);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_DEVICE + 4, REFERENCE_DEVICE_HEIGHT);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_MILLIMETERS, REFERENCE_DEVICE_WIDTH_MM);
        lx_guest_store_u32(emf + WIN32_ENHMETAHEADER_MILLIMETERS + 4, REFERENCE_DEVICE_HEIGHT_MM);
        result = SetEnhMetaFileBits((ULONG)e.size, emf);
    }
    else
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    free(e.data);
    return result;
}

ULONG GetWinMetaFileBits(ULONG handle, ULONG capacity, PVOID output, LONG mode, ULONG dc)
{
    LxHandleBorrow borrow = {0};
    if ((dc && !open32FindDC(dc)) || mode < WIN32_MM_TEXT || mode > WIN32_MM_ANISOTROPIC ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG result = 0;
    for (ULONG at = 0; at < f->size;)
    {
        const uint8 *p = f->data + at;
        ULONG bytes = lx_guest_load_u32(p + WIN32_EMR_BYTES);
        if (lx_guest_load_u32(p + WIN32_EMR_TYPE) == WIN32_EMR_GDICOMMENT &&
            bytes >= WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA &&
            lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_DATA) == WIN32_GDICOMMENT_IDENTIFIER &&
            lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_TYPE) == WIN32_GDICOMMENT_WINDOWS_METAFILE)
        {
            ULONG n = lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_BYTES);
            if (n <= bytes - WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA)
            {
                result = !output ? n : capacity < n ? capacity : n;
                if (output)
                    memcpy(output, p + WIN32_EMRGDICOMMENT_WINDOWS_METAFILE_DATA, result);
            }
            break;
        }
        at += bytes;
    }
    lx_handle_release(&borrow);
    if (result)
        return result;
    ULONG recording = CreateMetaFile(NULL);
    if (!recording)
        return 0;
    BOOL played = SetMapMode(recording, mode) && PlayEnhMetaFile(recording, handle, NULL);
    ULONG wmf = CloseMetaFile(recording);
    if (played && wmf)
        result = GetMetaFileBitsEx(wmf, capacity, output);
    if (wmf)
        DeleteMetaFile(wmf);
    return result;
}
