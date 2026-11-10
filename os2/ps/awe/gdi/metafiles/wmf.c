/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows metafiles: handles, validation, playback, enumeration and metafile files. */
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
    if (!data || size < WMF_SIZE_MIN || size > METAFILE_SIZE_MAX || (size & 1))
        return FALSE;
    USHORT type = lx_guest_load_u16(data + WIN32_METAHEADER_TYPE),
           version = lx_guest_load_u16(data + WIN32_METAHEADER_VERSION);
    if ((type != WIN32_MEMORYMETAFILE && type != WIN32_DISKMETAFILE) ||
        lx_guest_load_u16(data + WIN32_METAHEADER_HEADER_WORDS) != WIN32_METAHEADER_SIZE / 2 ||
        (version != WIN32_METAVERSION100 && version != WIN32_METAVERSION300) ||
        lx_guest_load_u32(data + WIN32_METAHEADER_FILE_WORDS) != size / 2 ||
        lx_guest_load_u16(data + WIN32_METAHEADER_PARAMETERS))
        return FALSE;
    ULONG maximum = lx_guest_load_u32(data + WIN32_METAHEADER_MAX_RECORD_WORDS);
    if (maximum < WIN32_METARECORD_MIN_WORDS || maximum > (size - WIN32_METAHEADER_SIZE) / 2)
        return FALSE;
    for (ULONG offset = WIN32_METAHEADER_SIZE; offset <= size - WIN32_METARECORD_PARAMETERS;)
    {
        ULONG words = lx_guest_load_u32(data + offset + WIN32_METARECORD_WORDS);
        USHORT function = lx_guest_load_u16(data + offset + WIN32_METARECORD_FUNCTION);
        if (words < WIN32_METARECORD_MIN_WORDS || words > maximum || words > (size - offset) / 2)
            return FALSE;
        if (!function)
            return words == WIN32_METARECORD_MIN_WORDS &&
                   offset + WIN32_METARECORD_PARAMETERS == size;
        offset += words * 2;
    }
    return FALSE;
}

ULONG SetMetaFileBitsEx(ULONG size, PVOID data)
{
    TRACE_NATIVE("SetMetaFileBitsEx(%u, %p)", size, data);
    if (!data || size < WMF_SIZE_MIN || size > METAFILE_SIZE_MAX || (size & 1))
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
    if (available < WIN32_BITMAPINFOHEADER_SIZE ||
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
    ULONG scans = lx_guest_load_u16(p + WMF_REGION_SCANS), at = WMF_REGION_SIZE;
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
                 CombineRgn(region, region, part, WIN32_RGN_OR);
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
    ULONG n = lx_guest_load_u16(p + 4),
          options = lx_guest_load_u16(p + 6) & (WIN32_ETO_OPAQUE | WIN32_ETO_CLIPPED);
    ULONG text_at = options ? 16 : 8, padded = (n + 1) & ~1u,
          parameters = bytes - WIN32_METARECORD_PARAMETERS;
    if (text_at > parameters || padded > parameters - text_at)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    uint8 rectangle[WIN32_RECT_SIZE];
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
    ULONG bytes = lx_guest_load_u32(record + WIN32_METARECORD_WORDS) * 2;
    USHORT function = lx_guest_load_u16(record + WIN32_METARECORD_FUNCTION);
    const UCHAR *p = record + WIN32_METARECORD_PARAMETERS;
    /* bytes counts the 6-byte record header and two bytes per parameter word, so each check
       below also fixes or bounds the record's parameter count. */
    if (!function)
        return TRUE;
    if (function == WIN32_META_SETMAPMODE && bytes == 8)
        return SetMapMode(dc, metafile_word(p)) != 0;
    if (function == WIN32_META_SETWINDOWORG && bytes == 10)
        return SetWindowOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == WIN32_META_SETWINDOWEXT && bytes == 10)
        return SetWindowExtEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == WIN32_META_STRETCHDIB && bytes >= 68)
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
    if (function == WIN32_META_SETBKMODE && bytes == 8)
        return SetBkMode(dc, metafile_word(p)) != 0;
    if (function == WIN32_META_SETROP2 && bytes == 8)
        return SetROP2(dc, metafile_word(p)) != 0;
    if (function == WIN32_META_SETPOLYFILLMODE && bytes == 8)
        return SetPolyFillMode(dc, metafile_word(p)) != 0;
    if (function == WIN32_META_SETSTRETCHBLTMODE && bytes == 8)
        return SetStretchBltMode(dc, metafile_word(p)) != 0;
    if (function == WIN32_META_SETTEXTCHAREXTRA && bytes == 8)
        return SetTextCharacterExtra(dc, metafile_word(p)) != INT32_MIN;
    if (function == WIN32_META_SETBKCOLOR && bytes == 10)
        return SetBkColor(dc, lx_guest_load_u32(p)) != UINT32_MAX;
    if (function == WIN32_META_SETTEXTCOLOR && bytes == 10)
        return SetTextColor(dc, lx_guest_load_u32(p)) != UINT32_MAX;
    if (function == WIN32_META_SETTEXTJUSTIFICATION && bytes == 10)
        return SetTextJustification(dc, metafile_word(p + 2), metafile_word(p));
    if (function == WIN32_META_SETVIEWPORTORG && bytes == 10)
        return SetViewportOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == WIN32_META_SETVIEWPORTEXT && bytes == 10)
        return SetViewportExtEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == WIN32_META_MOVETO && bytes == 10)
        return MoveToEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if (function == WIN32_META_LINETO && bytes == 10)
        return LineTo(dc, metafile_word(p + 2), metafile_word(p));
    if (function == WIN32_META_SAVEDC && bytes == 6)
        return SaveDC(dc) != 0;
    if (function == WIN32_META_RESTOREDC && bytes == 8)
        return RestoreDC(dc, metafile_word(p));
    if ((function == WIN32_META_ELLIPSE || function == WIN32_META_RECTANGLE) && bytes == 14)
        return function == WIN32_META_ELLIPSE
                   ? Ellipse(dc, metafile_word(p + 6), metafile_word(p + 4), metafile_word(p + 2),
                             metafile_word(p))
                   : Rectangle(dc, metafile_word(p + 6), metafile_word(p + 4), metafile_word(p + 2),
                               metafile_word(p));
    if (function == WIN32_META_ROUNDRECT && bytes == 18)
        return RoundRect(dc, metafile_word(p + 10), metafile_word(p + 8), metafile_word(p + 6),
                         metafile_word(p + 4), metafile_word(p + 2), metafile_word(p));
    if (function == WIN32_META_CHORD && bytes == 22)
        return open32Chord(dc, metafile_word(p + 14), metafile_word(p + 12), metafile_word(p + 10),
                           metafile_word(p + 8), metafile_word(p + 6), metafile_word(p + 4),
                           metafile_word(p + 2), metafile_word(p));
    if ((function == WIN32_META_ARC || function == WIN32_META_PIE) && bytes == 22)
        return function == WIN32_META_ARC
                   ? Arc(dc, metafile_word(p + 14), metafile_word(p + 12), metafile_word(p + 10),
                         metafile_word(p + 8), metafile_word(p + 6), metafile_word(p + 4),
                         metafile_word(p + 2), metafile_word(p))
                   : Pie(dc, metafile_word(p + 14), metafile_word(p + 12), metafile_word(p + 10),
                         metafile_word(p + 8), metafile_word(p + 6), metafile_word(p + 4),
                         metafile_word(p + 2), metafile_word(p));
    if ((function == WIN32_META_POLYGON || function == WIN32_META_POLYLINE) && bytes >= 8)
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
        BOOL ok = function == WIN32_META_POLYGON ? Polygon(dc, points, (LONG)n)
                                                 : Polyline(dc, points, (LONG)n);
        free(points);
        return ok;
    }
    if (function == WIN32_META_POLYPOLYGON && bytes >= 8)
    {
        ULONG polygons = lx_guest_load_u16(p);
        if (polygons > (bytes - 8) / 2)
            return FALSE;
        ULONG total = 0;
        for (ULONG i = 0; i < polygons; i++)
            total += lx_guest_load_u16(p + 2 + i * 2);
        if (total > METAFILE_POINTS_MAX || total > (bytes - 8 - polygons * 2) / 4)
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
    if (function == WIN32_META_TEXTOUT && bytes >= 12)
    {
        ULONG n = lx_guest_load_u16(p), padded = (n + 1) & ~1u;
        if (padded > bytes - 12)
            return FALSE;
        return TextOut(dc, metafile_word(p + 4 + padded), metafile_word(p + 2 + padded),
                       (PSZ)(p + 2), (LONG)n);
    }
    if (function == WIN32_META_SELECTOBJECT && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p),
              object = index & WMF_STOCK_OBJECT_INDEX ? GetStockObject((LONG)(index & 0x7fff))
                       : index < count ? lx_guest_load_u32((uint8 *)table + index * 4)
                                       : 0;
        return object && SelectObject(dc, object) != 0;
    }
    if (function == WIN32_META_DELETEOBJECT && bytes == 8)
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
    if (function == WIN32_META_CREATEPENINDIRECT || function == WIN32_META_CREATEBRUSHINDIRECT ||
        function == WIN32_META_CREATEFONTINDIRECT || function == WIN32_META_CREATEPALETTE ||
        function == WIN32_META_DIBCREATEPATTERNBRUSH || function == WIN32_META_CREATEREGION)
    {
        ULONG object = 0;
        if (function == WIN32_META_CREATEREGION && bytes >= 28)
            object = wmf_region(p, bytes - 6);
        else if (function == WIN32_META_DIBCREATEPATTERNBRUSH && bytes >= 50)
        {
            size_t info_bytes, pixel_bytes;
            ULONG usage = lx_guest_load_u16(p + 2);
            if (open32DibBuffers((PVOID)(p + 4), 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) &&
                info_bytes + pixel_bytes <= bytes - 10)
                object = open32DibPatternBrush((PVOID)(p + 4), (PVOID)(p + 4 + info_bytes), usage,
                                               lx_guest_load_u16(p) == BS_PATTERN);
        }
        else if (function == WIN32_META_CREATEPALETTE && bytes >= 10 &&
                 lx_guest_load_u16(p + 2) <= (bytes - 10) / 4)
            object = CreatePalette((PVOID)p);
        else if (function == WIN32_META_CREATEPENINDIRECT && bytes == 16)
            object = CreatePen(metafile_word(p) & PS_STYLE_MASK, metafile_word(p + 2),
                               lx_guest_load_u32(p + 6));
        else if (function == WIN32_META_CREATEBRUSHINDIRECT && bytes == 14)
        {
            uint8 brush[WIN32_LOGBRUSH_SIZE];
            lx_guest_store_u32(brush, lx_guest_load_u16(p));
            memcpy(brush + 4, p + 2, 4);
            lx_guest_store_u32(brush + 8, lx_guest_load_u16(p + 6));
            object = CreateBrushIndirect(brush);
        }
        else if (function == WIN32_META_CREATEFONTINDIRECT &&
                 bytes >= WIN32_METARECORD_PARAMETERS + WIN32_LOGFONT16_FACE_NAME)
        {
            /* Records may end soon after the face name's terminator. The 16-bit size
               fields widen; lfItalic onward copies as it is. */
            uint8 font[WIN32_LOGFONT_SIZE] = {0};
            for (unsigned i = 0; i < WIN32_LOGFONT16_ITALIC / 2; i++)
                lx_guest_store_u32(font + i * 4, (ULONG)metafile_word(p + i * 2));
            ULONG available = bytes - WIN32_METARECORD_PARAMETERS - WIN32_LOGFONT16_ITALIC;
            memcpy(font + WIN32_LOGFONT_ITALIC, p + WIN32_LOGFONT16_ITALIC,
                   available < WIN32_LOGFONT_SIZE - WIN32_LOGFONT_ITALIC
                       ? available
                       : WIN32_LOGFONT_SIZE - WIN32_LOGFONT_ITALIC);
            font[WIN32_LOGFONT_SIZE - 1] = 0;
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
    if (function == WIN32_META_SETPIXEL && bytes == 14)
        return SetPixel(dc, metafile_word(p + 6), metafile_word(p + 4), lx_guest_load_u32(p)) !=
               UINT32_MAX;
    if (function == WIN32_META_PATBLT && bytes == 18)
        return PatBlt(dc, metafile_word(p + 10), metafile_word(p + 8), metafile_word(p + 6),
                      metafile_word(p + 4), lx_guest_load_u32(p));
    if (function == WIN32_META_SETTEXTALIGN && bytes == 8)
        return SetTextAlign(dc, lx_guest_load_u16(p)) != UINT32_MAX;
    if ((function == WIN32_META_OFFSETWINDOWORG || function == WIN32_META_OFFSETVIEWPORTORG) &&
        bytes == 10)
        return function == WIN32_META_OFFSETWINDOWORG
                   ? OffsetWindowOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL)
                   : OffsetViewportOrgEx(dc, metafile_word(p + 2), metafile_word(p), NULL);
    if ((function == WMF_SCALEWINDOWEXT_RECORD || function == WIN32_META_SCALEVIEWPORTEXT) &&
        bytes == 14)
        return function == WMF_SCALEWINDOWEXT_RECORD
                   ? ScaleWindowExtEx(dc, metafile_word(p + 6), metafile_word(p + 4),
                                      metafile_word(p + 2), metafile_word(p), NULL)
                   : ScaleViewportExtEx(dc, metafile_word(p + 6), metafile_word(p + 4),
                                        metafile_word(p + 2), metafile_word(p), NULL);
    if ((function == WIN32_META_EXCLUDECLIPRECT || function == WIN32_META_INTERSECTCLIPRECT) &&
        bytes == 14)
        return (function == WIN32_META_EXCLUDECLIPRECT
                    ? ExcludeClipRect(dc, metafile_word(p + 6), metafile_word(p + 4),
                                      metafile_word(p + 2), metafile_word(p))
                    : IntersectClipRect(dc, metafile_word(p + 6), metafile_word(p + 4),
                                        metafile_word(p + 2), metafile_word(p))) != 0;
    if (function == WIN32_META_EXTTEXTOUT && bytes >= 14)
        return wmf_text(dc, p, bytes);
    if (function == WIN32_META_SELECTPALETTE && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p);
        ULONG object = index < count ? lx_guest_load_u32((uint8 *)table + index * 4) : 0;
        return object && SelectPalette(dc, object, FALSE) != 0;
    }
    if ((function == WIN32_META_DIBBITBLT || function == WIN32_META_DIBSTRETCHBLT) && bytes >= 20)
    {
        ULONG head = function == WIN32_META_DIBBITBLT ? 16 : 20,
              parameters = bytes - WIN32_METARECORD_PARAMETERS;
        ULONG operation = lx_guest_load_u32(p);
        /* Without a DIB the record is a pattern transfer ending in h, w, y and x. */
        if (parameters < head + WIN32_BITMAPINFOHEADER_SIZE)
        {
            const UCHAR *q = p + parameters - 8;
            return PatBlt(dc, metafile_word(q + 6), metafile_word(q + 4), metafile_word(q + 2),
                          metafile_word(q), operation);
        }
        const UCHAR *q = p + head - 8, *source = p + 4;
        LONG height = metafile_word(q), width = metafile_word(q + 2);
        LONG source_height = function == WIN32_META_DIBBITBLT ? height : metafile_word(source);
        LONG source_width = function == WIN32_META_DIBBITBLT ? width : metafile_word(source + 2);
        const UCHAR *origin = function == WIN32_META_DIBBITBLT ? source : source + 4;
        return wmf_dib(dc, p + head, parameters - head, metafile_word(q + 6), metafile_word(q + 4),
                       width, height, metafile_word(origin + 2), metafile_word(origin),
                       source_width, source_height, 0, operation);
    }
    if (function == WIN32_META_SETDIBTODEV && bytes >= 24 + WIN32_BITMAPINFOHEADER_SIZE)
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
    if (function == WIN32_META_SELECTCLIPREGION && bytes == 8)
    {
        ULONG index = lx_guest_load_u16(p);
        ULONG object = index < count ? lx_guest_load_u32((uint8 *)table + index * 4) : 0;
        if (!(index & WMF_STOCK_OBJECT_INDEX) && !object)
            return FALSE;
        return SelectClipRgn(dc, index & WMF_STOCK_OBJECT_INDEX ? 0 : object) != 0;
    }
    if (function == WIN32_META_REALIZEPALETTE)
        return RealizePalette(dc) != UINT32_MAX;
    if ((function == WIN32_META_SETPALENTRIES || function == WIN32_META_ANIMATEPALETTE) &&
        bytes >= 10)
    {
        Open32DC *context = open32FindDC(dc);
        ULONG first = lx_guest_load_u16(p), entries = lx_guest_load_u16(p + 2);
        if (entries > (bytes - 10) / 4)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return function == WIN32_META_SETPALENTRIES
                   ? SetPaletteEntries(context->palette_handle, first, entries, (PVOID)(p + 4)) != 0
                   : AnimatePalette(context->palette_handle, first, entries, (PVOID)(p + 4));
    }
    if (function == WIN32_META_RESIZEPALETTE && bytes == 8)
        return ResizePalette(open32FindDC(dc)->palette_handle, lx_guest_load_u16(p));
    if (function == WIN32_META_ESCAPE && bytes >= 10)
    {
        /* A device may decline an escape; that is not a playback failure. */
        ULONG size = lx_guest_load_u16(p + 2);
        if (size > bytes - 10)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (lx_guest_load_u16(p) != WIN32_MFCOMMENT)
            Escape(dc, lx_guest_load_u16(p), (LONG)size, (PVOID)(p + 4), NULL);
        return TRUE;
    }
    /* SetRelAbs has no effect on Win32 drawing, and every host face is a scalable face that
       aspect filtering cannot exclude. */
    if (function == WIN32_META_SETRELABS || function == WIN32_META_SETMAPPERFLAGS)
        return TRUE;
    TRACE_NATIVE("Unsupported WMF record %04X (%u bytes)", function, bytes);
    open32SetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL PlayMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count)
{
    (void)table;
    if (!open32FindDC(dc) || !record || count > UINT16_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG words = lx_guest_load_u32(record);
    if (words < WIN32_METARECORD_MIN_WORDS || words > METAFILE_SIZE_MAX / 2)
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
    ULONG count = lx_guest_load_u16(file->data + WIN32_METAHEADER_OBJECTS);
    ULONG table_bytes = count ? count * 4 : 4;
    size_t record_bytes =
        (size_t)lx_guest_load_u32(file->data + WIN32_METAHEADER_MAX_RECORD_WORDS) * 2;
    size_t size =
        (table_bytes + record_bytes + OPEN32_PAGE_SIZE - 1) & ~(size_t)(OPEN32_PAGE_SIZE - 1);
    LxGuestMapping *data = lx_execution_map(execution, 0, size, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    LxGuestMapping *code =
        data ? lx_execution_map(execution, 0, OPEN32_PAGE_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0)
             : NULL;
    BOOL ok = data && code;
    if (ok)
    {
        /* The execution boundary has a four-word callback. This guest thunk
           forwards those four words and the fifth MFENUMPROC parameter. EBP
           restores its own frame regardless of the callback's stack cleanup. */
        UCHAR thunk[] = {0x55, 0x89, 0xe5, 0x68, 0,    0,    0,    0,    0xff, 0x75, 0x14,
                         0xff, 0x75, 0x10, 0xff, 0x75, 0x0c, 0xff, 0x75, 0x08, 0xb8, 0,
                         0,    0,    0,    0xff, 0xd0, 0x89, 0xec, 0x5d, 0xc3};
        lx_guest_store_u32(thunk + ENUM_THUNK_PARAMETER, parameter);
        lx_guest_store_u32(thunk + ENUM_THUNK_PROCEDURE, procedure);
        memcpy(lx_execution_mapping_host(code), thunk, sizeof thunk);
        ok = lx_execution_protect(execution, code, LX_GUEST_READ | LX_GUEST_EXECUTE);
    }
    if (!ok)
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    if (ok)
    {
        UCHAR *record = (UCHAR *)lx_execution_mapping_host(data) + table_bytes;
        ULONG table_address = lx_execution_mapping_address(data);
        for (ULONG offset = WIN32_METAHEADER_SIZE; ok && offset < file->size;)
        {
            ULONG bytes = lx_guest_load_u32(file->data + offset + WIN32_METARECORD_WORDS) * 2;
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
    ULONG count = lx_guest_load_u16(file->data + WIN32_METAHEADER_OBJECTS);
    uint8 *table = calloc(count ? count : 1, 4);
    BOOL ok = saved > 0 && table;
    /* A refused record does not stop playback; only malformed framing ends it early. */
    for (size_t offset = WIN32_METAHEADER_SIZE;
         ok && offset + WIN32_METARECORD_PARAMETERS <= file->size;)
    {
        const UCHAR *record = file->data + offset;
        size_t bytes = (size_t)lx_guest_load_u32(record + WIN32_METARECORD_WORDS) * 2;
        if (bytes < WIN32_METARECORD_PARAMETERS || bytes > file->size - offset)
        {
            open32SetError(ERROR_INVALID_DATA);
            ok = FALSE;
            break;
        }
        if (!lx_guest_load_u16(record + WIN32_METARECORD_FUNCTION))
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
    uint8 of[WIN32_OFSTRUCT_SIZE];
    LONG file = OpenFile(name, of, WIN32_OF_CREATE | WIN32_OF_READWRITE);
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
    uint8 of[WIN32_OFSTRUCT_SIZE];
    LONG file = OpenFile(name, of, WIN32_OF_READ);
    if (file < 0)
        return 0;
    LONG size = _llseek((ULONG)file, 0, WIN32_FILE_END);
    ULONG result = 0;
    /* WMF_SIZE_MIN admits the smallest WMF; SetMetaFileBitsEx or SetEnhMetaFileBits
       validates the rest. */
    if (size >= WMF_SIZE_MIN && size <= METAFILE_SIZE_MAX &&
        _llseek((ULONG)file, 0, WIN32_FILE_BEGIN) == 0)
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
