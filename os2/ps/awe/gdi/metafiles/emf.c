/* Public domain. No rights reserved. Wholly AI-authored. */
/* Enhanced metafiles: handles, headers, record playback and enumeration. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/common/pm_text.h"
#include <math.h>
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"

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
    lx_host_once(&metafile_once, metafile_initialize);
    Open32Metafile *f = metafile_monitor ? malloc(sizeof *f + size) : NULL;
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
    lx_host_monitor_lock(metafile_monitor);
    f->handle = lx_handle_create(LX_HANDLE_OPEN32_ENHMETAFILE, f, metafile_destroy_file);
    if (f->handle)
    {
        f->next = metafile_files;
        metafile_files = f;
    }
    lx_host_monitor_unlock(metafile_monitor);
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
    return metafile_read(name, TRUE);
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

/* Handlers in PMWINX's record table that return 0 and draw nothing: types 0, 1 (header),
   23, 28, 69, 79 and 96 to 101. */
static BOOL emf_inert(ULONG type)
{
    return type <= 1 || type == 23 || type == 28 || type == 69 || type == 79 ||
           (type >= 96 && type <= 101);
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

/* MaskBlt: where the mask bit is set the foreground ROP applies, elsewhere the background ROP.
   Each pass clips to its pels, as device-space row runs of the target rectangle. */
static BOOL emf_mask_blt(ULONG dc, const uint8 *p, ULONG bytes)
{
    ULONG usage = lx_guest_load_u32(p + 80), info_at = lx_guest_load_u32(p + 84),
          info_size = lx_guest_load_u32(p + 88), bits_at = lx_guest_load_u32(p + 92),
          bits_size = lx_guest_load_u32(p + 96), operation = lx_guest_load_u32(p + 40),
          mask_info_at = lx_guest_load_u32(p + 112), mask_info_size = lx_guest_load_u32(p + 116),
          mask_bits_at = lx_guest_load_u32(p + 120), mask_bits_size = lx_guest_load_u32(p + 124);
    if (!emf_dib(p, bytes, mask_info_at, mask_info_size, mask_bits_at, mask_bits_size, 0) ||
        (info_size && !emf_dib(p, bytes, info_at, info_size, bits_at, bits_size, usage)) ||
        lx_guest_load_u16(p + mask_info_at + 14) != 1)
    {
        open32SetError(13);
        return FALSE;
    }
    LONG x = (LONG)lx_guest_load_u32(p + 24), y = (LONG)lx_guest_load_u32(p + 28),
         width = (LONG)lx_guest_load_u32(p + 32), height = (LONG)lx_guest_load_u32(p + 36),
         source_x = (LONG)lx_guest_load_u32(p + 44), source_y = (LONG)lx_guest_load_u32(p + 48),
         mask_x = (LONG)lx_guest_load_u32(p + 100), mask_y = (LONG)lx_guest_load_u32(p + 104);
    LONG mask_width = (LONG)lx_guest_load_u32(p + mask_info_at + 4),
         mask_rows = (LONG)lx_guest_load_u32(p + mask_info_at + 8);
    size_t stride = ((size_t)(mask_width > 0 ? mask_width : 0) + 31) / 32 * 4;
    const uint8 *mask = p + mask_bits_at;
    Open32DC *context = open32FindDC(dc);
    BOOL ok = TRUE;
    for (unsigned pass = 0; ok && pass < 2; pass++)
    {
        ULONG code = pass ? (operation >> 8) & 0xFF0000 : operation & 0xFF0000;
        ULONG region = CreateRectRgn(0, 0, 0, 0), run = region ? CreateRectRgn(0, 0, 0, 0) : 0;
        ok = run != 0;
        for (LONG v = 0; ok && v < height; v++)
        {
            LONG row = mask_rows > 0 ? mask_rows - 1 - (mask_y + v) : mask_y + v;
            LONG absolute = mask_rows < 0 ? -mask_rows : mask_rows;
            if (row < 0 || row >= absolute)
                continue;
            for (LONG u = 0; ok && u < width;)
            {
                LONG bit_at = mask_x + u;
                BOOL set = bit_at >= 0 && bit_at < mask_width &&
                           (mask[(size_t)row * stride + bit_at / 8] >> (7 - bit_at % 8)) & 1;
                LONG end = u + 1;
                while (end < width)
                {
                    LONG next = mask_x + end;
                    BOOL same = next >= 0 && next < mask_width &&
                                (mask[(size_t)row * stride + next / 8] >> (7 - next % 8)) & 1;
                    if (same != set)
                        break;
                    end++;
                }
                if (set == (pass == 0))
                {
                    LONG l = x + u, t = y + v, r = x + end, b = y + v + 1;
                    ok = open32MapPoint(context, &l, &t, TRUE) &&
                         open32MapPoint(context, &r, &b, TRUE) && SetRectRgn(run, l, t, r, b) &&
                         CombineRgn(region, region, run, 2);
                }
                u = end;
            }
        }
        LONG saved = ok ? SaveDC(dc) : 0;
        ok = saved > 0 && ExtSelectClipRgn(dc, region, 1) != 0;
        if (ok && info_size)
        {
            LONG rows = (LONG)lx_guest_load_u32(p + info_at + 8);
            ok = StretchDIBits(dc, x, y, width, height, source_x,
                               rows > 0 ? rows - source_y - height : source_y, width, height,
                               (PVOID)(p + bits_at), (PVOID)(p + info_at), usage, code) != 0;
        }
        else if (ok)
            ok = PatBlt(dc, x, y, width, height, code);
        if (saved)
            RestoreDC(dc, saved);
        if (run)
            DeleteObject(run);
        if (region)
            DeleteObject(region);
    }
    return ok;
}

/* PT_MOVETO, PT_LINETO and PT_BEZIERTO, with PT_CLOSEFIGURE closing back to the figure's start. */
static BOOL emf_poly_draw(ULONG dc, const uint8 *points, const uint8 *types, ULONG n, BOOL wide)
{
    Open32DC *context = open32FindDC(dc);
    LONG start_x = context->position_x, start_y = context->position_y;
    BOOL ok = TRUE;
    for (ULONG i = 0; ok && i < n; i++)
    {
        uint8 wire[24];
        unsigned used = (types[i] & ~1u) == 4 ? 3 : 1;
        if (i + used > n)
        {
            open32SetError(13);
            return FALSE;
        }
        for (unsigned k = 0; k < used; k++)
        {
            const uint8 *q = points + (size_t)(i + k) * (wide ? 4 : 8);
            lx_guest_store_u32(wire + k * 8, wide ? (ULONG)metafile_word(q) : lx_guest_load_u32(q));
            lx_guest_store_u32(wire + k * 8 + 4,
                               wide ? (ULONG)metafile_word(q + 2) : lx_guest_load_u32(q + 4));
        }
        LONG x = (LONG)lx_guest_load_u32(wire + (used - 1) * 8),
             y = (LONG)lx_guest_load_u32(wire + (used - 1) * 8 + 4);
        switch (types[i] & ~1u)
        {
            case 6:
                ok = MoveToEx(dc, x, y, NULL);
                start_x = x;
                start_y = y;
                break;
            case 2:
                ok = LineTo(dc, x, y);
                break;
            case 4:
                ok = emf_bezier(dc, wire, 3, TRUE);
                break;
            default:
                open32SetError(13);
                return FALSE;
        }
        if (ok && (types[i + used - 1] & 1))
            ok = LineTo(dc, start_x, start_y);
        i += used - 1;
    }
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
    if (emf_inert(type))
        return FALSE;
    /* Paths are already flat, and every host face is a scalable face that aspect filtering
       cannot exclude. */
    if (type == 14 || type == 16 || type == 65)
        return TRUE;
    if (type == 52)
        return RealizePalette(dc) != UINT32_MAX;
    if (type == 58 && bytes >= 12)
    {
        float limit;
        memcpy(&limit, p + 8, sizeof limit);
        open32FindDC(dc)->miter_limit = limit;
        return TRUE;
    }
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
    if (type == 70)
    {
        /* Only multiformat comments draw: the embedded enhanced picture plays into rclOutput.
           Comments of other kinds, including wrapped WMF bytes, are annotations. */
        if (bytes >= 40 && lx_guest_load_u32(p + 8) && lx_guest_load_u32(p + 12) == 0x43494447 &&
            lx_guest_load_u32(p + 16) == 0x40000004)
        {
            ULONG formats = lx_guest_load_u32(p + 36);
            for (ULONG i = 0; formats <= (bytes - 40) / 16 && i < formats; i++)
            {
                const uint8 *format = p + 40 + i * 16;
                ULONG size = lx_guest_load_u32(format + 8), at = lx_guest_load_u32(format + 12);
                if (lx_guest_load_u32(format) != 0x464d4520 || at > bytes - 12 ||
                    size > bytes - 12 - at)
                    continue;
                ULONG inner = SetEnhMetaFileBits(size, (PVOID)(p + 12 + at));
                BOOL ok = inner && PlayEnhMetaFile(dc, inner, (PVOID)(p + 20));
                if (inner)
                    DeleteEnhMetaFile(inner);
                return ok;
            }
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
            lx_guest_store_u32(wide + head + i * 8, (ULONG)metafile_word(p + head + i * 4));
            lx_guest_store_u32(wide + head + i * 8 + 4, (ULONG)metafile_word(p + head + i * 4 + 2));
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
    if ((type == 35 && bytes >= 32) || (type == 36 && bytes >= 36))
    {
        float matrix[6];
        memcpy(matrix, p + 8, sizeof matrix);
        double values[6];
        for (unsigned i = 0; i < 6; i++)
            values[i] = matrix[i];
        return open32WorldTransform(dc, values, type == 35 ? 0 : lx_guest_load_u32(p + 32));
    }
    if (type == 41 && bytes >= 28)
    {
        float start, sweep;
        memcpy(&start, p + 20, sizeof start);
        memcpy(&sweep, p + 24, sizeof sweep);
        return open32AngleArc(dc, EMF_LONG(8), EMF_LONG(12), lx_guest_load_u32(p + 16), start,
                              sweep);
    }
    if (type == 53 && bytes >= 24)
        return open32FloodFill(dc, EMF_LONG(8), EMF_LONG(12), lx_guest_load_u32(p + 16),
                               lx_guest_load_u32(p + 20));
    if (type == 55 && bytes >= 40)
        return open32ArcTo(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                           EMF_LONG(28), EMF_LONG(32), EMF_LONG(36));
    if ((type == 56 || type == 92) && bytes >= 28)
    {
        ULONG n = lx_guest_load_u32(p + 24), size = type == 56 ? 8 : 4;
        if (n > 65536 || n > (bytes - 28) / (size + 1))
        {
            open32SetError(13);
            return FALSE;
        }
        return emf_poly_draw(dc, p + 28, p + 28 + (size_t)n * size, n, size == 4);
    }
    if (type == 57 && bytes >= 12)
    {
        open32FindDC(dc)->arc_clockwise = EMF_LONG(8) == 2;
        return TRUE;
    }
    if (type == 66)
        return open32PathWiden(open32FindDC(dc));
    if (type == 67 && bytes >= 12)
        return open32PathClip(dc, EMF_LONG(8));
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
    if (type == 78 && bytes >= 128 && lx_guest_load_u32(p + 116))
        return emf_mask_blt(dc, p, bytes);
    if ((type == 76 && bytes >= 100) || (type == 77 && bytes >= 108) ||
        (type == 78 && bytes >= 128))
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
    if (type == 93 && bytes >= 32)
    {
        /* DIB_PAL_INDICES bits take the DC's text and background colors when drawn. */
        ULONG info_at = lx_guest_load_u32(p + 16), info_size = lx_guest_load_u32(p + 20),
              bits_at = lx_guest_load_u32(p + 24), bits_size = lx_guest_load_u32(p + 28);
        uint8 mono[48] = {0};
        if (info_at > bytes || info_size > bytes - info_at || info_size < 40 || bits_at > bytes ||
            bits_size > bytes - bits_at)
        {
            open32SetError(13);
            return FALSE;
        }
        memcpy(mono, p + info_at, 40);
        lx_guest_store_u32(mono + 32, 2);
        lx_guest_store_u32(mono + 44, 0xFFFFFF);
        size_t ib, pb;
        if (!open32DibBuffers(mono, 0, UINT32_MAX, 0, &ib, &pb) || pb > bits_size)
            return FALSE;
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          open32DibPatternBrush(mono, (PVOID)(p + bits_at), 0, TRUE));
    }
    if (type == 94 && bytes >= 32)
    {
        ULONG usage = lx_guest_load_u32(p + 12), info_at = lx_guest_load_u32(p + 16),
              bits_at = lx_guest_load_u32(p + 24);
        if (!emf_dib(p, bytes, info_at, lx_guest_load_u32(p + 20), bits_at,
                     lx_guest_load_u32(p + 28), usage))
            return FALSE;
        return emf_object(
            table, count, lx_guest_load_u32(p + 8),
            open32DibPatternBrush((PVOID)(p + info_at), (PVOID)(p + bits_at), usage, FALSE));
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
    /* The picture's device space maps onto the target. Its own mapping records then apply
       underneath this fit instead of replacing it. */
    Open32DC *context = open32FindDC(dc);
    LONG target_left = l, target_top = t, target_right = r, target_bottom = b;
    if (!open32MapPoint(context, &target_left, &target_top, TRUE) ||
        !open32MapPoint(context, &target_right, &target_bottom, TRUE))
        return FALSE;
    double sx = ((double)target_right - target_left) / (double)fw,
           sy = ((double)target_bottom - target_top) / (double)fh;
    context->fit[0] = sx;
    context->fit[1] = 0;
    context->fit[2] = 0;
    context->fit[3] = sy;
    context->fit[4] = target_left - fl * sx;
    context->fit[5] = target_top - ft * sy;
    context->fit_active = TRUE;
    context->world_active = FALSE;
    context->map_mode = 1;
    context->window_x = context->window_y = context->viewport_x = context->viewport_y = 0;
    context->window_width = context->window_height = 1;
    context->viewport_width = context->viewport_height = 1;
    return TRUE;
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
    /* PMWINX walks the records after the header, ignores each handler's result and stops at the
       end record, so a record that fails neither ends playback nor fails the call. */
    for (ULONG offset = lx_guest_load_u32(f->data + 4); ok && offset + 8 <= f->size;)
    {
        ULONG bytes = lx_guest_load_u32(f->data + offset + 4);
        ULONG type = lx_guest_load_u32(f->data + offset);
        if (type == 14)
            break;
        PlayEnhMetaFileRecord(dc, table, f->data + offset, count);
        offset += bytes;
    }
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
    LxExecution *execution = GProcessState.execution;
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
        for (ULONG at = 0; ok && at < f->size;)
        {
            ULONG bytes = lx_guest_load_u32(f->data + at + 4);
            memcpy(record, f->data + at, bytes);
            ok = lx_execution_callback32_4(execution, lx_execution_mapping_address(code), dc,
                                           address, address + table_bytes, count) != 0;
            at += bytes;
        }
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
