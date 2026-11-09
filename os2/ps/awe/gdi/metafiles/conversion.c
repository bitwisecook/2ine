/* Public domain. No rights reserved. Wholly AI-authored. */
/* SetWinMetaFileBits and GetWinMetaFileBits: conversion between Windows and enhanced metafiles. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include <math.h>
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"

/* WMF records are re-expressed as enhanced records, as PMWINX does by replaying the picture into
   an enhanced recording DC. The original bytes also stay in a GDICOMMENT_WINDOWS_METAFILE. */
typedef struct EmfBuilder
{
    uint8 *data;
    size_t size, capacity;
    ULONG records;
    BOOL failed;
} EmfBuilder;

static uint8 *emf_add(EmfBuilder *e, ULONG type, size_t body)
{
    size_t bytes = (8 + body + 3) & ~(size_t)3;
    if (e->failed || bytes > 64u * 1024 * 1024 - e->size)
    {
        e->failed = TRUE;
        return NULL;
    }
    if (e->capacity < e->size + bytes)
    {
        size_t next = e->capacity ? e->capacity * 2 : 4096;
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
    lx_guest_store_u32(p, type);
    lx_guest_store_u32(p + 4, (ULONG)bytes);
    e->size += bytes;
    e->records++;
    return p;
}

static void emf_longs(EmfBuilder *e, ULONG type, unsigned count, const LONG *values)
{
    uint8 *p = emf_add(e, type, (size_t)count * 4);
    for (unsigned i = 0; p && i < count; i++)
        lx_guest_store_u32(p + 8 + i * 4, (ULONG)values[i]);
}

static void emf_stretch(EmfBuilder *e, const uint8 *dib, size_t limit, ULONG usage, ULONG operation,
                        const LONG geometry[8])
{
    size_t info_bytes, pixel_bytes;
    if (limit < 40 ||
        !open32DibBuffers((PVOID)dib, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        info_bytes + pixel_bytes > limit)
        return;
    uint8 *p = emf_add(e, 81, 72 + info_bytes + pixel_bytes);
    if (!p)
        return;
    const unsigned offsets[] = {24, 28, 72, 76, 32, 36, 40, 44};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(p + offsets[i], (ULONG)geometry[i]);
    lx_guest_store_u32(p + 48, 80);
    lx_guest_store_u32(p + 52, (ULONG)info_bytes);
    lx_guest_store_u32(p + 56, 80 + (ULONG)info_bytes);
    lx_guest_store_u32(p + 60, (ULONG)pixel_bytes);
    lx_guest_store_u32(p + 64, usage);
    lx_guest_store_u32(p + 68, operation);
    memcpy(p + 80, dib, info_bytes + pixel_bytes);
}

static void emf_text(EmfBuilder *e, LONG x, LONG y, ULONG options, const uint8 *rectangle,
                     const uint8 *text, ULONG n, const uint8 *advances)
{
    size_t padded = ((size_t)n + 3) & ~(size_t)3;
    uint8 *p = emf_add(e, 83, 68 + padded + (size_t)n * 4);
    if (!p)
        return;
    lx_guest_store_u32(p + 24, 1);
    lx_guest_store_u32(p + 36, (ULONG)x);
    lx_guest_store_u32(p + 40, (ULONG)y);
    lx_guest_store_u32(p + 44, n);
    lx_guest_store_u32(p + 48, 76);
    lx_guest_store_u32(p + 52, options);
    for (unsigned i = 0; rectangle && i < 4; i++)
        lx_guest_store_u32(p + 56 + i * 4, (ULONG)metafile_word(rectangle + i * 2));
    if (n)
        memcpy(p + 76, text, n);
    ULONG dx_at = 76 + (ULONG)padded;
    if (advances)
    {
        lx_guest_store_u32(p + 72, dx_at);
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
    ULONG bytes = lx_guest_load_u32(q) * 2;
    USHORT function = lx_guest_load_u16(q + 4);
    const uint8 *p = q + 6;
    ULONG parameters = bytes - 6;
#define W(i) metafile_word(p + (i) * 2)

    static const struct
    {
        USHORT function;
        uint8 type, words;
    } points[] = {{0x20b, 10, 2}, {0x20c, 9, 2},  {0x20d, 12, 2},
                  {0x20e, 11, 2}, {0x214, 27, 2}, {0x213, 54, 2}};

    for (unsigned i = 0; i < sizeof points / sizeof points[0]; i++)
        if (points[i].function == function && parameters >= 4)
        {
            if (function == 0x20b)
            {
                origin[0] = W(1);
                origin[1] = W(0);
            }
            if (function == 0x20d)
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
    } words[] = {{0x103, 17}, {0x102, 18}, {0x104, 20}, {0x106, 19},
                 {0x107, 21}, {0x12e, 22}, {0x127, 34}};

    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++)
        if (words[i].function == function && parameters >= 2)
        {
            const LONG value = W(0);
            emf_longs(e, words[i].type, 1, &value);
            return;
        }
    switch (function)
    {
        case 0x1e:
            emf_add(e, 33, 0);
            return;
        case 0x35:
            emf_add(e, 52, 0);
            return;
        case 0x201:
        case 0x209:
            if (parameters >= 4)
            {
                const LONG color = (LONG)lx_guest_load_u32(p);
                emf_longs(e, function == 0x201 ? 25 : 24, 1, &color);
            }
            return;
        case 0x20f:
        case 0x211:
            if (parameters >= 4)
            {
                LONG *pair = origin + (function == 0x20f ? 0 : 2);
                pair[0] += W(1);
                pair[1] += W(0);
                emf_longs(e, function == 0x20f ? 10 : 12, 2, pair);
            }
            return;
        case 0x400:
        case 0x412:
            if (parameters >= 8)
            {
                const LONG scale[] = {W(3), W(2), W(1), W(0)};
                emf_longs(e, function == 0x400 ? 32 : 31, 4, scale);
            }
            return;
        case 0x415:
        case 0x416:
        case 0x418:
        case 0x41b:
            if (parameters >= 8)
            {
                const LONG box[] = {W(3), W(2), W(1), W(0)};
                emf_longs(e,
                          function == 0x415   ? 29
                          : function == 0x416 ? 30
                          : function == 0x418 ? 42
                                              : 43,
                          4, box);
            }
            return;
        case 0x61c:
            if (parameters >= 12)
            {
                const LONG box[] = {W(5), W(4), W(3), W(2), W(1), W(0)};
                emf_longs(e, 44, 6, box);
            }
            return;
        case 0x817:
        case 0x81a:
        case 0x830:
            if (parameters >= 16)
            {
                const LONG box[] = {W(7), W(6), W(5), W(4), W(3), W(2), W(1), W(0)};
                emf_longs(e, function == 0x817 ? 45 : function == 0x81a ? 47 : 46, 8, box);
            }
            return;
        case 0x41f:
            if (parameters >= 8)
            {
                const LONG pixel[] = {W(3), W(2), (LONG)lx_guest_load_u32(p)};
                emf_longs(e, 15, 3, pixel);
            }
            return;
        case 0x324:
        case 0x325:
            if (parameters >= 2 && lx_guest_load_u16(p) <= (parameters - 2) / 4)
            {
                ULONG n = lx_guest_load_u16(p);
                uint8 *r = emf_add(e, function == 0x324 ? 3 : 4, 20 + (size_t)n * 8);
                if (r)
                    lx_guest_store_u32(r + 24, n);
                for (ULONG i = 0; r && i < n * 2; i++)
                    lx_guest_store_u32(r + 28 + i * 4, (ULONG)metafile_word(p + 2 + i * 2));
            }
            return;
        case 0x538:
            if (parameters >= 2 && lx_guest_load_u16(p) <= (parameters - 2) / 2)
            {
                ULONG polygons = lx_guest_load_u16(p), total = 0;
                for (ULONG i = 0; i < polygons; i++)
                    total += lx_guest_load_u16(p + 2 + i * 2);
                if (total > (parameters - 2 - polygons * 2) / 4)
                    return;
                uint8 *r = emf_add(e, 8, 24 + (size_t)polygons * 4 + (size_t)total * 8);
                if (!r)
                    return;
                lx_guest_store_u32(r + 24, polygons);
                lx_guest_store_u32(r + 28, total);
                for (ULONG i = 0; i < polygons; i++)
                    lx_guest_store_u32(r + 32 + i * 4, lx_guest_load_u16(p + 2 + i * 2));
                for (ULONG i = 0; i < total * 2; i++)
                    lx_guest_store_u32(r + 32 + polygons * 4 + i * 4,
                                       (ULONG)metafile_word(p + 2 + polygons * 2 + i * 2));
            }
            return;
        case 0x521:
            if (parameters >= 6)
            {
                ULONG n = lx_guest_load_u16(p), padded = (n + 1) & ~1u;
                if (padded + 6 <= parameters)
                    emf_text(e, metafile_word(p + 4 + padded), metafile_word(p + 2 + padded), 0,
                             NULL, p + 2, n, NULL);
            }
            return;
        case 0xa32:
            if (parameters >= 8)
            {
                ULONG n = lx_guest_load_u16(p + 4), options = lx_guest_load_u16(p + 6) & 6;
                ULONG at = options ? 16 : 8, padded = (n + 1) & ~1u;
                if (at <= parameters && padded <= parameters - at)
                    emf_text(e, W(1), W(0), options, options ? p + 8 : NULL, p + at, n,
                             parameters - at - padded >= n * 2 && n ? p + at + padded : NULL);
            }
            return;
        case 0x2fa:
            if (parameters >= 10)
            {
                ULONG slot = emf_slot(slots, slot_count);
                const LONG pen[] = {(LONG)slot, W(0) & 15, W(1), 0, (LONG)lx_guest_load_u32(p + 6)};
                emf_longs(e, 38, 5, pen);
            }
            return;
        case 0x2fc:
            if (parameters >= 8)
            {
                ULONG slot = emf_slot(slots, slot_count);
                const LONG brush[] = {(LONG)slot, (LONG)lx_guest_load_u16(p),
                                      (LONG)lx_guest_load_u32(p + 2),
                                      (LONG)lx_guest_load_u16(p + 6)};
                emf_longs(e, 39, 4, brush);
            }
            return;
        case 0x2fb:
            if (parameters >= 18)
            {
                ULONG slot = emf_slot(slots, slot_count);
                uint8 *r = emf_add(e, 82, 96);
                if (!r)
                    return;
                lx_guest_store_u32(r + 8, slot);
                for (unsigned i = 0; i < 5; i++)
                    lx_guest_store_u32(r + 12 + i * 4, (ULONG)W(i));
                memcpy(r + 32, p + 10, 8);
                for (ULONG i = 0; i < 31 && 18 + i < parameters && p[18 + i]; i++)
                    lx_guest_store_u16(r + 40 + i * 2, p[18 + i]);
            }
            return;
        case 0xf7:
            if (parameters >= 4 && lx_guest_load_u16(p + 2) <= (parameters - 4) / 4)
            {
                ULONG slot = emf_slot(slots, slot_count), n = lx_guest_load_u16(p + 2);
                uint8 *r = emf_add(e, 49, 8 + (size_t)n * 4);
                if (r)
                {
                    lx_guest_store_u32(r + 8, slot);
                    memcpy(r + 12, p, 4 + (size_t)n * 4);
                }
            }
            return;
        case 0x12d:
        case 0x234:
            if (parameters >= 2)
            {
                ULONG index = lx_guest_load_u16(p);
                const LONG object =
                    (LONG)(index & 0x8000 ? 0x80000000u | (index & 0x7fff) : index + 1);
                emf_longs(e, function == 0x12d ? 37 : 48, 1, &object);
            }
            return;
        case 0x1f0:
            if (parameters >= 2 && lx_guest_load_u16(p) < slot_count)
            {
                ULONG index = lx_guest_load_u16(p);
                const LONG object = (LONG)index + 1;
                slots[index] = 0;
                emf_longs(e, 40, 1, &object);
            }
            return;
        case 0x61d:
            if (parameters >= 12)
            {
                uint8 *r = emf_add(e, 76, 92);
                if (!r)
                    return;
                lx_guest_store_u32(r + 24, (ULONG)W(5));
                lx_guest_store_u32(r + 28, (ULONG)W(4));
                lx_guest_store_u32(r + 32, (ULONG)W(3));
                lx_guest_store_u32(r + 36, (ULONG)W(2));
                lx_guest_store_u32(r + 40, lx_guest_load_u32(p));
            }
            return;
        case 0xf43:
            if (parameters >= 22)
            {
                const LONG geometry[] = {W(10), W(9), W(8), W(7), W(6), W(5), W(4), W(3)};
                emf_stretch(e, p + 22, parameters - 22, lx_guest_load_u16(p + 4),
                            lx_guest_load_u32(p), geometry);
            }
            return;
        case 0xb41:
            if (parameters >= 60)
            {
                const LONG geometry[] = {W(9), W(8), W(7), W(6), W(5), W(4), W(3), W(2)};
                emf_stretch(e, p + 20, parameters - 20, 0, lx_guest_load_u32(p), geometry);
            }
            return;
        case 0x940:
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
        open32SetError(13);
        return 0;
    }
    /* The picture's size comes from the WMF's own window, then METAFILEPICT, then the device. */
    LONG window_w = 0, window_h = 0;
    for (ULONG at = 18; at + 6 <= size;)
    {
        const uint8 *record = (const uint8 *)data + at;
        ULONG words = lx_guest_load_u32(record);
        USHORT function = lx_guest_load_u16(record + 4);
        if (!function)
            break;
        if (function == 0x20c && words == 5 && !window_w)
        {
            window_h = metafile_word(record + 6);
            window_w = metafile_word(record + 8);
        }
        at += words * 2;
    }
    window_w = window_w < 0 ? -window_w : window_w;
    window_h = window_h < 0 ? -window_h : window_h;
    LONG frame_w = 0, frame_h = 0;
    if (picture)
    {
        frame_w = (LONG)lx_guest_load_u32((uint8 *)picture + 4);
        frame_h = (LONG)lx_guest_load_u32((uint8 *)picture + 8);
    }
    if (!window_w || !window_h)
    {
        window_w = frame_w > 0 ? (LONG)((int64_t)frame_w * 1024 / 27000) : 1024;
        window_h = frame_h > 0 ? (LONG)((int64_t)frame_h * 768 / 20300) : 768;
    }
    if (frame_w <= 0 || frame_h <= 0)
    {
        frame_w = (LONG)((int64_t)window_w * 27000 / 1024);
        frame_h = (LONG)((int64_t)window_h * 20300 / 768);
    }
    ULONG slot_count = lx_guest_load_u16((const uint8 *)data + 10);
    uint8 *slots = calloc(slot_count ? slot_count : 1, 1);
    EmfBuilder e = {0};
    uint8 *header = slots ? emf_add(&e, 1, 80) : NULL;
    if (!header)
    {
        free(slots);
        open32SetError(8);
        return 0;
    }
    uint8 *comment = emf_add(&e, 70, 28 + (size_t)size);
    if (comment)
    {
        lx_guest_store_u32(comment + 8, size + 24);
        lx_guest_store_u32(comment + 12, 0x43494447);
        lx_guest_store_u32(comment + 16, 0x80000001);
        lx_guest_store_u32(comment + 20, 0x300);
        lx_guest_store_u32(comment + 32, size);
        memcpy(comment + 36, data, size);
    }
    const LONG mode = 8, extent[] = {window_w, window_h};
    emf_longs(&e, 17, 1, &mode);
    emf_longs(&e, 11, 2, extent);
    emf_longs(&e, 9, 2, extent);
    LONG origin[4] = {0};
    for (ULONG at = 18; at + 6 <= size;)
    {
        const uint8 *record = (const uint8 *)data + at;
        if (!lx_guest_load_u16(record + 4))
            break;
        emf_record_from_wmf(&e, record, slots, slot_count, origin);
        at += lx_guest_load_u32(record) * 2;
    }
    uint8 *last = emf_add(&e, 14, 12);
    if (last)
        lx_guest_store_u32(last + 16, 20);
    free(slots);
    ULONG result = 0;
    if (!e.failed)
    {
        uint8 *emf = e.data;
        lx_guest_store_u32(emf + 16, (ULONG)window_w);
        lx_guest_store_u32(emf + 20, (ULONG)window_h);
        lx_guest_store_u32(emf + 32, (ULONG)frame_w);
        lx_guest_store_u32(emf + 36, (ULONG)frame_h);
        lx_guest_store_u32(emf + 40, 0x464d4520);
        lx_guest_store_u32(emf + 44, 0x10000);
        lx_guest_store_u32(emf + 48, (ULONG)e.size);
        lx_guest_store_u32(emf + 52, e.records);
        lx_guest_store_u16(emf + 56, (uint16)(slot_count + 1));
        lx_guest_store_u32(emf + 72, 1024);
        lx_guest_store_u32(emf + 76, 768);
        lx_guest_store_u32(emf + 80, 270);
        lx_guest_store_u32(emf + 84, 203);
        result = SetEnhMetaFileBits((ULONG)e.size, emf);
    }
    else
        open32SetError(8);
    free(e.data);
    return result;
}

ULONG GetWinMetaFileBits(ULONG handle, ULONG capacity, PVOID output, LONG mode, ULONG dc)
{
    LxHandleBorrow borrow = {0};
    if ((dc && !open32FindDC(dc)) || mode < 1 || mode > 8 ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(87);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG result = 0;
    for (ULONG at = 0; at < f->size;)
    {
        const uint8 *p = f->data + at;
        ULONG bytes = lx_guest_load_u32(p + 4);
        if (lx_guest_load_u32(p) == 70 && bytes >= 36 && lx_guest_load_u32(p + 12) == 0x43494447 &&
            lx_guest_load_u32(p + 16) == 0x80000001)
        {
            ULONG n = lx_guest_load_u32(p + 32);
            if (n <= bytes - 36)
            {
                result = !output ? n : capacity < n ? capacity : n;
                if (output)
                    memcpy(output, p + 36, result);
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
