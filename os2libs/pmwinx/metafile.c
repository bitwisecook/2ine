/* Public domain. No rights reserved. Wholly AI-authored. */
/* Memory WMFs own a validated copy, independently of the caller's buffer. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "guest/handles.h"
#include "hal/host_sync.h"
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pm_text.h"

typedef struct Open32Metafile
{
    struct Open32Metafile *next;
    ULONG handle, size;
    BOOL enhanced;
    UCHAR data[];
} Open32Metafile;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static Open32Metafile *files;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
}
static void destroy_file(void *resource)
{
    Open32Metafile *file = resource;
    lx_host_monitor_lock(monitor);
    Open32Metafile **slot = &files;
    while (*slot && *slot != file)
        slot = &(*slot)->next;
    if (*slot)
        *slot = file->next;
    lx_host_monitor_unlock(monitor);
    free(file);
}
static BOOL valid_wmf(const UCHAR *data, ULONG size)
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
        open32SetError(13);
        return 0;
    }
    lx_host_once(&once, initialize);
    Open32Metafile *file = monitor ? malloc(sizeof *file + size) : NULL;
    if (!file)
    {
        open32SetError(8);
        return 0;
    }
    file->size = size;
    file->enhanced = FALSE;
    memcpy(file->data, data, size);
    /* Validate the owned snapshot: another guest thread can reuse its input
       buffer as soon as it has entered this API. */
    if (!valid_wmf(file->data, size))
    {
        free(file);
        open32SetError(13);
        return 0;
    }
    lx_host_monitor_lock(monitor);
    file->handle = lx_handle_create(LX_HANDLE_OPEN32_METAFILE, file, destroy_file);
    if (file->handle)
    {
        file->next = files;
        files = file;
    }
    lx_host_monitor_unlock(monitor);
    ULONG handle = file->handle;
    if (!handle)
    {
        free(file);
        open32SetError(8);
    }
    return handle;
}
ULONG GetMetaFileBitsEx(ULONG handle, ULONG size, PVOID data)
{
    TRACE_NATIVE("GetMetaFileBitsEx(%u, %u, %p)", handle, size, data);
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(6);
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
    open32SetError(6);
    return FALSE;
}
static LONG signed_word(const UCHAR *bytes)
{
    return (SHORT)lx_guest_load_u16(bytes);
}
/* While a metafile is fitted to a target rectangle, its own viewport and mapping-mode records
   would replace that fit, so playback keeps the fitted viewport. */
static _Thread_local unsigned fitted_playback;
static BOOL wmf_dib(ULONG dc, const UCHAR *dib, ULONG available, LONG x, LONG y, LONG width,
                    LONG height, LONG source_x, LONG source_y, LONG source_width,
                    LONG source_height, ULONG usage, ULONG operation)
{
    size_t info_bytes, pixel_bytes;
    if (available < 40 ||
        !open32DibBuffers((PVOID)dib, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        !pixel_bytes || info_bytes > available || pixel_bytes > available - info_bytes)
    {
        open32SetError(13);
        return FALSE;
    }
    return StretchDIBits(dc, x, y, width, height, source_x, source_y, source_width, source_height,
                         (PVOID)(dib + info_bytes), (PVOID)dib, usage, operation) != 0;
}
static BOOL wmf_text(ULONG dc, const UCHAR *p, ULONG bytes)
{
    ULONG n = lx_guest_load_u16(p + 4), options = lx_guest_load_u16(p + 6) & 6;
    ULONG text_at = options ? 16 : 8, padded = (n + 1) & ~1u, parameters = bytes - 6;
    if (text_at > parameters || padded > parameters - text_at)
    {
        open32SetError(13);
        return FALSE;
    }
    uint8 rectangle[16];
    for (unsigned i = 0; options && i < 4; i++)
        lx_guest_store_u32(rectangle + i * 4, (ULONG)signed_word(p + 8 + i * 2));
    uint8 *advances = NULL;
    if (parameters - text_at - padded >= n * 2 && n)
    {
        advances = malloc((size_t)n * 4);
        if (!advances)
        {
            open32SetError(8);
            return FALSE;
        }
        for (ULONG i = 0; i < n; i++)
            lx_guest_store_u32(advances + i * 4, (ULONG)signed_word(p + text_at + padded + i * 2));
    }
    BOOL result = ExtTextOut(dc, signed_word(p + 2), signed_word(p), options,
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
    if (fitted_playback && (function == 0x103 || function == 0x20d || function == 0x20e ||
                            function == 0x211 || function == 0x412))
        return TRUE;
    if (function == 0x103 && bytes == 8)
        return SetMapMode(dc, signed_word(p)) != 0;
    if (function == 0x20b && bytes == 10)
        return SetWindowOrgEx(dc, signed_word(p + 2), signed_word(p), NULL);
    if (function == 0x20c && bytes == 10)
        return SetWindowExtEx(dc, signed_word(p + 2), signed_word(p), NULL);
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
            open32SetError(13);
            return FALSE;
        }
        return StretchDIBits(dc, signed_word(p + 20), signed_word(p + 18), signed_word(p + 16),
                             signed_word(p + 14), signed_word(p + 12), signed_word(p + 10),
                             signed_word(p + 8), signed_word(p + 6), (UCHAR *)info + info_bytes,
                             info, usage, lx_guest_load_u32(p)) != 0;
    }
    if (function == 0x102 && bytes == 8)
        return SetBkMode(dc, signed_word(p)) != 0;
    if (function == 0x104 && bytes == 8)
        return SetROP2(dc, signed_word(p)) != 0;
    if (function == 0x106 && bytes == 8)
        return SetPolyFillMode(dc, signed_word(p)) != 0;
    if (function == 0x107 && bytes == 8)
        return SetStretchBltMode(dc, signed_word(p)) != 0;
    if (function == 0x108 && bytes == 8)
        return SetTextCharacterExtra(dc, signed_word(p)) != INT32_MIN;
    if (function == 0x201 && bytes == 10)
        return SetBkColor(dc, lx_guest_load_u32(p)) != UINT32_MAX;
    if (function == 0x209 && bytes == 10)
        return SetTextColor(dc, lx_guest_load_u32(p)) != UINT32_MAX;
    if (function == 0x20a && bytes == 10)
        return SetTextJustification(dc, signed_word(p + 2), signed_word(p));
    if (function == 0x20d && bytes == 10)
        return SetViewportOrgEx(dc, signed_word(p + 2), signed_word(p), NULL);
    if (function == 0x20e && bytes == 10)
        return SetViewportExtEx(dc, signed_word(p + 2), signed_word(p), NULL);
    if (function == 0x214 && bytes == 10)
        return MoveToEx(dc, signed_word(p + 2), signed_word(p), NULL);
    if (function == 0x213 && bytes == 10)
        return LineTo(dc, signed_word(p + 2), signed_word(p));
    if (function == 0x1e && bytes == 6)
        return SaveDC(dc) != 0;
    if (function == 0x127 && bytes == 8)
        return RestoreDC(dc, signed_word(p));
    if ((function == 0x418 || function == 0x41b) && bytes == 14)
        return function == 0x418 ? Ellipse(dc, signed_word(p + 6), signed_word(p + 4),
                                           signed_word(p + 2), signed_word(p))
                                 : Rectangle(dc, signed_word(p + 6), signed_word(p + 4),
                                             signed_word(p + 2), signed_word(p));
    if (function == 0x61c && bytes == 18)
        return RoundRect(dc, signed_word(p + 10), signed_word(p + 8), signed_word(p + 6),
                         signed_word(p + 4), signed_word(p + 2), signed_word(p));
    if (function == 0x830 && bytes == 22)
        return open32Chord(dc, signed_word(p + 14), signed_word(p + 12), signed_word(p + 10),
                           signed_word(p + 8), signed_word(p + 6), signed_word(p + 4),
                           signed_word(p + 2), signed_word(p));
    if ((function == 0x817 || function == 0x81a) && bytes == 22)
        return function == 0x817 ? Arc(dc, signed_word(p + 14), signed_word(p + 12),
                                       signed_word(p + 10), signed_word(p + 8), signed_word(p + 6),
                                       signed_word(p + 4), signed_word(p + 2), signed_word(p))
                                 : Pie(dc, signed_word(p + 14), signed_word(p + 12),
                                       signed_word(p + 10), signed_word(p + 8), signed_word(p + 6),
                                       signed_word(p + 4), signed_word(p + 2), signed_word(p));
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
            lx_guest_store_u32(points + i * 8, (ULONG)signed_word(p + 2 + i * 4));
            lx_guest_store_u32(points + i * 8 + 4, (ULONG)signed_word(p + 4 + i * 4));
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
            lx_guest_store_u32(points + i * 8, (ULONG)signed_word(p + 2 + polygons * 2 + i * 4));
            lx_guest_store_u32(points + i * 8 + 4,
                               (ULONG)signed_word(p + 4 + polygons * 2 + i * 4));
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
        return TextOut(dc, signed_word(p + 4 + padded), signed_word(p + 2 + padded), (PSZ)(p + 2),
                       (LONG)n);
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
    if (function == 0x2fa || function == 0x2fc || function == 0x2fb || function == 0xf7)
    {
        ULONG object = 0;
        if (function == 0xf7 && bytes >= 10 && lx_guest_load_u16(p + 2) <= (bytes - 10) / 4)
            object = CreatePalette((PVOID)p);
        else if (function == 0x2fa && bytes == 16)
            object = CreatePen(signed_word(p) & 15, signed_word(p + 2), lx_guest_load_u32(p + 6));
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
                lx_guest_store_u32(font + i * 4, (ULONG)signed_word(p + i * 2));
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
        return SetPixel(dc, signed_word(p + 6), signed_word(p + 4), lx_guest_load_u32(p)) !=
               UINT32_MAX;
    if (function == 0x61d && bytes == 18)
        return PatBlt(dc, signed_word(p + 10), signed_word(p + 8), signed_word(p + 6),
                      signed_word(p + 4), lx_guest_load_u32(p));
    if (function == 0x12e && bytes == 8)
        return SetTextAlign(dc, lx_guest_load_u16(p)) != UINT32_MAX;
    if ((function == 0x20f || function == 0x211) && bytes == 10)
        return function == 0x20f
                   ? OffsetWindowOrgEx(dc, signed_word(p + 2), signed_word(p), NULL)
                   : OffsetViewportOrgEx(dc, signed_word(p + 2), signed_word(p), NULL);
    if ((function == 0x400 || function == 0x412) && bytes == 14)
        return function == 0x400 ? ScaleWindowExtEx(dc, signed_word(p + 6), signed_word(p + 4),
                                                    signed_word(p + 2), signed_word(p), NULL)
                                 : ScaleViewportExtEx(dc, signed_word(p + 6), signed_word(p + 4),
                                                      signed_word(p + 2), signed_word(p), NULL);
    if ((function == 0x415 || function == 0x416) && bytes == 14)
        return (function == 0x415 ? ExcludeClipRect(dc, signed_word(p + 6), signed_word(p + 4),
                                                    signed_word(p + 2), signed_word(p))
                                  : IntersectClipRect(dc, signed_word(p + 6), signed_word(p + 4),
                                                      signed_word(p + 2), signed_word(p))) != 0;
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
            return PatBlt(dc, signed_word(q + 6), signed_word(q + 4), signed_word(q + 2),
                          signed_word(q), operation);
        }
        const UCHAR *q = p + head - 8, *source = p + 4;
        LONG height = signed_word(q), width = signed_word(q + 2);
        LONG source_height = function == 0x940 ? height : signed_word(source);
        LONG source_width = function == 0x940 ? width : signed_word(source + 2);
        const UCHAR *origin = function == 0x940 ? source : source + 4;
        return wmf_dib(dc, p + head, parameters - head, signed_word(q + 6), signed_word(q + 4),
                       width, height, signed_word(origin + 2), signed_word(origin), source_width,
                       source_height, 0, operation);
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
            open32SetError(13);
            return FALSE;
        }
        return SetDIBitsToDevice(dc, signed_word(p + 16), signed_word(p + 14), signed_word(p + 12),
                                 signed_word(p + 10), signed_word(p + 8), signed_word(p + 6),
                                 lx_guest_load_u16(p + 4), lines, (PVOID)(dib + info_bytes),
                                 (PVOID)dib, usage) != 0;
    }
    /* Device escapes, relative mode, mapper flags and palette realization change no pixels on
       this true-color raster backend. */
    if (function == 0x626 || function == 0x105 || function == 0x231 || function == 0x35 ||
        function == 0x37)
        return TRUE;
    TRACE_NATIVE("Unsupported WMF record %04X (%u bytes)", function, bytes);
    open32SetError(50);
    return FALSE;
}
BOOL PlayMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count)
{
    (void)table;
    if (!open32FindDC(dc) || !record || count > 65535)
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG words = lx_guest_load_u32(record);
    if (words < 3 || words > 32u * 1024 * 1024)
    {
        open32SetError(13);
        return FALSE;
    }
    return play_record(dc, record, table, count);
}
BOOL EnumMetaFile(ULONG dc, ULONG handle, ULONG procedure, ULONG parameter)
{
    TRACE_NATIVE("EnumMetaFile(%u, %u, %X, %X)", dc, handle, procedure, parameter);
    LxExecution *execution = GLoaderState.execution;
    LxHandleBorrow borrow = {0};
    if ((dc && !open32FindDC(dc)) || !open32ValidProcedure(procedure) ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(87);
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
        open32SetError(8);
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
        open32SetError(6);
        return FALSE;
    }
    LONG saved = SaveDC(dc);
    Open32Metafile *file = borrow.resource;
    ULONG count = lx_guest_load_u16(file->data + 10);
    uint8 *table = calloc(count ? count : 1, 4);
    BOOL ok = saved > 0 && table;
    for (ULONG offset = 18; ok && offset < file->size;)
    {
        ok = play_record(dc, file->data + offset, table, count);
        offset += lx_guest_load_u32(file->data + offset) * 2;
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
        open32SetError(error ? error : 87);
    return ok;
}
void open32MetafilesCleanup(void)
{
    while (files)
        lx_handle_close(files->handle,
                        files->enhanced ? LX_HANDLE_OPEN32_ENHMETAFILE : LX_HANDLE_OPEN32_METAFILE);
    if (monitor)
        lx_host_monitor_destroy(monitor);
    monitor = NULL;
    once = (LxHostOnce)LX_HOST_ONCE_INIT;
}

/* A recording DC owns an ordinary checked DC plus an independent WMF stream.
   The original PMWINX likewise separates metafile records from its HPS wrapper. */
typedef struct Open32Recorder
{
    ULONG dc, objects, maximum;
    size_t size, capacity;
    uint8 *data;
    char *name;
    BOOL failed;
    struct Open32Recorder *next;
} Open32Recorder;
static _Thread_local Open32Recorder *recorders;
static Open32Recorder *recorder(ULONG dc)
{
    for (Open32Recorder *r = recorders; r; r = r->next)
        if (r->dc == dc)
            return r;
    return NULL;
}
static BOOL append_record(Open32Recorder *r, USHORT function, const void *data, size_t bytes)
{
    size_t padded = (bytes + 1) & ~(size_t)1, total = padded + 6;
    if (r->failed || total > 64u * 1024 * 1024 - r->size)
    {
        r->failed = TRUE;
        return FALSE;
    }
    if (r->capacity < r->size + total)
    {
        size_t next = r->capacity ? r->capacity * 2 : 256;
        while (next < r->size + total)
            next *= 2;
        uint8 *more = realloc(r->data, next);
        if (!more)
        {
            r->failed = TRUE;
            return FALSE;
        }
        r->data = more;
        r->capacity = next;
    }
    uint8 *p = r->data + r->size;
    lx_guest_store_u32(p, (ULONG)(total / 2));
    lx_guest_store_u16(p + 4, function);
    if (bytes)
        memcpy(p + 6, data, bytes);
    if (bytes & 1)
        p[6 + bytes] = 0;
    r->size += total;
    if (total / 2 > r->maximum)
        r->maximum = (ULONG)(total / 2);
    return TRUE;
}
void open32MetafileWords(ULONG dc, USHORT function, size_t count, const LONG *words)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    uint8 parameters[64];
    if (count > 32)
    {
        r->failed = TRUE;
        return;
    }
    for (size_t i = 0; i < count; i++)
    {
        if (words[i] < INT16_MIN || words[i] > INT16_MAX)
        {
            r->failed = TRUE;
            return;
        }
        lx_guest_store_u16(parameters + i * 2, (uint16)words[count - 1 - i]);
    }
    append_record(r, function, parameters, count * 2);
}
void open32MetafilePoints(ULONG dc, USHORT function, PVOID input, LONG count)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (count < 0 || count > 65535)
    {
        r->failed = TRUE;
        return;
    }
    size_t bytes = 2 + (size_t)count * 4;
    uint8 *p = malloc(bytes);
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, (uint16)count);
    for (LONG i = 0; i < count; i++)
        for (unsigned k = 0; k < 2; k++)
        {
            LONG value = (LONG)lx_guest_load_u32((uint8 *)input + i * 8 + k * 4);
            if (value < INT16_MIN || value > INT16_MAX)
                r->failed = TRUE;
            lx_guest_store_u16(p + 2 + i * 4 + k * 2, (uint16)value);
        }
    if (!r->failed)
        append_record(r, function, p, bytes);
    free(p);
}
void open32MetafileObject(ULONG dc, ULONG object)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    uint8 source[60], wire[50];
    size_t bytes = 0;
    USHORT type = 0;
    ULONG kind = GetObjectType(object);
    if (kind == 1 && open32PenObject(object, sizeof source, source))
    {
        type = 0x2fa;
        bytes = 10;
        lx_guest_store_u16(wire, (uint16)lx_guest_load_u32(source));
        lx_guest_store_u16(wire + 2, (uint16)lx_guest_load_u32(source + 4));
        lx_guest_store_u16(wire + 4, 0);
        memcpy(wire + 6, source + 12, 4);
    }
    else if (kind == 2 && open32BrushObject(object, sizeof source, source))
    {
        type = 0x2fc;
        bytes = 8;
        lx_guest_store_u16(wire, (uint16)lx_guest_load_u32(source));
        memcpy(wire + 2, source + 4, 4);
        lx_guest_store_u16(wire + 6, (uint16)lx_guest_load_u32(source + 8));
    }
    else if (kind == 6 && open32FontObject(object, sizeof source, source))
    {
        type = 0x2fb;
        bytes = 50;
        for (unsigned i = 0; i < 5; i++)
            lx_guest_store_u16(wire + i * 2, (uint16)lx_guest_load_u32(source + i * 4));
        memcpy(wire + 10, source + 20, 40);
    }
    else
    {
        r->failed = TRUE;
        return;
    }
    if (r->objects >= 65535 || !append_record(r, type, wire, bytes))
    {
        r->failed = TRUE;
        return;
    }
    uint8 index[2];
    lx_guest_store_u16(index, (uint16)r->objects++);
    append_record(r, 0x12d, index, 2);
}
void open32MetafileText(ULONG dc, LONG x, LONG y, PCHAR text, ULONG count)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (count > 65535 || x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX)
    {
        r->failed = TRUE;
        return;
    }
    size_t padded = (count + 1) & ~1u;
    uint8 *p = calloc(padded + 6, 1);
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, (uint16)count);
    if (count)
        memcpy(p + 2, text, count);
    lx_guest_store_u16(p + 2 + padded, (uint16)y);
    lx_guest_store_u16(p + 4 + padded, (uint16)x);
    append_record(r, 0x521, p, padded + 6);
    free(p);
}
void open32MetafileDCCleanup(ULONG dc)
{
    Open32Recorder **link = &recorders;
    while (*link && (*link)->dc != dc)
        link = &(*link)->next;
    if (*link)
    {
        Open32Recorder *r = *link;
        *link = r->next;
        free(r->data);
        free(r->name);
        free(r);
    }
}
static BOOL save_metafile(PSZ name, const uint8 *bytes, ULONG size)
{
    uint8 of[136];
    LONG file = OpenFile(name, of, 0x1002);
    if (file < 0)
        return FALSE;
    ULONG written = _lwrite((ULONG)file, (PVOID)bytes, size);
    LONG closed = _lclose((ULONG)file);
    return written == size && closed == 0;
}
static ULONG read_metafile(PSZ name, BOOL enhanced)
{
    if (!name)
    {
        open32SetError(87);
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
            open32SetError(8);
    }
    else
        open32SetError(13);
    _lclose((ULONG)file);
    return result;
}
ULONG CreateMetaFile(PSZ name)
{
    ULONG dc = CreateCompatibleDC(0);
    if (!dc)
        return 0;
    Open32Recorder *r = calloc(1, sizeof *r);
    if (!r)
    {
        DeleteDC(dc);
        open32SetError(8);
        return 0;
    }
    r->dc = dc;
    if (name)
        r->name = strdup(name);
    if (name && !r->name)
    {
        free(r);
        DeleteDC(dc);
        open32SetError(8);
        return 0;
    }
    r->size = 18;
    r->next = recorders;
    recorders = r;
    open32MetafileObject(dc, GetStockObject(0));
    open32MetafileObject(dc, GetStockObject(7));
    open32MetafileObject(dc, GetStockObject(13));
    return dc;
}
ULONG CloseMetaFile(ULONG dc)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
    {
        open32SetError(6);
        return 0;
    }
    ULONG result = 0;
    if (append_record(r, 0, NULL, 0))
    {
        lx_guest_store_u16(r->data, 1);
        lx_guest_store_u16(r->data + 2, 9);
        lx_guest_store_u16(r->data + 4, 0x300);
        lx_guest_store_u32(r->data + 6, (ULONG)(r->size / 2));
        lx_guest_store_u16(r->data + 10, (uint16)r->objects);
        lx_guest_store_u32(r->data + 12, r->maximum);
        lx_guest_store_u16(r->data + 16, 0);
        if (!r->name || save_metafile(r->name, r->data, (ULONG)r->size))
            result = SetMetaFileBitsEx((ULONG)r->size, r->data);
    }
    else
        open32SetError(8);
    open32MetafileDCCleanup(dc);
    DeleteDC(dc);
    return result;
}
ULONG GetMetaFile(PSZ name)
{
    return read_metafile(name, FALSE);
}
ULONG CopyMetaFile(ULONG handle, PSZ name)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_METAFILE, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG result =
        (!name || save_metafile(name, f->data, f->size)) ? SetMetaFileBitsEx(f->size, f->data) : 0;
    lx_handle_release(&borrow);
    return result;
}

static BOOL valid_emf(const uint8 *p, ULONG size)
{
    if (!p || size < 108 || size > 64u * 1024 * 1024 || (size & 3) || lx_guest_load_u32(p) != 1 ||
        lx_guest_load_u32(p + 4) < 88 || lx_guest_load_u32(p + 4) > size ||
        lx_guest_load_u32(p + 40) != 0x464d4520 || lx_guest_load_u32(p + 48) != size)
        return FALSE;
    ULONG count = 0;
    for (ULONG offset = 0; offset <= size - 8;)
    {
        ULONG type = lx_guest_load_u32(p + offset), bytes = lx_guest_load_u32(p + offset + 4);
        if (bytes < 8 || (bytes & 3) || bytes > size - offset)
            return FALSE;
        count++;
        if (type == 14)
            return bytes >= 20 && offset + bytes == size && count == lx_guest_load_u32(p + 52);
        offset += bytes;
    }
    return FALSE;
}
ULONG SetEnhMetaFileBits(ULONG size, PVOID data)
{
    if (!data || size < 108 || size > 64u * 1024 * 1024)
    {
        open32SetError(13);
        return 0;
    }
    lx_host_once(&once, initialize);
    Open32Metafile *f = monitor ? malloc(sizeof *f + size) : NULL;
    if (!f)
    {
        open32SetError(8);
        return 0;
    }
    memcpy(f->data, data, size);
    if (!valid_emf(f->data, size))
    {
        free(f);
        open32SetError(13);
        return 0;
    }
    f->size = size;
    f->enhanced = TRUE;
    lx_host_monitor_lock(monitor);
    f->handle = lx_handle_create(LX_HANDLE_OPEN32_ENHMETAFILE, f, destroy_file);
    if (f->handle)
    {
        f->next = files;
        files = f;
    }
    lx_host_monitor_unlock(monitor);
    ULONG result = f->handle;
    if (!result)
    {
        free(f);
        open32SetError(8);
    }
    return result;
}
ULONG GetEnhMetaFile(PSZ name)
{
    return read_metafile(name, TRUE);
}
BOOL DeleteEnhMetaFile(ULONG handle)
{
    if (lx_handle_close(handle, LX_HANDLE_OPEN32_ENHMETAFILE))
        return TRUE;
    open32SetError(6);
    return FALSE;
}
ULONG GetEnhMetaFileBits(ULONG handle, ULONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG copied = !output ? f->size : capacity < f->size ? capacity : f->size;
    if (output)
        memcpy(output, f->data, copied);
    lx_handle_release(&borrow);
    return copied;
}
ULONG GetEnhMetaFileHeader(ULONG handle, ULONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG size = lx_guest_load_u32(f->data + 4), copied = !output           ? size
                                                          : capacity < size ? capacity
                                                                            : size;
    if (output)
        memcpy(output, f->data, copied);
    lx_handle_release(&borrow);
    return copied;
}
static BOOL emf_object(PVOID table, ULONG count, ULONG index, ULONG object)
{
    if (!index || index >= count || !object)
    {
        if (object)
            DeleteObject(object);
        open32SetError(13);
        return FALSE;
    }
    uint8 *p = (uint8 *)table + index * 4;
    if (lx_guest_load_u32(p))
    {
        DeleteObject(object);
        open32SetError(13);
        return FALSE;
    }
    lx_guest_store_u32(p, object);
    return TRUE;
}
/* An RGNDATA block becomes a region of the same logical rectangles. */
static ULONG emf_region(const uint8 *data, ULONG size)
{
    ULONG n = size >= 32 ? lx_guest_load_u32(data + 8) : UINT32_MAX;
    if (n > (size - 32) / 16 || size < 32)
    {
        open32SetError(13);
        return 0;
    }
    ULONG region = CreateRectRgn(0, 0, 0, 0), part = region ? CreateRectRgn(0, 0, 0, 0) : 0;
    BOOL ok = part != 0;
    for (ULONG i = 0; ok && i < n; i++)
    {
        const uint8 *r = data + 32 + i * 16;
        ok = SetRectRgn(part, (LONG)lx_guest_load_u32(r), (LONG)lx_guest_load_u32(r + 4),
                        (LONG)lx_guest_load_u32(r + 8), (LONG)lx_guest_load_u32(r + 12)) &&
             CombineRgn(region, region, part, 2);
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
static BOOL emf_dib(const uint8 *p, ULONG bytes, ULONG info_at, ULONG info_size, ULONG bits_at,
                    ULONG bits_size, ULONG usage)
{
    size_t info_bytes, pixel_bytes;
    if (info_at > bytes || info_size > bytes - info_at || bits_at > bytes ||
        bits_size > bytes - bits_at || info_size < 40 ||
        !open32DibBuffers((PVOID)(p + info_at), 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        info_bytes > info_size || pixel_bytes > bits_size)
    {
        open32SetError(13);
        return FALSE;
    }
    return TRUE;
}
/* Cubic segments flatten to sixteen chords each. */
static BOOL emf_bezier(ULONG dc, const uint8 *points, ULONG n, BOOL from_current)
{
    Open32DC *context = open32FindDC(dc);
    LONG x0 = context->position_x, y0 = context->position_y;
    ULONG first = 0;
    if (!from_current)
    {
        if (!n)
            return TRUE;
        x0 = (LONG)lx_guest_load_u32(points);
        y0 = (LONG)lx_guest_load_u32(points + 4);
        first = 1;
    }
    if ((n - first) % 3 || (n - first) / 3 > 4095)
    {
        open32SetError(13);
        return FALSE;
    }
    ULONG segments = (n - first) / 3, total = segments * 16 + 1;
    if (!segments)
        return TRUE;
    uint8 *line = malloc((size_t)total * 8);
    if (!line)
    {
        open32SetError(8);
        return FALSE;
    }
    lx_guest_store_u32(line, (ULONG)x0);
    lx_guest_store_u32(line + 4, (ULONG)y0);
    double ax = x0, ay = y0;
    for (ULONG s = 0; s < segments; s++)
    {
        const uint8 *q = points + (first + s * 3) * 8;
        double bx = (LONG)lx_guest_load_u32(q), by = (LONG)lx_guest_load_u32(q + 4),
               cx = (LONG)lx_guest_load_u32(q + 8), cy = (LONG)lx_guest_load_u32(q + 12),
               dx = (LONG)lx_guest_load_u32(q + 16), dy = (LONG)lx_guest_load_u32(q + 20);
        for (unsigned i = 1; i <= 16; i++)
        {
            double t = i / 16.0, u = 1 - t;
            double wa = u * u * u, wb = 3 * u * u * t, wc = 3 * u * t * t, wd = t * t * t;
            uint8 *out = line + ((size_t)s * 16 + i) * 8;
            lx_guest_store_u32(out, (ULONG)(LONG)(wa * ax + wb * bx + wc * cx + wd * dx + 0.5));
            lx_guest_store_u32(out + 4, (ULONG)(LONG)(wa * ay + wb * by + wc * cy + wd * dy + 0.5));
        }
        ax = dx;
        ay = dy;
    }
    BOOL ok = Polyline(dc, line, (LONG)total);
    if (ok && from_current)
    {
        context->position_x = (LONG)ax;
        context->position_y = (LONG)ay;
    }
    free(line);
    return ok;
}
BOOL PlayEnhMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count)
{
    if (!open32FindDC(dc) || !record || count > 65535 || (count && !table))
    {
        open32SetError(87);
        return FALSE;
    }
    const uint8 *p = record;
    ULONG type = lx_guest_load_u32(p), bytes = lx_guest_load_u32(p + 4);
    if (bytes < 8 || (bytes & 3) || bytes > 64u * 1024 * 1024)
    {
        open32SetError(13);
        return FALSE;
    }
#define EMF_LONG(o) ((LONG)lx_guest_load_u32(p + (o)))
    /* Records PMWINX's dispatch table leaves as no-ops, plus state with no raster effect here. */
    if (type == 1 || type == 14 || type == 16 || type == 23 || type == 28 || type == 52 ||
        type == 58 || type == 65 || type == 69 || type == 79 || (type >= 96 && type <= 101))
        return TRUE;
    if (fitted_playback && (type == 11 || type == 12 || type == 17 || type == 31))
        return TRUE;
    if (type == 9 && bytes >= 16)
        return SetWindowExtEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == 10 && bytes >= 16)
        return SetWindowOrgEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == 11 && bytes >= 16)
        return SetViewportExtEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == 12 && bytes >= 16)
        return SetViewportOrgEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == 17 && bytes >= 12)
        return SetMapMode(dc, EMF_LONG(8)) != 0;
    if (type == 18 && bytes >= 12)
        return SetBkMode(dc, EMF_LONG(8)) != 0;
    if (type == 19 && bytes >= 12)
        return SetPolyFillMode(dc, EMF_LONG(8)) != 0;
    if (type == 20 && bytes >= 12)
        return SetROP2(dc, EMF_LONG(8)) != 0;
    if (type == 21 && bytes >= 12)
        return SetStretchBltMode(dc, EMF_LONG(8)) != 0;
    if (type == 22 && bytes >= 12)
        return SetTextAlign(dc, lx_guest_load_u32(p + 8)) != UINT32_MAX;
    if (type == 24 && bytes >= 12)
        return SetTextColor(dc, lx_guest_load_u32(p + 8)) != UINT32_MAX;
    if (type == 25 && bytes >= 12)
        return SetBkColor(dc, lx_guest_load_u32(p + 8)) != UINT32_MAX;
    if (type == 15 && bytes >= 20)
        return SetPixel(dc, EMF_LONG(8), EMF_LONG(12), lx_guest_load_u32(p + 16)) != UINT32_MAX;
    if (type == 27 && bytes >= 16)
        return MoveToEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == 54 && bytes >= 16)
        return LineTo(dc, EMF_LONG(8), EMF_LONG(12));
    if (type == 33)
        return SaveDC(dc) != 0;
    if (type == 34 && bytes >= 12)
        return RestoreDC(dc, EMF_LONG(8));
    if (type == 59)
        return BeginPath(dc);
    if (type == 60)
        return EndPath(dc);
    if (type == 64)
        return StrokePath(dc);
    if (type == 42 && bytes >= 24)
        return Ellipse(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20));
    if (type == 43 && bytes >= 24)
        return Rectangle(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20));
    if (type == 44 && bytes >= 32)
        return RoundRect(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                         EMF_LONG(28));
    if ((type == 45 || type == 47) && bytes >= 40)
        return type == 45 ? Arc(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20),
                                EMF_LONG(24), EMF_LONG(28), EMF_LONG(32), EMF_LONG(36))
                          : Pie(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20),
                                EMF_LONG(24), EMF_LONG(28), EMF_LONG(32), EMF_LONG(36));
    if ((type == 3 || type == 4) && bytes >= 28)
    {
        ULONG n = lx_guest_load_u32(p + 24);
        if (n > 65536 || n > (bytes - 28) / 8)
        {
            open32SetError(13);
            return FALSE;
        }
        return type == 3 ? Polygon(dc, (PVOID)(p + 28), (LONG)n)
                         : Polyline(dc, (PVOID)(p + 28), (LONG)n);
    }
    if ((type == 7 || type == 8) && bytes >= 32)
    {
        ULONG polygons = lx_guest_load_u32(p + 24), points = lx_guest_load_u32(p + 28);
        if (polygons > 65536 || points > 65536 || polygons > (bytes - 32) / 4 ||
            points > (bytes - 32 - polygons * 4) / 8)
        {
            open32SetError(13);
            return FALSE;
        }
        uint64 total = 0;
        for (ULONG i = 0; i < polygons; i++)
            total += lx_guest_load_u32(p + 32 + i * 4);
        if (total != points)
        {
            open32SetError(13);
            return FALSE;
        }
        return type == 7
                   ? PolyPolyline(dc, (PVOID)(p + 32 + polygons * 4), (PVOID)(p + 32), polygons)
                   : PolyPolygon(dc, (PVOID)(p + 32 + polygons * 4), (PVOID)(p + 32),
                                 (LONG)polygons);
    }
    if (type == 37 && bytes >= 12)
    {
        ULONG index = lx_guest_load_u32(p + 8), object = 0;
        if (index & 0x80000000)
            object = GetStockObject((LONG)(index & 0x7fffffff));
        else if (index < count)
            object = lx_guest_load_u32((uint8 *)table + index * 4);
        return object && SelectObject(dc, object) != 0;
    }
    if (type == 38 && bytes >= 28)
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          CreatePen(EMF_LONG(12), EMF_LONG(16), lx_guest_load_u32(p + 24)));
    if (type == 39 && bytes >= 24)
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          CreateBrushIndirect((PVOID)(p + 12)));
    if (type == 40 && bytes >= 12)
    {
        ULONG index = lx_guest_load_u32(p + 8);
        if (!index || index >= count)
            return FALSE;
        ULONG object = lx_guest_load_u32((uint8 *)table + index * 4);
        if (!DeleteObject(object))
            return FALSE;
        lx_guest_store_u32((uint8 *)table + index * 4, 0);
        return TRUE;
    }
    if (type == 82 && bytes >= 104)
    {
        uint8 font[60];
        memcpy(font, p + 12, 28);
        memset(font + 28, 0, 32);
        for (unsigned i = 0; i < 31; i++)
        {
            USHORT c = lx_guest_load_u16(p + 40 + i * 2);
            if (!c)
                break;
            font[28 + i] = c < 128 ? (uint8)c : '?';
        }
        return emf_object(table, count, lx_guest_load_u32(p + 8), CreateFontIndirect(font));
    }
    if (type == 81 && bytes >= 80)
    {
        ULONG info_at = lx_guest_load_u32(p + 48), info_size = lx_guest_load_u32(p + 52),
              bits_at = lx_guest_load_u32(p + 56), bits_size = lx_guest_load_u32(p + 60),
              usage = lx_guest_load_u32(p + 64);
        if (info_at > bytes || info_size > bytes - info_at || bits_at > bytes ||
            bits_size > bytes - bits_at || info_size < 40)
            return FALSE;
        size_t ib, pb;
        if (!open32DibBuffers((PVOID)(p + info_at), 0, UINT32_MAX, usage, &ib, &pb) ||
            ib > info_size || pb > bits_size)
            return FALSE;
        return StretchDIBits(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(72), EMF_LONG(76),
                             EMF_LONG(32), EMF_LONG(36), EMF_LONG(40), EMF_LONG(44),
                             (PVOID)(p + bits_at), (PVOID)(p + info_at), usage,
                             lx_guest_load_u32(p + 68)) != 0;
    }
    if (type == 84 && bytes >= 76)
    {
        ULONG n = lx_guest_load_u32(p + 44), at = lx_guest_load_u32(p + 48),
              dx_at = lx_guest_load_u32(p + 72);
        if (n > 65536 || at > bytes || n > (bytes - at) / 2 ||
            (dx_at && (dx_at > bytes || n > (bytes - dx_at) / 4)))
            return FALSE;
        char *ansi = malloc(n ? n : 1);
        if (!ansi)
        {
            open32SetError(8);
            return FALSE;
        }
        BOOL valid = TRUE;
        for (ULONG i = 0; i < n; i++)
        {
            USHORT code = lx_guest_load_u16(p + at + i * 2);
            BOOL found = FALSE;
            for (unsigned c = 0; c < 256; c++)
                if (lx_pm_codepoint(WinCpTranslateChar(0, 1004, (uint8)c, 437)) == code)
                {
                    ansi[i] = (char)c;
                    found = TRUE;
                    break;
                }
            if (!found)
            {
                valid = FALSE;
                open32SetError(50);
                break;
            }
        }
        BOOL result =
            valid && ExtTextOut(dc, EMF_LONG(36), EMF_LONG(40), lx_guest_load_u32(p + 52),
                                (PVOID)(p + 56), ansi, n, dx_at ? (PLONG)(p + dx_at) : NULL);
        free(ansi);
        return result;
    }
    if (type == 83 && bytes >= 76)
    {
        ULONG n = lx_guest_load_u32(p + 44), text_at = lx_guest_load_u32(p + 48),
              dx_at = lx_guest_load_u32(p + 72);
        if (n > 65536 || text_at > bytes || n > bytes - text_at ||
            (dx_at && (dx_at > bytes || n > (bytes - dx_at) / 4)))
            return FALSE;
        return ExtTextOut(dc, EMF_LONG(36), EMF_LONG(40), lx_guest_load_u32(p + 52),
                          (PVOID)(p + 56), (PCHAR)(p + text_at), n,
                          dx_at ? (PLONG)(p + dx_at) : NULL);
    }
    if (type == 70 && bytes >= 16)
    {
        ULONG cb = lx_guest_load_u32(p + 8);
        if (cb > bytes - 12)
            return FALSE;
        if (cb >= 24 && bytes >= 36 && lx_guest_load_u32(p + 12) == 0x43494447 &&
            lx_guest_load_u32(p + 16) == 0x80000001)
        {
            ULONG n = lx_guest_load_u32(p + 32);
            if (cb < 24 || n > cb - 24)
                return FALSE;
            ULONG wmf = SetMetaFileBitsEx(n, (PVOID)(p + 36));
            BOOL ok = wmf && PlayMetaFile(dc, wmf);
            if (wmf)
                DeleteMetaFile(wmf);
            return ok;
        }
        return TRUE;
    }
    if (type >= 85 && type <= 91 && bytes >= 28)
    {
        /* The 16-bit point forms widen into the matching 32-bit record. */
        static const uint8 wide_types[] = {2, 3, 4, 5, 6, 7, 8};
        BOOL multiple = type >= 90;
        ULONG polygons = multiple && bytes >= 32 ? lx_guest_load_u32(p + 24) : 0,
              points = lx_guest_load_u32(p + (multiple ? 28 : 24));
        if ((multiple && bytes < 32) || polygons > 65536 || points > 65536)
        {
            open32SetError(13);
            return FALSE;
        }
        ULONG head = multiple ? 32 + polygons * 4 : 28;
        if (head > bytes || points > (bytes - head) / 4)
        {
            open32SetError(13);
            return FALSE;
        }
        uint8 *wide = malloc(head + (size_t)points * 8);
        if (!wide)
        {
            open32SetError(8);
            return FALSE;
        }
        memcpy(wide, p, head);
        lx_guest_store_u32(wide, wide_types[type - 85]);
        lx_guest_store_u32(wide + 4, head + points * 8);
        for (ULONG i = 0; i < points; i++)
        {
            lx_guest_store_u32(wide + head + i * 8, (ULONG)signed_word(p + head + i * 4));
            lx_guest_store_u32(wide + head + i * 8 + 4, (ULONG)signed_word(p + head + i * 4 + 2));
        }
        BOOL result = PlayEnhMetaFileRecord(dc, table, wide, count);
        free(wide);
        return result;
    }
    if ((type == 2 || type == 5 || type == 6) && bytes >= 28)
    {
        ULONG n = lx_guest_load_u32(p + 24);
        if (n > 65536 || n > (bytes - 28) / 8)
        {
            open32SetError(13);
            return FALSE;
        }
        if (type != 6)
            return emf_bezier(dc, p + 28, n, type == 5);
        BOOL result = TRUE;
        for (ULONG i = 0; result && i < n; i++)
            result = LineTo(dc, (LONG)lx_guest_load_u32(p + 28 + i * 8),
                            (LONG)lx_guest_load_u32(p + 32 + i * 8));
        return result;
    }
    if (type == 13 && bytes >= 16)
        return SetBrushOrgEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if ((type == 29 || type == 30) && bytes >= 24)
        return (type == 29
                    ? ExcludeClipRect(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20))
                    : IntersectClipRect(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16),
                                        EMF_LONG(20))) != 0;
    if (type == 26 && bytes >= 16)
    {
        ULONG region = CreateRectRgn(0, 0, 0, 0);
        if (!region)
            return FALSE;
        LONG selected = GetClipRgn(dc, region);
        BOOL result = selected >= 0;
        if (selected > 0)
            result = OffsetRgn(region, EMF_LONG(8), EMF_LONG(12)) && SelectClipRgn(dc, region);
        DeleteObject(region);
        return result;
    }
    if ((type == 31 || type == 32) && bytes >= 24)
        return type == 31 ? ScaleViewportExtEx(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16),
                                               EMF_LONG(20), NULL)
                          : ScaleWindowExtEx(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16),
                                             EMF_LONG(20), NULL);
    if (type == 35 && bytes >= 32)
    {
        /* Only the identity world transform can be represented by DC mapping. */
        static const float identity[6] = {1, 0, 0, 1, 0, 0};
        float matrix[6];
        memcpy(matrix, p + 8, sizeof matrix);
        if (!memcmp(matrix, identity, sizeof matrix))
            return TRUE;
    }
    if (type == 36 && bytes >= 36 && lx_guest_load_u32(p + 32) == 1)
        return TRUE;
    if (type == 57 && bytes >= 12 && EMF_LONG(8) == 1)
        return TRUE;
    if (type == 46 && bytes >= 40)
        return open32Chord(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                           EMF_LONG(28), EMF_LONG(32), EMF_LONG(36));
    if (type == 61)
        return open32PathClose(open32FindDC(dc));
    if (type == 62 || type == 63)
        return open32PathFill(dc, type == 63);
    if (type == 68)
    {
        open32PathCleanup(open32FindDC(dc));
        return TRUE;
    }
    if (type == 48 && bytes >= 12)
    {
        ULONG index = lx_guest_load_u32(p + 8), object = 0;
        if (index & 0x80000000)
            object = GetStockObject((LONG)(index & 0x7fffffff));
        else if (index < count)
            object = lx_guest_load_u32((uint8 *)table + index * 4);
        return object && SelectPalette(dc, object, FALSE) != 0;
    }
    if (type == 49 && bytes >= 16)
    {
        if (lx_guest_load_u16(p + 14) > (bytes - 16) / 4)
        {
            open32SetError(13);
            return FALSE;
        }
        return emf_object(table, count, lx_guest_load_u32(p + 8), CreatePalette((PVOID)(p + 12)));
    }
    if ((type == 50 && bytes >= 20) || (type == 51 && bytes >= 16))
    {
        ULONG index = lx_guest_load_u32(p + 8);
        ULONG object = index < count ? lx_guest_load_u32((uint8 *)table + index * 4) : 0;
        if (!object)
        {
            open32SetError(13);
            return FALSE;
        }
        if (type == 51)
            return ResizePalette(object, lx_guest_load_u32(p + 12));
        ULONG entries = lx_guest_load_u32(p + 16);
        if (entries > (bytes - 20) / 4)
        {
            open32SetError(13);
            return FALSE;
        }
        SetPaletteEntries(object, lx_guest_load_u32(p + 12), entries, (PVOID)(p + 20));
        return TRUE;
    }
    if ((type == 71 && bytes >= 32) || (type == 72 && bytes >= 40) || (type == 73 && bytes >= 28) ||
        (type == 74 && bytes >= 28) || (type == 75 && bytes >= 16))
    {
        ULONG size = lx_guest_load_u32(p + (type == 75 ? 8 : 24));
        ULONG at = type == 71 ? 32 : type == 72 ? 40 : type == 75 ? 16 : 28;
        if (size > bytes - at)
        {
            open32SetError(13);
            return FALSE;
        }
        if (type == 75 && !size)
            return ExtSelectClipRgn(dc, 0, EMF_LONG(12)) != 0;
        ULONG region = emf_region(p + at, size), brush = 0;
        if (!region)
            return FALSE;
        if (type == 71 || type == 72)
        {
            ULONG index = lx_guest_load_u32(p + 28);
            if (index & 0x80000000)
                brush = GetStockObject((LONG)(index & 0x7fffffff));
            else if (index < count)
                brush = lx_guest_load_u32((uint8 *)table + index * 4);
        }
        BOOL result = FALSE;
        if (type == 71)
            result = brush && FillRgn(dc, region, brush);
        else if (type == 72)
            result = brush && FrameRgn(dc, region, brush, EMF_LONG(32), EMF_LONG(36));
        else if (type == 74)
            result = PaintRgn(dc, region);
        else if (type == 75)
            result = ExtSelectClipRgn(dc, region, EMF_LONG(12)) != 0;
        else
        {
            ULONG n = lx_guest_load_u32(p + at + 8);
            result = TRUE;
            for (ULONG i = 0; result && i < n; i++)
                result = InvertRect(dc, (PVOID)(p + at + 32 + i * 16));
        }
        DeleteObject(region);
        return result;
    }
    if ((type == 76 && bytes >= 100) || (type == 77 && bytes >= 108))
    {
        ULONG operation = lx_guest_load_u32(p + 40), usage = lx_guest_load_u32(p + 80),
              info_at = lx_guest_load_u32(p + 84), info_size = lx_guest_load_u32(p + 88),
              bits_at = lx_guest_load_u32(p + 92), bits_size = lx_guest_load_u32(p + 96);
        if (!info_size)
            return PatBlt(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(32), EMF_LONG(36), operation);
        if (!emf_dib(p, bytes, info_at, info_size, bits_at, bits_size, usage))
            return FALSE;
        LONG width = type == 77 ? EMF_LONG(100) : EMF_LONG(32),
             height = type == 77 ? EMF_LONG(104) : EMF_LONG(36);
        LONG rows = (LONG)lx_guest_load_u32(p + info_at + 8);
        /* Device source coordinates run from the top; bottom-up DIB rows run from the bottom. */
        LONG source_y = rows > 0 ? rows - EMF_LONG(48) - height : EMF_LONG(48);
        return StretchDIBits(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(32), EMF_LONG(36),
                             EMF_LONG(44), source_y, width, height, (PVOID)(p + bits_at),
                             (PVOID)(p + info_at), usage, operation) != 0;
    }
    if (type == 80 && bytes >= 76)
    {
        ULONG info_at = lx_guest_load_u32(p + 48), info_size = lx_guest_load_u32(p + 52),
              bits_at = lx_guest_load_u32(p + 56), bits_size = lx_guest_load_u32(p + 60),
              usage = lx_guest_load_u32(p + 64);
        size_t ib, pb;
        if (info_at > bytes || info_size > bytes - info_at || info_size < 40 || bits_at > bytes ||
            bits_size > bytes - bits_at ||
            !open32DibBuffers((PVOID)(p + info_at), lx_guest_load_u32(p + 68),
                              lx_guest_load_u32(p + 72), usage, &ib, &pb) ||
            ib > info_size || pb > bits_size)
        {
            open32SetError(13);
            return FALSE;
        }
        return SetDIBitsToDevice(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(40), EMF_LONG(44),
                                 EMF_LONG(32), EMF_LONG(36), lx_guest_load_u32(p + 68),
                                 lx_guest_load_u32(p + 72), (PVOID)(p + bits_at),
                                 (PVOID)(p + info_at), usage) != 0;
    }
    if (type == 94 && bytes >= 32)
    {
        ULONG usage = lx_guest_load_u32(p + 12), info_at = lx_guest_load_u32(p + 16),
              bits_at = lx_guest_load_u32(p + 24);
        if (!emf_dib(p, bytes, info_at, lx_guest_load_u32(p + 20), bits_at,
                     lx_guest_load_u32(p + 28), usage))
            return FALSE;
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          open32DibPatternBrush((PVOID)(p + info_at), (PVOID)(p + bits_at), usage));
    }
    if (type == 95 && bytes >= 52)
    {
        uint8 brush[12];
        ULONG style = lx_guest_load_u32(p + 28);
        lx_guest_store_u32(brush, lx_guest_load_u32(p + 36));
        lx_guest_store_u32(brush + 4, lx_guest_load_u32(p + 40));
        lx_guest_store_u32(brush + 8, lx_guest_load_u32(p + 44));
        /* User style arrays cannot be selected through PMWINX; they replay as solid pens. */
        if ((style & 15) == 7)
            style &= ~15u;
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          ExtCreatePen(style, lx_guest_load_u32(p + 32), brush, 0, NULL));
    }
#undef EMF_LONG
    TRACE_NATIVE("Unsupported EMF record %u (%u bytes)", type, bytes);
    open32SetError(50);
    return FALSE;
}
static BOOL enhanced_bounds(ULONG dc, const Open32Metafile *file, PVOID bounds)
{
    if (!bounds)
        return TRUE;
    LONG l = (LONG)lx_guest_load_u32(bounds), t = (LONG)lx_guest_load_u32((uint8 *)bounds + 4),
         r = (LONG)lx_guest_load_u32((uint8 *)bounds + 8),
         b = (LONG)lx_guest_load_u32((uint8 *)bounds + 12);
    LONG fl = (LONG)lx_guest_load_u32(file->data + 8),
         ft = (LONG)lx_guest_load_u32(file->data + 12),
         fr = (LONG)lx_guest_load_u32(file->data + 16),
         fb = (LONG)lx_guest_load_u32(file->data + 20);
    int64_t w = (int64_t)r - l, h = (int64_t)b - t, fw = (int64_t)fr - fl, fh = (int64_t)fb - ft;
    if (!fw || !fh)
    {
        /* Without device bounds the frame, in 0.01 mm, gives the picture's extent. */
        int64_t device_w = (LONG)lx_guest_load_u32(file->data + 72),
                device_h = (LONG)lx_guest_load_u32(file->data + 76),
                metric_w = (LONG)lx_guest_load_u32(file->data + 80),
                metric_h = (LONG)lx_guest_load_u32(file->data + 84);
        if (metric_w <= 0 || metric_h <= 0)
            return TRUE;
        fl = 0;
        ft = 0;
        fw = ((int64_t)(LONG)lx_guest_load_u32(file->data + 32) -
              (LONG)lx_guest_load_u32(file->data + 24)) *
             device_w / (metric_w * 100);
        fh = ((int64_t)(LONG)lx_guest_load_u32(file->data + 36) -
              (LONG)lx_guest_load_u32(file->data + 28)) *
             device_h / (metric_h * 100);
        if (!fw || !fh)
            return TRUE;
    }
    if (!w || !h || w < INT32_MIN || w > INT32_MAX || h < INT32_MIN || h > INT32_MAX ||
        fw < INT32_MIN || fw > INT32_MAX || fh < INT32_MIN || fh > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    return SetMapMode(dc, 8) && SetWindowOrgEx(dc, fl, ft, NULL) &&
           SetWindowExtEx(dc, (LONG)fw, (LONG)fh, NULL) && SetViewportOrgEx(dc, l, t, NULL) &&
           SetViewportExtEx(dc, (LONG)w, (LONG)h, NULL);
}
BOOL PlayEnhMetaFile(ULONG dc, ULONG handle, PVOID bounds)
{
    LxHandleBorrow borrow = {0};
    if (bounds &&
        (lx_guest_load_u32(bounds) == lx_guest_load_u32((uint8 *)bounds + 8) ||
         lx_guest_load_u32((uint8 *)bounds + 4) == lx_guest_load_u32((uint8 *)bounds + 12)))
    {
        open32SetError(87);
        return FALSE;
    }
    if (!open32FindDC(dc) || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    Open32Metafile *f = borrow.resource;
    ULONG count = lx_guest_load_u16(f->data + 56);
    uint8 *table = calloc(count ? count : 1, 4);
    LONG saved = SaveDC(dc);
    BOOL ok = table && saved > 0;
    if (ok)
        ok = enhanced_bounds(dc, f, bounds);
    if (bounds)
        fitted_playback++;
    for (ULONG offset = 0; ok && offset < f->size;)
    {
        ULONG bytes = lx_guest_load_u32(f->data + offset + 4);
        ok = PlayEnhMetaFileRecord(dc, table, f->data + offset, count);
        offset += bytes;
    }
    if (bounds)
        fitted_playback--;
    if (saved && !RestoreDC(dc, saved))
        ok = FALSE;
    if (table)
    {
        for (ULONG i = 1; i < count; i++)
        {
            ULONG object = lx_guest_load_u32(table + i * 4);
            if (object)
                DeleteObject(object);
        }
        free(table);
    }
    lx_handle_release(&borrow);
    return ok;
}
BOOL EnumEnhMetaFile(ULONG dc, ULONG handle, ULONG procedure, ULONG parameter, PVOID bounds)
{
    LxHandleBorrow borrow = {0};
    LxExecution *execution = GLoaderState.execution;
    if ((dc && (!open32FindDC(dc) || !bounds)) || !open32ValidProcedure(procedure) ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(87);
        return FALSE;
    }
    if (bounds &&
        (lx_guest_load_u32(bounds) == lx_guest_load_u32((uint8 *)bounds + 8) ||
         lx_guest_load_u32((uint8 *)bounds + 4) == lx_guest_load_u32((uint8 *)bounds + 12)))
    {
        lx_handle_release(&borrow);
        return FALSE;
    }
    Open32Metafile *f = borrow.resource;
    ULONG count = lx_guest_load_u16(f->data + 56), maximum = 0;
    for (ULONG at = 0; at < f->size;)
    {
        ULONG bytes = lx_guest_load_u32(f->data + at + 4);
        if (bytes > maximum)
            maximum = bytes;
        at += bytes;
    }
    ULONG table_bytes = count ? count * 4 : 4;
    size_t size = (table_bytes + (size_t)maximum + 4095) & ~(size_t)4095;
    LxGuestMapping *data = lx_execution_map(execution, 0, size, 3, 0),
                   *code = data ? lx_execution_map(execution, 0, 4096, 3, 0) : NULL;
    LONG saved = dc && bounds ? SaveDC(dc) : 0;
    BOOL ok = data && code && (!(dc && bounds) || (saved > 0 && enhanced_bounds(dc, f, bounds)));
    if (ok)
    {
        uint8 thunk[] = {0x55, 0x89, 0xe5, 0x68, 0,    0,    0,    0,    0xff, 0x75, 0x14,
                         0xff, 0x75, 0x10, 0xff, 0x75, 0x0c, 0xff, 0x75, 0x08, 0xb8, 0,
                         0,    0,    0,    0xff, 0xd0, 0x89, 0xec, 0x5d, 0xc3};
        lx_guest_store_u32(thunk + 4, parameter);
        lx_guest_store_u32(thunk + 21, procedure);
        memcpy(lx_execution_mapping_host(code), thunk, sizeof thunk);
        ok = lx_execution_protect(execution, code, LX_GUEST_READ | LX_GUEST_EXECUTE);
    }
    if (ok)
    {
        ULONG address = lx_execution_mapping_address(data);
        uint8 *objects = lx_execution_mapping_host(data), *record = objects + table_bytes;
        if (dc && bounds)
            fitted_playback++;
        for (ULONG at = 0; ok && at < f->size;)
        {
            ULONG bytes = lx_guest_load_u32(f->data + at + 4);
            memcpy(record, f->data + at, bytes);
            ok = lx_execution_callback32_4(execution, lx_execution_mapping_address(code), dc,
                                           address, address + table_bytes, count) != 0;
            at += bytes;
        }
        if (dc && bounds)
            fitted_playback--;
        /* Objects PlayEnhMetaFileRecord left in the guest table end with the enumeration. */
        for (ULONG i = 1; i < count; i++)
        {
            ULONG object = lx_guest_load_u32(objects + i * 4);
            if (object)
                DeleteObject(object);
        }
    }
    if (code)
        lx_execution_unmap(execution, code);
    if (data)
        lx_execution_unmap(execution, data);
    if (saved && !RestoreDC(dc, saved))
        ok = FALSE;
    lx_handle_release(&borrow);
    return ok;
}
ULONG SetWinMetaFileBits(ULONG size, PVOID data, ULONG dc, PVOID picture)
{
    if ((dc && !open32FindDC(dc)) || !valid_wmf(data, size))
    {
        open32SetError(13);
        return 0;
    }
    size_t comment = (36 + (size_t)size + 3) & ~(size_t)3, total = 88 + comment + 20;
    uint8 *emf = calloc(total, 1);
    if (!emf)
    {
        open32SetError(8);
        return 0;
    }
    lx_guest_store_u32(emf, 1);
    lx_guest_store_u32(emf + 4, 88);
    /* The picture's bounds come from the WMF's own window, then METAFILEPICT, then the device. */
    LONG window_x = 0, window_y = 0, window_w = 0, window_h = 0;
    for (ULONG at = 18; at + 6 <= size;)
    {
        const uint8 *record = (const uint8 *)data + at;
        ULONG words = lx_guest_load_u32(record);
        USHORT function = lx_guest_load_u16(record + 4);
        if (!function)
            break;
        if (function == 0x20b && words == 5)
        {
            window_y = signed_word(record + 6);
            window_x = signed_word(record + 8);
        }
        else if (function == 0x20c && words == 5 && !window_w)
        {
            window_h = signed_word(record + 6);
            window_w = signed_word(record + 8);
        }
        at += words * 2;
    }
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
    lx_guest_store_u32(emf + 8, (ULONG)window_x);
    lx_guest_store_u32(emf + 12, (ULONG)window_y);
    lx_guest_store_u32(emf + 16, (ULONG)(window_x + window_w));
    lx_guest_store_u32(emf + 20, (ULONG)(window_y + window_h));
    lx_guest_store_u32(emf + 32, (ULONG)frame_w);
    lx_guest_store_u32(emf + 36, (ULONG)frame_h);
    lx_guest_store_u32(emf + 40, 0x464d4520);
    lx_guest_store_u32(emf + 44, 0x10000);
    lx_guest_store_u32(emf + 48, (ULONG)total);
    lx_guest_store_u32(emf + 52, 3);
    lx_guest_store_u16(emf + 56, 1);
    lx_guest_store_u32(emf + 72, 1024);
    lx_guest_store_u32(emf + 76, 768);
    lx_guest_store_u32(emf + 80, 270);
    lx_guest_store_u32(emf + 84, 203);
    uint8 *p = emf + 88;
    lx_guest_store_u32(p, 70);
    lx_guest_store_u32(p + 4, (ULONG)comment);
    lx_guest_store_u32(p + 8, size + 24);
    lx_guest_store_u32(p + 12, 0x43494447);
    lx_guest_store_u32(p + 16, 0x80000001);
    lx_guest_store_u32(p + 20, 0x300);
    lx_guest_store_u32(p + 32, size);
    memcpy(p + 36, data, size);
    p += comment;
    lx_guest_store_u32(p, 14);
    lx_guest_store_u32(p + 4, 20);
    lx_guest_store_u32(p + 16, 20);
    ULONG result = SetEnhMetaFileBits((ULONG)total, emf);
    free(emf);
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

void open32MetafileColor(ULONG dc, USHORT function, ULONG color)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    uint8 bytes[4];
    lx_guest_store_u32(bytes, color);
    append_record(r, function, bytes, 4);
}

BOOL open32IsMetafile(ULONG dc)
{
    return recorder(dc) != NULL;
}
void open32MetafileDib(ULONG dc, LONG x, LONG y, LONG w, LONG h, LONG sx, LONG sy, LONG sw, LONG sh,
                       PVOID bits, PVOID information, ULONG usage, ULONG operation)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    size_t ib, pb;
    if (!open32DibBuffers(information, 0, UINT32_MAX, usage, &ib, &pb) ||
        ib + pb > 64u * 1024 * 1024 - 22)
    {
        r->failed = TRUE;
        return;
    }
    LONG coords[] = {sh, sw, sy, sx, h, w, y, x};
    for (unsigned i = 0; i < 8; i++)
        if (coords[i] < INT16_MIN || coords[i] > INT16_MAX)
        {
            r->failed = TRUE;
            return;
        }
    uint8 *p = malloc(22 + ib + pb);
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u32(p, operation);
    lx_guest_store_u16(p + 4, (uint16)usage);
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u16(p + 6 + i * 2, (uint16)coords[i]);
    memcpy(p + 22, information, ib);
    memcpy(p + 22 + ib, bits, pb);
    append_record(r, 0xf43, p, 22 + ib + pb);
    free(p);
}
void open32MetafilePatBlt(ULONG dc, LONG x, LONG y, LONG w, LONG h, ULONG operation)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    LONG values[] = {h, w, y, x};
    uint8 p[12];
    lx_guest_store_u32(p, operation);
    for (unsigned i = 0; i < 4; i++)
    {
        if (values[i] < INT16_MIN || values[i] > INT16_MAX)
        {
            r->failed = TRUE;
            return;
        }
        lx_guest_store_u16(p + 4 + i * 2, (uint16)values[i]);
    }
    append_record(r, 0x61d, p, 12);
}

BOOL open32MetafilePolyPolygon(ULONG dc, PVOID points, PVOID counts, LONG polygons)
{
    Open32Recorder *r = recorder(dc);
    if (!r || polygons < 1 || polygons > 65535)
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG total = 0;
    for (LONG i = 0; i < polygons; i++)
    {
        ULONG n = lx_guest_load_u32((uint8 *)counts + i * 4);
        if (n < 2 || n > 65535 - total)
        {
            open32SetError(87);
            return FALSE;
        }
        total += n;
    }
    size_t bytes = 2 + (size_t)polygons * 2 + (size_t)total * 4;
    uint8 *p = malloc(bytes);
    if (!p)
    {
        open32SetError(8);
        return FALSE;
    }
    lx_guest_store_u16(p, (uint16)polygons);
    for (LONG i = 0; i < polygons; i++)
        lx_guest_store_u16(p + 2 + i * 2, (uint16)lx_guest_load_u32((uint8 *)counts + i * 4));
    BOOL ok = TRUE;
    for (ULONG i = 0; i < total; i++)
        for (unsigned k = 0; k < 2; k++)
        {
            LONG value = (LONG)lx_guest_load_u32((uint8 *)points + i * 8 + k * 4);
            if (value < INT16_MIN || value > INT16_MAX)
            {
                ok = FALSE;
                break;
            }
            lx_guest_store_u16(p + 2 + polygons * 2 + i * 4 + k * 2, (uint16)value);
        }
    if (ok)
        ok = append_record(r, 0x538, p, bytes);
    free(p);
    if (!ok)
        open32SetError(87);
    return ok;
}

void open32MetafilePixel(ULONG dc, LONG x, LONG y, ULONG color)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX)
    {
        r->failed = TRUE;
        return;
    }
    uint8 p[8];
    lx_guest_store_u32(p, color);
    lx_guest_store_u16(p + 4, (uint16)y);
    lx_guest_store_u16(p + 6, (uint16)x);
    append_record(r, 0x41f, p, 8);
}
