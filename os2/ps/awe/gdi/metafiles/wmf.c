/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows metafiles: handles, validation, playback, enumeration and metafile metafile_files. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include <math.h>
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"

LxHostOnce metafile_once = LX_HOST_ONCE_INIT;

LxHostMonitor *metafile_monitor;

Open32Metafile *metafile_files;

void metafile_initialize(void)
{
    metafile_monitor = lx_host_monitor_create();
}

void metafile_destroy_file(void *resource)
{
    Open32Metafile *file = resource;
    lx_host_monitor_lock(metafile_monitor);
    Open32Metafile **slot = &metafile_files;
    while (*slot && *slot != file)
        slot = &(*slot)->next;
    if (*slot)
        *slot = file->next;
    lx_host_monitor_unlock(metafile_monitor);
    free(file);
}

BOOL metafile_valid_wmf(const UCHAR *data, ULONG size)
{
    if (!data || size < 24 || size > 64u * 1024 * 1024 || (size & 1))
        return FALSE;
    USHORT type = lx_guest_load_u16(data), version = lx_guest_load_u16(data + 4);
    if ((type != 1 && type != 2) || lx_guest_load_u16(data + 2) != 9 ||
        (version != 0x100 && version != 0x300) || lx_guest_load_u32(data + 6) != size / 2 ||
        lx_guest_load_u16(data + 16))
        return FALSE;
    ULONG maximum = lx_guest_load_u32(data + 12);
    if (maximum < 3 || maximum > (size - 18) / 2)
        return FALSE;
    for (ULONG offset = 18; offset <= size - 6;)
    {
        ULONG words = lx_guest_load_u32(data + offset);
        USHORT function = lx_guest_load_u16(data + offset + 4);
        if (words < 3 || words > maximum || words > (size - offset) / 2)
            return FALSE;
        if (!function)
            return words == 3 && offset + 6 == size;
        offset += words * 2;
    }
    return FALSE;
}

ULONG SetMetaFileBitsEx(ULONG size, PVOID data)
{
    TRACE_NATIVE("SetMetaFileBitsEx(%u, %p)", size, data);
    if (!data || size < 24 || size > 64u * 1024 * 1024 || (size & 1))
    {
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    lx_host_once(&metafile_once, metafile_initialize);
    Open32Metafile *file = metafile_monitor ? malloc(sizeof *file + size) : NULL;
    if (!file)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    file->size = size;
    file->enhanced = FALSE;
    memcpy(file->data, data, size);
    /* Validate the owned snapshot: another guest thread can reuse its input
       buffer as soon as it has entered this API. */
    if (!metafile_valid_wmf(file->data, size))
    {
        free(file);
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    lx_host_monitor_lock(metafile_monitor);
    file->handle = lx_handle_create(LX_HANDLE_OPEN32_METAFILE, file, metafile_destroy_file);
    if (file->handle)
    {
        file->next = metafile_files;
        metafile_files = file;
    }
    lx_host_monitor_unlock(metafile_monitor);
    ULONG handle = file->handle;
    if (!handle)
    {
        free(file);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return handle;
}

ULONG GetMetaFileBitsEx(ULONG handle, ULONG size, PVOID data)
{
    TRACE_NATIVE("GetMetaFileBitsEx(%u, %u, %p)", handle, size, data);
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    Open32Metafile *file = borrow.resource;
    ULONG result = data && size < file->size ? size : file->size;
    if (data)
        memcpy(data, file->data, result);
    lx_handle_release(&borrow);
    return result;
}

BOOL DeleteMetaFile(ULONG handle)
{
    TRACE_NATIVE("DeleteMetaFile(%u)", handle);
    if (lx_handle_close(handle, LX_HANDLE_OPEN32_METAFILE))
        return TRUE;
    open32SetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

LONG metafile_word(const UCHAR *bytes)
{
    return (SHORT)lx_guest_load_u16(bytes);
}

static BOOL wmf_dib(ULONG dc, const UCHAR *dib, ULONG available, LONG x, LONG y, LONG width,
                    LONG height, LONG source_x, LONG source_y, LONG source_width,
                    LONG source_height, ULONG usage, ULONG operation)
{
    size_t info_bytes, pixel_bytes;
    if (available < 40 ||
        !open32DibBuffers((PVOID)dib, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        !pixel_bytes || info_bytes > available || pixel_bytes > available - info_bytes)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return StretchDIBits(dc, x, y, width, height, source_x, source_y, source_width, source_height,
                         (PVOID)(dib + info_bytes), (PVOID)dib, usage, operation) != 0;
}

/* META_CREATEREGION: a Region object whose scans each list x pairs for one band. */
static ULONG wmf_region(const UCHAR *p, ULONG size)
{
    ULONG scans = lx_guest_load_u16(p + 10), at = 22;
    ULONG region = CreateRectRgn(0, 0, 0, 0), part = region ? CreateRectRgn(0, 0, 0, 0) : 0;
    BOOL ok = part != 0;
    for (ULONG i = 0; ok && i < scans; i++)
    {
        if (at + 6 > size)
        {
            ok = FALSE;
            break;
        }
        ULONG values = lx_guest_load_u16(p + at);
        LONG top = metafile_word(p + at + 2), bottom = metafile_word(p + at + 4);
        if (at + 8 + (size_t)values * 2 > size)
        {
            ok = FALSE;
            break;
        }
        for (ULONG v = 0; ok && v + 1 < values; v += 2)
            ok = SetRectRgn(part, metafile_word(p + at + 6 + v * 2), top,
                            metafile_word(p + at + 8 + v * 2), bottom) &&
                 CombineRgn(region, region, part, 2);
        at += 8 + values * 2;
    }
    if (part)
        DeleteObject(part);
    if (!ok && region)
    {
        DeleteObject(region);
        region = 0;
    }
    return region;
}

static BOOL wmf_text(ULONG dc, const UCHAR *p, ULONG bytes)
{
    ULONG n = lx_guest_load_u16(p + 4), options = lx_guest_load_u16(p + 6) & 6;
    ULONG text_at = options ? 16 : 8, padded = (n + 1) & ~1u, parameters = bytes - 6;
    if (text_at > parameters || padded > parameters - text_at)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    uint8 rectangle[16];
    for (unsigned i = 0; options && i < 4; i++)
        lx_guest_store_u32(rectangle + i * 4, (ULONG)metafile_word(p + 8 + i * 2));
    uint8 *advances = NULL;
    if (parameters - text_at - padded >= n * 2 && n)
    {
        advances = malloc((size_t)n * 4);
        if (!advances)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        for (ULONG i = 0; i < n; i++)
            lx_guest_store_u32(advances + i * 4,
                               (ULONG)metafile_word(p + text_at + padded + i * 2));
    }
    BOOL result = ExtTextOut(dc, metafile_word(p + 2), metafile_word(p), options,
                             options ? rectangle : NULL, (PCHAR)(p + text_at), n, (PLONG)advances);
    free(advances);
    return result;
}

static BOOL play_record(ULONG dc, const UCHAR *record, PVOID table, ULONG count)
{
    ULONG bytes = lx_guest_load_u32(record) * 2;
    USHORT function = lx_guest_load_u16(record + 4);
    const UCHAR *p = record + 6;
    if (!function)
        return TRUE;
    if (function == 0x103 && bytes == 8)
        return SetMapMode(dc, metafile_word(p)) != 0;
    if (function == 0x20b && bytes == 10)
        return SetWindowOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == 0x20c && bytes == 10)
        return SetWindowExtEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == 0xf43 && bytes >= 68)
    {
        /* META_STRETCHDIB: DWORD raster operation, WORD usage, eight signed
           coordinates in reverse call order, then BITMAPINFO and pixel bits. */
        ULONG usage = lx_guest_load_u16(p + 4);
        size_t info_bytes, pixel_bytes;
        PVOID info = (PVOID)(p + 22);
        if (!open32DibBuffers(info, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
            !pixel_bytes || info_bytes > bytes - 28 || pixel_bytes > bytes - 28 - info_bytes)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return StretchDIBits(dc, metafile_word(p + 20), metafile_word(p + 18),
                             metafile_word(p + 16), metafile_word(p + 14), metafile_word(p + 12),
                             metafile_word(p + 10), metafile_word(p + 8), metafile_word(p + 6),
                             (UCHAR *)info + info_bytes, info, usage, lx_guest_load_u32(p)) != 0;
    }
    if (function == 0x102 && bytes == 8)
        return SetBkMode(dc, metafile_word(p)) != 0;
    if (function == 0x104 && bytes == 8)
        return SetROP2(dc, metafile_word(p)) != 0;
    if (function == 0x106 && bytes == 8)
        return SetPolyFillMode(dc, metafile_word(p)) != 0;
    if (function == 0x107 && bytes == 8)
        return SetStretchBltMode(dc, metafile_word(p)) != 0;
    if (function == 0x108 && bytes == 8)
        return SetTextCharacterExtra(dc, metafile_word(p)) != INT32_MIN;
    if (function == 0x201 && bytes == 10)
        return SetBkColor(dc, lx_guest_load_u32(p)) != UINT32_MAX;
    if (function == 0x209 && bytes == 10)
        return SetTextColor(dc, lx_guest_load_u32(p)) != UINT32_MAX;
    if (function == 0x20a && bytes == 10)
        return SetTextJustification(dc, metafile_word(p + 2), metafile_word(p));
    if (function == 0x20d && bytes == 10)
        return SetViewportOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == 0x20e && bytes == 10)
        return SetViewportExtEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == 0x214 && bytes == 10)
        return MoveToEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == 0x213 && bytes == 10)
        return LineTo(dc, metafile_word(p + 2), metafile_word(p));
    if (function == 0x1e && bytes == 6)
        return SaveDC(dc) != 0;
    if (function == 0x127 && bytes == 8)
        return RestoreDC(dc, metafile_word(p));
    if ((function == 0x418 || function == 0x41b) && bytes == 14)
        return function == 0x418 ? Ellipse(dc, metafile_word(p + 6), metafile_word(p + 4),
                                           metafile_word(p + 2), metafile_word(p))
                                 : Rectangle(dc, metafile_word(p + 6), metafile_word(p + 4),
                                             metafile_word(p + 2), metafile_word(p));
    if (function == 0x61c && bytes == 18)
        return RoundRect(dc, metafile_word(p + 10), metafile_word(p + 8), metafile_word(p + 6),
                         metafile_word(p + 4), metafile_word(p + 2), metafile_word(p));
    if (function == 0x830 && bytes == 22)
        return open32Chord(dc, metafile_word(p + 14), metafile_word(p + 12), metafile_word(p + 10),
                           metafile_word(p + 8), metafile_word(p + 6), metafile_word(p + 4),
                           metafile_word(p + 2), metafile_word(p));
    if ((function == 0x817 || function == 0x81a) && bytes == 22)
        return function == 0x817
                   ? Arc(dc, metafile_word(p + 14), metafile_word(p + 12), metafile_word(p + 10),
                         metafile_word(p + 8), metafile_word(p + 6), metafile_word(p + 4),
                         metafile_word(p + 2), metafile_word(p))
                   : Pie(dc, metafile_word(p + 14), metafile_word(p + 12), metafile_word(p + 10),
                         metafile_word(p + 8), metafile_word(p + 6), metafile_word(p + 4),
                         metafile_word(p + 2), metafile_word(p));
    if ((function == 0x324 || function == 0x325) && bytes >= 8)
    {
        ULONG n = lx_guest_load_u16(p);
        if (n > (bytes - 8) / 4)
            return FALSE;
        uint8 *points = malloc((size_t)n * 8);
        if (!points)
            return FALSE;
        for (ULONG i = 0; i < n; i++)
        {
            lx_guest_store_u32(points + i * 8, (ULONG)metafile_word(p + 2 + i * 4));
            lx_guest_store_u32(points + i * 8 + 4, (ULONG)metafile_word(p + 4 + i * 4));
        }
        BOOL ok = function == 0x324 ? Polygon(dc, points, (LONG)n) : Polyline(dc, points, (LONG)n);
        free(points);
        return ok;
    }
    if (function == 0x538 && bytes >= 8)
    {
        ULONG polygons = lx_guest_load_u16(p);
        if (polygons > (bytes - 8) / 2)
            return FALSE;
        ULONG total = 0;
        for (ULONG i = 0; i < polygons; i++)
            total += lx_guest_load_u16(p + 2 + i * 2);
        if (total > 65536 || total > (bytes - 8 - polygons * 2) / 4)
            return FALSE;
        uint8 *counts = malloc((size_t)polygons * 4), *points = malloc((size_t)total * 8);
        if (!counts || !points)
        {
            free(counts);
            free(points);
            return FALSE;
        }
        for (ULONG i = 0; i < polygons; i++)
            lx_guest_store_u32(counts + i * 4, lx_guest_load_u16(p + 2 + i * 2));
        for (ULONG i = 0; i < total; i++)
        {
            lx_guest_store_u32(points + i * 8, (ULONG)metafile_word(p + 2 + polygons * 2 + i * 4));
            lx_guest_store_u32(points + i * 8 + 4,
                               (ULONG)metafile_word(p + 4 + polygons * 2 + i * 4));
        }
        BOOL ok = PolyPolygon(dc, points, counts, (LONG)polygons);
        free(points);
        free(counts);
        return ok;
    }
    if (function == 0x521 && bytes >= 12)
    {
        ULONG n = lx_guest_load_u16(p), padded = (n + 1) & ~1u;
        if (padded > bytes - 12)
            return FALSE;
        return TextOut(dc, metafile_word(p + 4 + padded), metafile_word(p + 2 + padded),
                       (PSZ)(p + 2), (LONG)n);
    }
    if (function == 0x12d && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p),
              object = index & 0x8000  ? GetStockObject((LONG)(index & 0x7fff))
                       : index < count ? lx_guest_load_u32((uint8 *)table + index * 4)
                                       : 0;
        return object && SelectObject(dc, object) != 0;
    }
    if (function == 0x1f0 && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p);
        if (index >= count)
            return FALSE;
        ULONG object = lx_guest_load_u32((uint8 *)table + index * 4);
        if (!object || !DeleteObject(object))
            return FALSE;
        lx_guest_store_u32((uint8 *)table + index * 4, 0);
        return TRUE;
    }
    if (function == 0x2fa || function == 0x2fc || function == 0x2fb || function == 0xf7 ||
        function == 0x142 || function == 0x6ff)
    {
        ULONG object = 0;
        if (function == 0x6ff && bytes >= 28)
            object = wmf_region(p, bytes - 6);
        else if (function == 0x142 && bytes >= 50)
        {
            size_t info_bytes, pixel_bytes;
            ULONG usage = lx_guest_load_u16(p + 2);
            if (open32DibBuffers((PVOID)(p + 4), 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) &&
                info_bytes + pixel_bytes <= bytes - 10)
                object = open32DibPatternBrush((PVOID)(p + 4), (PVOID)(p + 4 + info_bytes), usage,
                                               lx_guest_load_u16(p) == 3);
        }
        else if (function == 0xf7 && bytes >= 10 && lx_guest_load_u16(p + 2) <= (bytes - 10) / 4)
            object = CreatePalette((PVOID)p);
        else if (function == 0x2fa && bytes == 16)
            object =
                CreatePen(metafile_word(p) & 15, metafile_word(p + 2), lx_guest_load_u32(p + 6));
        else if (function == 0x2fc && bytes == 14)
        {
            uint8 brush[12];
            lx_guest_store_u32(brush, lx_guest_load_u16(p));
            memcpy(brush + 4, p + 2, 4);
            lx_guest_store_u32(brush + 8, lx_guest_load_u16(p + 6));
            object = CreateBrushIndirect(brush);
        }
        else if (function == 0x2fb && bytes >= 24)
        {
            /* Records may end soon after the face name's terminator. */
            uint8 font[60] = {0};
            for (unsigned i = 0; i < 5; i++)
                lx_guest_store_u32(font + i * 4, (ULONG)metafile_word(p + i * 2));
            memcpy(font + 20, p + 10, bytes - 16 < 40 ? bytes - 16 : 40);
            font[59] = 0;
            object = CreateFontIndirect(font);
        }
        if (!object)
            return FALSE;
        for (ULONG i = 0; i < count; i++)
            if (!lx_guest_load_u32((uint8 *)table + i * 4))
            {
                lx_guest_store_u32((uint8 *)table + i * 4, object);
                return TRUE;
            }
        DeleteObject(object);
        return FALSE;
    }
    if (function == 0x41f && bytes == 14)
        return SetPixel(dc, metafile_word(p + 6), metafile_word(p + 4), lx_guest_load_u32(p)) !=
               UINT32_MAX;
    if (function == 0x61d && bytes == 18)
        return PatBlt(dc, metafile_word(p + 10), metafile_word(p + 8), metafile_word(p + 6),
                      metafile_word(p + 4), lx_guest_load_u32(p));
    if (function == 0x12e && bytes == 8)
        return SetTextAlign(dc, lx_guest_load_u16(p)) != UINT32_MAX;
    if ((function == 0x20f || function == 0x211) && bytes == 10)
        return function == 0x20f
                   ? OffsetWindowOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL)
                   : OffsetViewportOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if ((function == 0x400 || function == 0x412) && bytes == 14)
        return function == 0x400
                   ? ScaleWindowExtEx(dc, metafile_word(p + 6), metafile_word(p + 4),
                                      metafile_word(p + 2), metafile_word(p), NULL)
                   : ScaleViewportExtEx(dc, metafile_word(p + 6), metafile_word(p + 4),
                                        metafile_word(p + 2), metafile_word(p), NULL);
    if ((function == 0x415 || function == 0x416) && bytes == 14)
        return (function == 0x415
                    ? ExcludeClipRect(dc, metafile_word(p + 6), metafile_word(p + 4),
                                      metafile_word(p + 2), metafile_word(p))
                    : IntersectClipRect(dc, metafile_word(p + 6), metafile_word(p + 4),
                                        metafile_word(p + 2), metafile_word(p))) != 0;
    if (function == 0xa32 && bytes >= 14)
        return wmf_text(dc, p, bytes);
    if (function == 0x234 && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p);
        ULONG object = index < count ? lx_guest_load_u32((uint8 *)table + index * 4) : 0;
        return object && SelectPalette(dc, object, FALSE) != 0;
    }
    if ((function == 0x940 || function == 0xb41) && bytes >= 20)
    {
        ULONG head = function == 0x940 ? 16 : 20, parameters = bytes - 6;
        ULONG operation = lx_guest_load_u32(p);
        /* Without a DIB the record is a pattern transfer ending in h, w, y and x. */
        if (parameters < head + 40)
        {
            const UCHAR *q = p + parameters - 8;
            return PatBlt(dc, metafile_word(q + 6), metafile_word(q + 4), metafile_word(q + 2),
                          metafile_word(q), operation);
        }
        const UCHAR *q = p + head - 8, *source = p + 4;
        LONG height = metafile_word(q), width = metafile_word(q + 2);
        LONG source_height = function == 0x940 ? height : metafile_word(source);
        LONG source_width = function == 0x940 ? width : metafile_word(source + 2);
        const UCHAR *origin = function == 0x940 ? source : source + 4;
        return wmf_dib(dc, p + head, parameters - head, metafile_word(q + 6), metafile_word(q + 4),
                       width, height, metafile_word(origin + 2), metafile_word(origin),
                       source_width, source_height, 0, operation);
    }
    if (function == 0xd33 && bytes >= 24 + 40)
    {
        size_t info_bytes, pixel_bytes;
        ULONG usage = lx_guest_load_u16(p), lines = lx_guest_load_u16(p + 2);
        const UCHAR *dib = p + 18;
        ULONG available = bytes - 24;
        if (!open32DibBuffers((PVOID)dib, lx_guest_load_u16(p + 4), lines, usage, &info_bytes,
                              &pixel_bytes) ||
            info_bytes > available || pixel_bytes > available - info_bytes)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return SetDIBitsToDevice(dc, metafile_word(p + 16), metafile_word(p + 14),
                                 metafile_word(p + 12), metafile_word(p + 10), metafile_word(p + 8),
                                 metafile_word(p + 6), lx_guest_load_u16(p + 4), lines,
                                 (PVOID)(dib + info_bytes), (PVOID)dib, usage) != 0;
    }
    if (function == 0x12c && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p);
        ULONG object = index < count ? lx_guest_load_u32((uint8 *)table + index * 4) : 0;
        if (!(index & 0x8000) && !object)
            return FALSE;
        return SelectClipRgn(dc, index & 0x8000 ? 0 : object) != 0;
    }
    if (function == 0x35)
        return RealizePalette(dc) != UINT32_MAX;
    if ((function == 0x37 || function == 0x436) && bytes >= 10)
    {
        Open32DC *context = open32FindDC(dc);
        ULONG first = lx_guest_load_u16(p), entries = lx_guest_load_u16(p + 2);
        if (entries > (bytes - 10) / 4)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return function == 0x37
                   ? SetPaletteEntries(context->palette_handle, first, entries, (PVOID)(p + 4)) != 0
                   : AnimatePalette(context->palette_handle, first, entries, (PVOID)(p + 4));
    }
    if (function == 0x139 && bytes == 8)
        return ResizePalette(open32FindDC(dc)->palette_handle, lx_guest_load_u16(p));
    if (function == 0x626 && bytes >= 10)
    {
        /* A device may decline an escape; that is not a playback failure. */
        ULONG size = lx_guest_load_u16(p + 2);
        if (size > bytes - 10)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (lx_guest_load_u16(p) != 15)
            Escape(dc, lx_guest_load_u16(p), (LONG)size, (PVOID)(p + 4), NULL);
        return TRUE;
    }
    /* SetRelAbs has no effect on Win32 drawing, and every host face is a scalable face that
       aspect filtering cannot exclude. */
    if (function == 0x105 || function == 0x231)
        return TRUE;
    TRACE_NATIVE("Unsupported WMF record %04X (%u bytes)", function, bytes);
    open32SetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL PlayMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count)
{
    (void)table;
    if (!open32FindDC(dc) || !record || count > 65535)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG words = lx_guest_load_u32(record);
    if (words < 3 || words > 32u * 1024 * 1024)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return play_record(dc, record, table, count);
}

BOOL EnumMetaFile(ULONG dc, ULONG handle, ULONG procedure, ULONG parameter)
{
    TRACE_NATIVE("EnumMetaFile(%u, %u, %X, %X)", dc, handle, procedure, parameter);
    LxExecution *execution = GProcessState.execution;
    LxHandleBorrow borrow = {0};
    if ((dc && !open32FindDC(dc)) || !open32ValidProcedure(procedure) ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Open32Metafile *file = borrow.resource;
    ULONG count = lx_guest_load_u16(file->data + 10);
    ULONG table_bytes = count ? count * 4 : 4;
    size_t record_bytes = (size_t)lx_guest_load_u32(file->data + 12) * 2;
    size_t size = (table_bytes + record_bytes + 4095) & ~(size_t)4095;
    LxGuestMapping *data = lx_execution_map(execution, 0, size, 3, 0);
    LxGuestMapping *code = data ? lx_execution_map(execution, 0, 4096, 3, 0) : NULL;
    BOOL ok = data && code;
    if (ok)
    {
        /* The execution boundary has a four-word callback. This guest thunk
           forwards those four words and the fifth MFENUMPROC parameter. EBP
           restores its own frame regardless of the callback's stack cleanup. */
        UCHAR thunk[] = {0x55, 0x89, 0xe5, 0x68, 0,    0,    0,    0,    0xff, 0x75, 0x14,
                         0xff, 0x75, 0x10, 0xff, 0x75, 0x0c, 0xff, 0x75, 0x08, 0xb8, 0,
                         0,    0,    0,    0xff, 0xd0, 0x89, 0xec, 0x5d, 0xc3};
        lx_guest_store_u32(thunk + 4, parameter);
        lx_guest_store_u32(thunk + 21, procedure);
        memcpy(lx_execution_mapping_host(code), thunk, sizeof thunk);
        ok = lx_execution_protect(execution, code, LX_GUEST_READ | LX_GUEST_EXECUTE);
    }
    if (!ok)
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    if (ok)
    {
        UCHAR *record = (UCHAR *)lx_execution_mapping_host(data) + table_bytes;
        ULONG table_address = lx_execution_mapping_address(data);
        for (ULONG offset = 18; ok && offset < file->size;)
        {
            ULONG bytes = lx_guest_load_u32(file->data + offset) * 2;
            memcpy(record, file->data + offset, bytes);
            ok = lx_execution_callback32_4(execution, lx_execution_mapping_address(code), dc,
                                           table_address, table_address + table_bytes, count) != 0;
            offset += bytes;
        }
    }
    if (code)
        lx_execution_unmap(execution, code);
    if (data)
        lx_execution_unmap(execution, data);
    lx_handle_release(&borrow);
    return ok;
}

BOOL PlayMetaFile(ULONG dc, ULONG handle)
{
    TRACE_NATIVE("PlayMetaFile(%u, %u)", dc, handle);
    LxHandleBorrow borrow = {0};
    if (!open32FindDC(dc) || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    LONG saved = SaveDC(dc);
    Open32Metafile *file = borrow.resource;
    ULONG count = lx_guest_load_u16(file->data + 10);
    uint8 *table = calloc(count ? count : 1, 4);
    BOOL ok = saved > 0 && table;
    /* A refused record does not stop playback; only malformed framing ends it early. */
    for (size_t offset = 18; ok && offset + 6 <= file->size;)
    {
        const UCHAR *record = file->data + offset;
        size_t bytes = (size_t)lx_guest_load_u32(record) * 2;
        if (bytes < 6 || bytes > file->size - offset)
        {
            open32SetError(ERROR_INVALID_DATA);
            ok = FALSE;
            break;
        }
        if (!lx_guest_load_u16(record + 4))
            break;
        play_record(dc, record, table, count);
        offset += bytes;
    }
    ULONG error = GetLastError();
    if (saved && !RestoreDC(dc, saved))
        ok = FALSE;
    if (table)
    {
        for (ULONG i = 0; i < count; i++)
        {
            ULONG object = lx_guest_load_u32(table + i * 4);
            if (object)
                DeleteObject(object);
        }
        free(table);
    }
    lx_handle_release(&borrow);
    if (!ok)
        open32SetError(error ? error : ERROR_INVALID_PARAMETER);
    return ok;
}

void open32MetafilesCleanup(void)
{
    /* A guest may exit inside EnumMetaFile, which keeps its file borrowed and listed. */
    for (Open32Metafile *file = metafile_files, *next; file; file = next)
    {
        next = file->next;
        lx_handle_close(file->handle,
                        file->enhanced ? LX_HANDLE_OPEN32_ENHMETAFILE : LX_HANDLE_OPEN32_METAFILE);
    }
    if (metafile_monitor)
        lx_host_monitor_destroy(metafile_monitor);
    metafile_monitor = NULL;
    metafile_once = (LxHostOnce)LX_HOST_ONCE_INIT;
}

BOOL metafile_save(PSZ name, const uint8 *bytes, ULONG size)
{
    uint8 of[136];
    LONG file = OpenFile(name, of, 0x1002);
    if (file < 0)
        return FALSE;
    ULONG written = _lwrite((ULONG)file, (PVOID)bytes, size);
    LONG closed = _lclose((ULONG)file);
    return written == size && closed == 0;
}

ULONG metafile_read(PSZ name, BOOL enhanced)
{
    if (!name)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint8 of[136];
    LONG file = OpenFile(name, of, 0);
    if (file < 0)
        return 0;
    LONG size = _llseek((ULONG)file, 0, 2);
    ULONG result = 0;
    if (size >= 24 && size <= 64 * 1024 * 1024 && _llseek((ULONG)file, 0, 0) == 0)
    {
        uint8 *bytes = malloc((size_t)size);
        if (bytes)
        {
            if (_lread((ULONG)file, bytes, (ULONG)size) == (ULONG)size)
                result = enhanced ? SetEnhMetaFileBits((ULONG)size, bytes)
                                  : SetMetaFileBitsEx((ULONG)size, bytes);
            free(bytes);
        }
        else
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    else
        open32SetError(ERROR_INVALID_DATA);
    _lclose((ULONG)file);
    return result;
}

ULONG GetMetaFile(PSZ name)
{
    return metafile_read(name, FALSE);
}

ULONG CopyMetaFile(ULONG handle, PSZ name)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG result =
        (!name || metafile_save(name, f->data, f->size)) ? SetMetaFileBitsEx(f->size, f->data) : 0;
    lx_handle_release(&borrow);
    return result;
}
