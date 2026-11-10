/* Public domain. No rights reserved. Wholly AI-authored. */
/* Enhanced metafiles: handles, headers, record playback and enumeration. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/common/pm_text.h"
#include <math.h>
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"

enum
{
    /* EMR_EXTTEXTOUTW text is matched back to the ANSI (Windows Latin 1) byte whose PM
       translation gives the same character. */
    ANSI_CODEPAGE = 1004,
    PC_CODEPAGE = 437,
    /* rclFrame is in 0.01 mm units; szlMillimeters in millimetres. */
    HUNDREDTHS_PER_MILLIMETER = 100,
    /* Cubic segments flatten to sixteen chords each. */
    BEZIER_CHORDS = 16,
    /* At most 4095 segments, so a flattened curve stays within 65536 points. */
    BEZIER_SEGMENTS_MAX = 4095
};

static BOOL valid_emf(const uint8 *p, ULONG size)
{
    if (!p || size < EMF_SIZE_MIN || size > METAFILE_SIZE_MAX || (size & 3) ||
        lx_guest_load_u32(p + WIN32_EMR_TYPE) != WIN32_EMR_HEADER ||
        lx_guest_load_u32(p + WIN32_ENHMETAHEADER_HEADER_BYTES) < WIN32_ENHMETAHEADER_SIZE ||
        lx_guest_load_u32(p + WIN32_ENHMETAHEADER_HEADER_BYTES) > size ||
        lx_guest_load_u32(p + WIN32_ENHMETAHEADER_SIGNATURE) != WIN32_ENHMETA_SIGNATURE ||
        lx_guest_load_u32(p + WIN32_ENHMETAHEADER_FILE_BYTES) != size)
        return FALSE;
    ULONG count = 0;
    for (ULONG offset = 0; offset <= size - WIN32_EMR_SIZE;)
    {
        ULONG type = lx_guest_load_u32(p + offset + WIN32_EMR_TYPE),
              bytes = lx_guest_load_u32(p + offset + WIN32_EMR_BYTES);
        if (bytes < WIN32_EMR_SIZE || (bytes & 3) || bytes > size - offset)
            return FALSE;
        count++;
        if (type == WIN32_EMR_EOF)
            return bytes >= WIN32_EMREOF_SIZE && offset + bytes == size &&
                   count == lx_guest_load_u32(p + WIN32_ENHMETAHEADER_RECORDS);
        offset += bytes;
    }
    return FALSE;
}

ULONG SetEnhMetaFileBits(ULONG size, PVOID data)
{
    if (!data || size < EMF_SIZE_MIN || size > METAFILE_SIZE_MAX)
    {
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    lx_host_once(&metafile_once, metafile_initialize);
    Open32Metafile *f = metafile_monitor ? malloc(sizeof *f + size) : NULL;
    if (!f)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    memcpy(f->data, data, size);
    if (!valid_emf(f->data, size))
    {
        free(f);
        open32SetError(ERROR_INVALID_DATA);
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
    open32SetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

ULONG GetEnhMetaFileBits(ULONG handle, ULONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
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
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    Open32Metafile *f = borrow.resource;
    ULONG size = lx_guest_load_u32(f->data + WIN32_ENHMETAHEADER_HEADER_BYTES),
          copied = !output           ? size
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
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    uint8 *p = (uint8 *)table + index * 4;
    if (lx_guest_load_u32(p))
    {
        DeleteObject(object);
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    lx_guest_store_u32(p, object);
    return TRUE;
}

/* An RGNDATA block becomes a region of the same logical rectangles. */
static ULONG emf_region(const uint8 *data, ULONG size)
{
    ULONG n = size >= WIN32_RGNDATAHEADER_SIZE ? lx_guest_load_u32(data + WIN32_RGNDATAHEADER_COUNT)
                                               : UINT32_MAX;
    if (n > (size - WIN32_RGNDATAHEADER_SIZE) / WIN32_RECT_SIZE || size < WIN32_RGNDATAHEADER_SIZE)
    {
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    ULONG region = CreateRectRgn(0, 0, 0, 0), part = region ? CreateRectRgn(0, 0, 0, 0) : 0;
    BOOL ok = part != 0;
    for (ULONG i = 0; ok && i < n; i++)
    {
        const uint8 *r = data + WIN32_RGNDATAHEADER_SIZE + i * WIN32_RECT_SIZE;
        ok = SetRectRgn(part, (LONG)lx_guest_load_u32(r + WIN32_RECT_LEFT),
                        (LONG)lx_guest_load_u32(r + WIN32_RECT_TOP),
                        (LONG)lx_guest_load_u32(r + WIN32_RECT_RIGHT),
                        (LONG)lx_guest_load_u32(r + WIN32_RECT_BOTTOM)) &&
             CombineRgn(region, region, part, WIN32_RGN_OR);
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
        bits_size > bytes - bits_at || info_size < WIN32_BITMAPINFOHEADER_SIZE ||
        !open32DibBuffers((PVOID)(p + info_at), 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        info_bytes > info_size || pixel_bytes > bits_size)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}

/* Handlers in PMWINX's record table that return 0 and draw nothing: type 0, the header,
   EMR_SETCOLORADJUSTMENT, EMR_SETMETARGN, the reserved type 69, EMR_PLGBLT and
   EMR_POLYTEXTOUTA to EMR_DELETECOLORSPACE. */
static BOOL emf_inert(ULONG type)
{
    return type <= WIN32_EMR_HEADER || type == WIN32_EMR_SETCOLORADJUSTMENT ||
           type == WIN32_EMR_SETMETARGN || type == 69 || type == WIN32_EMR_PLGBLT ||
           (type >= WIN32_EMR_POLYTEXTOUTA && type <= WIN32_EMR_DELETECOLORSPACE);
}

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
    if ((n - first) % 3 || (n - first) / 3 > BEZIER_SEGMENTS_MAX)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    ULONG segments = (n - first) / 3, total = segments * BEZIER_CHORDS + 1;
    if (!segments)
        return TRUE;
    uint8 *line = malloc((size_t)total * 8);
    if (!line)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        for (unsigned i = 1; i <= BEZIER_CHORDS; i++)
        {
            double t = i / (double)BEZIER_CHORDS, u = 1 - t;
            double wa = u * u * u, wb = 3 * u * u * t, wc = 3 * u * t * t, wd = t * t * t;
            uint8 *out = line + ((size_t)s * BEZIER_CHORDS + i) * 8;
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
        lx_guest_load_u16(p + mask_info_at + WIN32_BITMAPINFOHEADER_BIT_COUNT) != 1)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    LONG x = (LONG)lx_guest_load_u32(p + 24), y = (LONG)lx_guest_load_u32(p + 28),
         width = (LONG)lx_guest_load_u32(p + 32), height = (LONG)lx_guest_load_u32(p + 36),
         source_x = (LONG)lx_guest_load_u32(p + 44), source_y = (LONG)lx_guest_load_u32(p + 48),
         mask_x = (LONG)lx_guest_load_u32(p + 100), mask_y = (LONG)lx_guest_load_u32(p + 104);
    LONG mask_width = (LONG)lx_guest_load_u32(p + mask_info_at + WIN32_BITMAPINFOHEADER_WIDTH),
         mask_rows = (LONG)lx_guest_load_u32(p + mask_info_at + WIN32_BITMAPINFOHEADER_HEIGHT);
    /* Monochrome DIB rows are padded to 32 bits. */
    size_t stride = ((size_t)(mask_width > 0 ? mask_width : 0) + 31) / 32 * 4;
    const uint8 *mask = p + mask_bits_at;
    Open32DC *context = open32FindDC(dc);
    BOOL ok = TRUE;
    for (unsigned pass = 0; ok && pass < 2; pass++)
    {
        /* dwRop carries the foreground ROP in its third byte and the background ROP in its
           fourth; each becomes a ternary ROP3 code in bits 16-23. */
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
                         CombineRgn(region, region, run, WIN32_RGN_OR);
                }
                u = end;
            }
        }
        LONG saved = ok ? SaveDC(dc) : 0;
        ok = saved > 0 && ExtSelectClipRgn(dc, region, WIN32_RGN_AND) != 0;
        if (ok && info_size)
        {
            LONG rows = (LONG)lx_guest_load_u32(p + info_at + WIN32_BITMAPINFOHEADER_HEIGHT);
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
        unsigned used = (types[i] & ~(unsigned)WIN32_PT_CLOSEFIGURE) == WIN32_PT_BEZIERTO ? 3 : 1;
        if (i + used > n)
        {
            open32SetError(ERROR_INVALID_DATA);
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
        switch (types[i] & ~(unsigned)WIN32_PT_CLOSEFIGURE)
        {
            case WIN32_PT_MOVETO:
                ok = MoveToEx(dc, x, y, NULL);
                start_x = x;
                start_y = y;
                break;
            case WIN32_PT_LINETO:
                ok = LineTo(dc, x, y);
                break;
            case WIN32_PT_BEZIERTO:
                ok = emf_bezier(dc, wire, 3, TRUE);
                break;
            default:
                open32SetError(ERROR_INVALID_DATA);
                return FALSE;
        }
        if (ok && (types[i + used - 1] & WIN32_PT_CLOSEFIGURE))
            ok = LineTo(dc, start_x, start_y);
        i += used - 1;
    }
    return ok;
}

BOOL PlayEnhMetaFileRecord(ULONG dc, PVOID table, PVOID record, ULONG count)
{
    if (!open32FindDC(dc) || !record || count > UINT16_MAX || (count && !table))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const uint8 *p = record;
    ULONG type = lx_guest_load_u32(p + WIN32_EMR_TYPE),
          bytes = lx_guest_load_u32(p + WIN32_EMR_BYTES);
    if (bytes < WIN32_EMR_SIZE || (bytes & 3) || bytes > METAFILE_SIZE_MAX)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
/* Record fields follow the EMR header at offset 8; bytes >= checks bound each record's
   fixed part before its fields are read. */
#define EMF_LONG(o) ((LONG)lx_guest_load_u32(p + (o)))
    if (emf_inert(type))
        return FALSE;
    /* Paths are already flat, and every host face is a scalable face that aspect filtering
       cannot exclude. */
    if (type == WIN32_EMR_EOF || type == WIN32_EMR_SETMAPPERFLAGS || type == WIN32_EMR_FLATTENPATH)
        return TRUE;
    if (type == WIN32_EMR_REALIZEPALETTE)
        return RealizePalette(dc) != UINT32_MAX;
    if (type == WIN32_EMR_SETMITERLIMIT && bytes >= 12)
    {
        float limit;
        memcpy(&limit, p + 8, sizeof limit);
        open32FindDC(dc)->miter_limit = limit;
        return TRUE;
    }
    if (type == WIN32_EMR_SETWINDOWEXTEX && bytes >= 16)
        return SetWindowExtEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == WIN32_EMR_SETWINDOWORGEX && bytes >= 16)
        return SetWindowOrgEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == WIN32_EMR_SETVIEWPORTEXTEX && bytes >= 16)
        return SetViewportExtEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == WIN32_EMR_SETVIEWPORTORGEX && bytes >= 16)
        return SetViewportOrgEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == WIN32_EMR_SETMAPMODE && bytes >= 12)
        return SetMapMode(dc, EMF_LONG(8)) != 0;
    if (type == WIN32_EMR_SETBKMODE && bytes >= 12)
        return SetBkMode(dc, EMF_LONG(8)) != 0;
    if (type == WIN32_EMR_SETPOLYFILLMODE && bytes >= 12)
        return SetPolyFillMode(dc, EMF_LONG(8)) != 0;
    if (type == WIN32_EMR_SETROP2 && bytes >= 12)
        return SetROP2(dc, EMF_LONG(8)) != 0;
    if (type == WIN32_EMR_SETSTRETCHBLTMODE && bytes >= 12)
        return SetStretchBltMode(dc, EMF_LONG(8)) != 0;
    if (type == WIN32_EMR_SETTEXTALIGN && bytes >= 12)
        return SetTextAlign(dc, lx_guest_load_u32(p + 8)) != UINT32_MAX;
    if (type == WIN32_EMR_SETTEXTCOLOR && bytes >= 12)
        return SetTextColor(dc, lx_guest_load_u32(p + 8)) != UINT32_MAX;
    if (type == WIN32_EMR_SETBKCOLOR && bytes >= 12)
        return SetBkColor(dc, lx_guest_load_u32(p + 8)) != UINT32_MAX;
    if (type == WIN32_EMR_SETPIXELV && bytes >= 20)
        return SetPixel(dc, EMF_LONG(8), EMF_LONG(12), lx_guest_load_u32(p + 16)) != UINT32_MAX;
    if (type == WIN32_EMR_MOVETOEX && bytes >= 16)
        return MoveToEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if (type == WIN32_EMR_LINETO && bytes >= 16)
        return LineTo(dc, EMF_LONG(8), EMF_LONG(12));
    if (type == WIN32_EMR_SAVEDC)
        return SaveDC(dc) != 0;
    if (type == WIN32_EMR_RESTOREDC && bytes >= 12)
        return RestoreDC(dc, EMF_LONG(8));
    if (type == WIN32_EMR_BEGINPATH)
        return BeginPath(dc);
    if (type == WIN32_EMR_ENDPATH)
        return EndPath(dc);
    if (type == WIN32_EMR_STROKEPATH)
        return StrokePath(dc);
    if (type == WIN32_EMR_ELLIPSE && bytes >= 24)
        return Ellipse(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20));
    if (type == WIN32_EMR_RECTANGLE && bytes >= 24)
        return Rectangle(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20));
    if (type == WIN32_EMR_ROUNDRECT && bytes >= 32)
        return RoundRect(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                         EMF_LONG(28));
    if ((type == WIN32_EMR_ARC || type == WIN32_EMR_PIE) && bytes >= 40)
        return type == WIN32_EMR_ARC
                   ? Arc(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                         EMF_LONG(28), EMF_LONG(32), EMF_LONG(36))
                   : Pie(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                         EMF_LONG(28), EMF_LONG(32), EMF_LONG(36));
    if ((type == WIN32_EMR_POLYGON || type == WIN32_EMR_POLYLINE) && bytes >= 28)
    {
        ULONG n = lx_guest_load_u32(p + 24);
        if (n > METAFILE_POINTS_MAX || n > (bytes - 28) / 8)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return type == WIN32_EMR_POLYGON ? Polygon(dc, (PVOID)(p + 28), (LONG)n)
                                         : Polyline(dc, (PVOID)(p + 28), (LONG)n);
    }
    if ((type == WIN32_EMR_POLYPOLYLINE || type == WIN32_EMR_POLYPOLYGON) && bytes >= 32)
    {
        ULONG polygons = lx_guest_load_u32(p + 24), points = lx_guest_load_u32(p + 28);
        if (polygons > METAFILE_POINTS_MAX || points > METAFILE_POINTS_MAX ||
            polygons > (bytes - 32) / 4 || points > (bytes - 32 - polygons * 4) / 8)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        uint64 total = 0;
        for (ULONG i = 0; i < polygons; i++)
            total += lx_guest_load_u32(p + 32 + i * 4);
        if (total != points)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return type == WIN32_EMR_POLYPOLYLINE
                   ? PolyPolyline(dc, (PVOID)(p + 32 + polygons * 4), (PVOID)(p + 32), polygons)
                   : PolyPolygon(dc, (PVOID)(p + 32 + polygons * 4), (PVOID)(p + 32),
                                 (LONG)polygons);
    }
    if (type == WIN32_EMR_SELECTOBJECT && bytes >= 12)
    {
        ULONG index = lx_guest_load_u32(p + 8), object = 0;
        if (index & WIN32_ENHMETA_STOCK_OBJECT)
            object = GetStockObject((LONG)(index & 0x7fffffff));
        else if (index < count)
            object = lx_guest_load_u32((uint8 *)table + index * 4);
        return object && SelectObject(dc, object) != 0;
    }
    if (type == WIN32_EMR_CREATEPEN && bytes >= 28)
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          CreatePen(EMF_LONG(12), EMF_LONG(16), lx_guest_load_u32(p + 24)));
    if (type == WIN32_EMR_CREATEBRUSHINDIRECT && bytes >= 24)
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          CreateBrushIndirect((PVOID)(p + 12)));
    if (type == WIN32_EMR_DELETEOBJECT && bytes >= 12)
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
    if (type == WIN32_EMR_EXTCREATEFONTINDIRECTW && bytes >= 104)
    {
        /* LOGFONTW to LOGFONTA: face characters outside ASCII become '?'. */
        uint8 font[WIN32_LOGFONT_SIZE];
        memcpy(font, p + WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT, WIN32_LOGFONT_FACE_NAME);
        memset(font + WIN32_LOGFONT_FACE_NAME, 0, WIN32_LF_FACESIZE);
        for (unsigned i = 0; i < WIN32_LF_FACESIZE - 1; i++)
        {
            USHORT c = lx_guest_load_u16(p + WIN32_EMREXTCREATEFONTINDIRECTW_LOGFONT +
                                         WIN32_LOGFONT_FACE_NAME + i * 2);
            if (!c)
                break;
            font[WIN32_LOGFONT_FACE_NAME + i] = c < 128 ? (uint8)c : '?';
        }
        return emf_object(table, count, lx_guest_load_u32(p + 8), CreateFontIndirect(font));
    }
    if (type == WIN32_EMR_STRETCHDIBITS && bytes >= 80)
    {
        ULONG info_at = lx_guest_load_u32(p + 48), info_size = lx_guest_load_u32(p + 52),
              bits_at = lx_guest_load_u32(p + 56), bits_size = lx_guest_load_u32(p + 60),
              usage = lx_guest_load_u32(p + 64);
        if (info_at > bytes || info_size > bytes - info_at || bits_at > bytes ||
            bits_size > bytes - bits_at || info_size < WIN32_BITMAPINFOHEADER_SIZE)
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
    if (type == WIN32_EMR_EXTTEXTOUTW && bytes >= 76)
    {
        ULONG n = lx_guest_load_u32(p + 44), at = lx_guest_load_u32(p + 48),
              dx_at = lx_guest_load_u32(p + 72);
        if (n > METAFILE_POINTS_MAX || at > bytes || n > (bytes - at) / 2 ||
            (dx_at && (dx_at > bytes || n > (bytes - dx_at) / 4)))
            return FALSE;
        char *ansi = malloc(n ? n : 1);
        if (!ansi)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        BOOL valid = TRUE;
        for (ULONG i = 0; i < n; i++)
        {
            USHORT code = lx_guest_load_u16(p + at + i * 2);
            BOOL found = FALSE;
            for (unsigned c = 0; c < 256; c++)
                if (lx_pm_codepoint(WinCpTranslateChar(0, ANSI_CODEPAGE, (uint8)c, PC_CODEPAGE)) ==
                    code)
                {
                    ansi[i] = (char)c;
                    found = TRUE;
                    break;
                }
            if (!found)
            {
                valid = FALSE;
                open32SetError(ERROR_NOT_SUPPORTED);
                break;
            }
        }
        BOOL result =
            valid && ExtTextOut(dc, EMF_LONG(36), EMF_LONG(40), lx_guest_load_u32(p + 52),
                                (PVOID)(p + 56), ansi, n, dx_at ? (PLONG)(p + dx_at) : NULL);
        free(ansi);
        return result;
    }
    if (type == WIN32_EMR_EXTTEXTOUTA && bytes >= 76)
    {
        ULONG n = lx_guest_load_u32(p + 44), text_at = lx_guest_load_u32(p + 48),
              dx_at = lx_guest_load_u32(p + 72);
        if (n > METAFILE_POINTS_MAX || text_at > bytes || n > bytes - text_at ||
            (dx_at && (dx_at > bytes || n > (bytes - dx_at) / 4)))
            return FALSE;
        return ExtTextOut(dc, EMF_LONG(36), EMF_LONG(40), lx_guest_load_u32(p + 52),
                          (PVOID)(p + 56), (PCHAR)(p + text_at), n,
                          dx_at ? (PLONG)(p + dx_at) : NULL);
    }
    if (type == WIN32_EMR_GDICOMMENT)
    {
        /* Only multiformat comments draw: the embedded enhanced picture plays into rclOutput.
           Comments of other kinds, including wrapped WMF bytes, are annotations. */
        if (bytes >= WIN32_EMRGDICOMMENT_MULTIFORMATS_SIZE &&
            lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_DATA_BYTES) &&
            lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_DATA) == WIN32_GDICOMMENT_IDENTIFIER &&
            lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_TYPE) == WIN32_GDICOMMENT_MULTIFORMATS)
        {
            ULONG formats = lx_guest_load_u32(p + WIN32_EMRGDICOMMENT_MULTIFORMATS_FORMATS);
            for (ULONG i = 0; formats <= (bytes - WIN32_EMRGDICOMMENT_MULTIFORMATS_SIZE) /
                                             WIN32_EMRFORMAT_SIZE &&
                              i < formats;
                 i++)
            {
                const uint8 *format =
                    p + WIN32_EMRGDICOMMENT_MULTIFORMATS_SIZE + i * WIN32_EMRFORMAT_SIZE;
                ULONG size = lx_guest_load_u32(format + WIN32_EMRFORMAT_DATA_BYTES),
                      at = lx_guest_load_u32(format + WIN32_EMRFORMAT_DATA_OFFSET);
                if (lx_guest_load_u32(format + WIN32_EMRFORMAT_SIGNATURE) !=
                        WIN32_ENHMETA_SIGNATURE ||
                    at > bytes - WIN32_EMRGDICOMMENT_DATA ||
                    size > bytes - WIN32_EMRGDICOMMENT_DATA - at)
                    continue;
                ULONG inner = SetEnhMetaFileBits(size, (PVOID)(p + WIN32_EMRGDICOMMENT_DATA + at));
                BOOL ok =
                    inner && PlayEnhMetaFile(dc, inner,
                                             (PVOID)(p + WIN32_EMRGDICOMMENT_MULTIFORMATS_OUTPUT));
                if (inner)
                    DeleteEnhMetaFile(inner);
                return ok;
            }
        }
        return TRUE;
    }
    if (type >= WIN32_EMR_POLYBEZIER16 && type <= WIN32_EMR_POLYPOLYGON16 &&
        bytes >= WIN32_EMRPOLYLINE_POINTS)
    {
        /* The 16-bit point forms widen into the matching 32-bit record. */
        static const uint8 wide_types[] = {WIN32_EMR_POLYBEZIER, WIN32_EMR_POLYGON,
                                           WIN32_EMR_POLYLINE,   WIN32_EMR_POLYBEZIERTO,
                                           WIN32_EMR_POLYLINETO, WIN32_EMR_POLYPOLYLINE,
                                           WIN32_EMR_POLYPOLYGON};
        BOOL multiple = type >= WIN32_EMR_POLYPOLYLINE16;
        ULONG polygons = multiple && bytes >= WIN32_EMRPOLYPOLYLINE_COUNTS
                             ? lx_guest_load_u32(p + WIN32_EMRPOLYPOLYLINE_POLYGONS)
                             : 0,
              points = lx_guest_load_u32(
                  p + (multiple ? WIN32_EMRPOLYPOLYLINE_COUNT : WIN32_EMRPOLYLINE_COUNT));
        if ((multiple && bytes < WIN32_EMRPOLYPOLYLINE_COUNTS) || polygons > METAFILE_POINTS_MAX ||
            points > METAFILE_POINTS_MAX)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        ULONG head =
            multiple ? WIN32_EMRPOLYPOLYLINE_COUNTS + polygons * 4 : WIN32_EMRPOLYLINE_POINTS;
        if (head > bytes || points > (bytes - head) / 4)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        uint8 *wide = malloc(head + (size_t)points * 8);
        if (!wide)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        memcpy(wide, p, head);
        lx_guest_store_u32(wide + WIN32_EMR_TYPE, wide_types[type - WIN32_EMR_POLYBEZIER16]);
        lx_guest_store_u32(wide + WIN32_EMR_BYTES, head + points * 8);
        for (ULONG i = 0; i < points; i++)
        {
            lx_guest_store_u32(wide + head + i * 8, (ULONG)metafile_word(p + head + i * 4));
            lx_guest_store_u32(wide + head + i * 8 + 4, (ULONG)metafile_word(p + head + i * 4 + 2));
        }
        BOOL result = PlayEnhMetaFileRecord(dc, table, wide, count);
        free(wide);
        return result;
    }
    if ((type == WIN32_EMR_POLYBEZIER || type == WIN32_EMR_POLYBEZIERTO ||
         type == WIN32_EMR_POLYLINETO) &&
        bytes >= 28)
    {
        ULONG n = lx_guest_load_u32(p + 24);
        if (n > METAFILE_POINTS_MAX || n > (bytes - 28) / 8)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (type != WIN32_EMR_POLYLINETO)
            return emf_bezier(dc, p + 28, n, type == WIN32_EMR_POLYBEZIERTO);
        BOOL result = TRUE;
        for (ULONG i = 0; result && i < n; i++)
            result = LineTo(dc, (LONG)lx_guest_load_u32(p + 28 + i * 8),
                            (LONG)lx_guest_load_u32(p + 32 + i * 8));
        return result;
    }
    if (type == WIN32_EMR_SETBRUSHORGEX && bytes >= 16)
        return SetBrushOrgEx(dc, EMF_LONG(8), EMF_LONG(12), NULL);
    if ((type == WIN32_EMR_EXCLUDECLIPRECT || type == WIN32_EMR_INTERSECTCLIPRECT) && bytes >= 24)
        return (type == WIN32_EMR_EXCLUDECLIPRECT
                    ? ExcludeClipRect(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20))
                    : IntersectClipRect(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16),
                                        EMF_LONG(20))) != 0;
    if (type == WIN32_EMR_OFFSETCLIPRGN && bytes >= 16)
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
    if ((type == WIN32_EMR_SCALEVIEWPORTEXTEX || type == WIN32_EMR_SCALEWINDOWEXTEX) && bytes >= 24)
        return type == WIN32_EMR_SCALEVIEWPORTEXTEX
                   ? ScaleViewportExtEx(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20),
                                        NULL)
                   : ScaleWindowExtEx(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20),
                                      NULL);
    if ((type == WIN32_EMR_SETWORLDTRANSFORM && bytes >= 32) ||
        (type == WIN32_EMR_MODIFYWORLDTRANSFORM && bytes >= 36))
    {
        float matrix[6];
        memcpy(matrix, p + 8, sizeof matrix);
        double values[6];
        for (unsigned i = 0; i < 6; i++)
            values[i] = matrix[i];
        return open32WorldTransform(
            dc, values, type == WIN32_EMR_SETWORLDTRANSFORM ? 0 : lx_guest_load_u32(p + 32));
    }
    if (type == WIN32_EMR_ANGLEARC && bytes >= 28)
    {
        float start, sweep;
        memcpy(&start, p + 20, sizeof start);
        memcpy(&sweep, p + 24, sizeof sweep);
        return open32AngleArc(dc, EMF_LONG(8), EMF_LONG(12), lx_guest_load_u32(p + 16), start,
                              sweep);
    }
    if (type == WIN32_EMR_EXTFLOODFILL && bytes >= 24)
        return open32FloodFill(dc, EMF_LONG(8), EMF_LONG(12), lx_guest_load_u32(p + 16),
                               lx_guest_load_u32(p + 20));
    if (type == WIN32_EMR_ARCTO && bytes >= 40)
        return open32ArcTo(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                           EMF_LONG(28), EMF_LONG(32), EMF_LONG(36));
    if ((type == WIN32_EMR_POLYDRAW || type == WIN32_EMR_POLYDRAW16) && bytes >= 28)
    {
        ULONG n = lx_guest_load_u32(p + 24), size = type == WIN32_EMR_POLYDRAW ? 8 : 4;
        if (n > METAFILE_POINTS_MAX || n > (bytes - 28) / (size + 1))
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return emf_poly_draw(dc, p + 28, p + 28 + (size_t)n * size, n, size == 4);
    }
    if (type == WIN32_EMR_SETARCDIRECTION && bytes >= 12)
    {
        open32FindDC(dc)->arc_clockwise = EMF_LONG(8) == WIN32_AD_CLOCKWISE;
        return TRUE;
    }
    if (type == WIN32_EMR_WIDENPATH)
        return open32PathWiden(open32FindDC(dc));
    if (type == WIN32_EMR_SELECTCLIPPATH && bytes >= 12)
        return open32PathClip(dc, EMF_LONG(8));
    if (type == WIN32_EMR_CHORD && bytes >= 40)
        return open32Chord(dc, EMF_LONG(8), EMF_LONG(12), EMF_LONG(16), EMF_LONG(20), EMF_LONG(24),
                           EMF_LONG(28), EMF_LONG(32), EMF_LONG(36));
    if (type == WIN32_EMR_CLOSEFIGURE)
        return open32PathClose(open32FindDC(dc));
    if (type == WIN32_EMR_FILLPATH || type == WIN32_EMR_STROKEANDFILLPATH)
        return open32PathFill(dc, type == WIN32_EMR_STROKEANDFILLPATH);
    if (type == WIN32_EMR_ABORTPATH)
    {
        open32PathCleanup(open32FindDC(dc));
        return TRUE;
    }
    if (type == WIN32_EMR_SELECTPALETTE && bytes >= 12)
    {
        ULONG index = lx_guest_load_u32(p + 8), object = 0;
        if (index & WIN32_ENHMETA_STOCK_OBJECT)
            object = GetStockObject((LONG)(index & 0x7fffffff));
        else if (index < count)
            object = lx_guest_load_u32((uint8 *)table + index * 4);
        return object && SelectPalette(dc, object, FALSE) != 0;
    }
    if (type == WIN32_EMR_CREATEPALETTE && bytes >= 16)
    {
        if (lx_guest_load_u16(p + 14) > (bytes - 16) / 4)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return emf_object(table, count, lx_guest_load_u32(p + 8), CreatePalette((PVOID)(p + 12)));
    }
    if ((type == WIN32_EMR_SETPALETTEENTRIES && bytes >= 20) ||
        (type == WIN32_EMR_RESIZEPALETTE && bytes >= 16))
    {
        ULONG index = lx_guest_load_u32(p + 8);
        ULONG object = index < count ? lx_guest_load_u32((uint8 *)table + index * 4) : 0;
        if (!object)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (type == WIN32_EMR_RESIZEPALETTE)
            return ResizePalette(object, lx_guest_load_u32(p + 12));
        ULONG entries = lx_guest_load_u32(p + 16);
        if (entries > (bytes - 20) / 4)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        SetPaletteEntries(object, lx_guest_load_u32(p + 12), entries, (PVOID)(p + 20));
        return TRUE;
    }
    if ((type == WIN32_EMR_FILLRGN && bytes >= 32) || (type == WIN32_EMR_FRAMERGN && bytes >= 40) ||
        (type == WIN32_EMR_INVERTRGN && bytes >= 28) ||
        (type == WIN32_EMR_PAINTRGN && bytes >= 28) ||
        (type == WIN32_EMR_EXTSELECTCLIPRGN && bytes >= 16))
    {
        ULONG size = lx_guest_load_u32(p + (type == WIN32_EMR_EXTSELECTCLIPRGN ? 8 : 24));
        ULONG at = type == WIN32_EMR_FILLRGN            ? 32
                   : type == WIN32_EMR_FRAMERGN         ? 40
                   : type == WIN32_EMR_EXTSELECTCLIPRGN ? 16
                                                        : 28;
        if (size > bytes - at)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        if (type == WIN32_EMR_EXTSELECTCLIPRGN && !size)
            return ExtSelectClipRgn(dc, 0, EMF_LONG(12)) != 0;
        ULONG region = emf_region(p + at, size), brush = 0;
        if (!region)
            return FALSE;
        if (type == WIN32_EMR_FILLRGN || type == WIN32_EMR_FRAMERGN)
        {
            ULONG index = lx_guest_load_u32(p + 28);
            if (index & WIN32_ENHMETA_STOCK_OBJECT)
                brush = GetStockObject((LONG)(index & 0x7fffffff));
            else if (index < count)
                brush = lx_guest_load_u32((uint8 *)table + index * 4);
        }
        BOOL result = FALSE;
        if (type == WIN32_EMR_FILLRGN)
            result = brush && FillRgn(dc, region, brush);
        else if (type == WIN32_EMR_FRAMERGN)
            result = brush && FrameRgn(dc, region, brush, EMF_LONG(32), EMF_LONG(36));
        else if (type == WIN32_EMR_PAINTRGN)
            result = PaintRgn(dc, region);
        else if (type == WIN32_EMR_EXTSELECTCLIPRGN)
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
    if (type == WIN32_EMR_MASKBLT && bytes >= 128 && lx_guest_load_u32(p + 116))
        return emf_mask_blt(dc, p, bytes);
    if ((type == WIN32_EMR_BITBLT && bytes >= 100) ||
        (type == WIN32_EMR_STRETCHBLT && bytes >= 108) ||
        (type == WIN32_EMR_MASKBLT && bytes >= 128))
    {
        ULONG operation = lx_guest_load_u32(p + 40), usage = lx_guest_load_u32(p + 80),
              info_at = lx_guest_load_u32(p + 84), info_size = lx_guest_load_u32(p + 88),
              bits_at = lx_guest_load_u32(p + 92), bits_size = lx_guest_load_u32(p + 96);
        if (!info_size)
            return PatBlt(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(32), EMF_LONG(36), operation);
        if (!emf_dib(p, bytes, info_at, info_size, bits_at, bits_size, usage))
            return FALSE;
        LONG width = type == WIN32_EMR_STRETCHBLT ? EMF_LONG(100) : EMF_LONG(32),
             height = type == WIN32_EMR_STRETCHBLT ? EMF_LONG(104) : EMF_LONG(36);
        LONG rows = (LONG)lx_guest_load_u32(p + info_at + 8);
        /* Device source coordinates run from the top; bottom-up DIB rows run from the bottom. */
        LONG source_y = rows > 0 ? rows - EMF_LONG(48) - height : EMF_LONG(48);
        return StretchDIBits(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(32), EMF_LONG(36),
                             EMF_LONG(44), source_y, width, height, (PVOID)(p + bits_at),
                             (PVOID)(p + info_at), usage, operation) != 0;
    }
    if (type == WIN32_EMR_SETDIBITSTODEVICE && bytes >= 76)
    {
        ULONG info_at = lx_guest_load_u32(p + 48), info_size = lx_guest_load_u32(p + 52),
              bits_at = lx_guest_load_u32(p + 56), bits_size = lx_guest_load_u32(p + 60),
              usage = lx_guest_load_u32(p + 64);
        size_t ib, pb;
        if (info_at > bytes || info_size > bytes - info_at ||
            info_size < WIN32_BITMAPINFOHEADER_SIZE || bits_at > bytes ||
            bits_size > bytes - bits_at ||
            !open32DibBuffers((PVOID)(p + info_at), lx_guest_load_u32(p + 68),
                              lx_guest_load_u32(p + 72), usage, &ib, &pb) ||
            ib > info_size || pb > bits_size)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return SetDIBitsToDevice(dc, EMF_LONG(24), EMF_LONG(28), EMF_LONG(40), EMF_LONG(44),
                                 EMF_LONG(32), EMF_LONG(36), lx_guest_load_u32(p + 68),
                                 lx_guest_load_u32(p + 72), (PVOID)(p + bits_at),
                                 (PVOID)(p + info_at), usage) != 0;
    }
    if (type == WIN32_EMR_CREATEMONOBRUSH && bytes >= 32)
    {
        /* DIB_PAL_INDICES bits take the DC's text and background colors when drawn. */
        ULONG info_at = lx_guest_load_u32(p + 16), info_size = lx_guest_load_u32(p + 20),
              bits_at = lx_guest_load_u32(p + 24), bits_size = lx_guest_load_u32(p + 28);
        uint8 mono[WIN32_BITMAPINFOHEADER_SIZE + 2 * WIN32_RGBQUAD_SIZE] = {0};
        if (info_at > bytes || info_size > bytes - info_at ||
            info_size < WIN32_BITMAPINFOHEADER_SIZE || bits_at > bytes ||
            bits_size > bytes - bits_at)
        {
            open32SetError(ERROR_INVALID_DATA);
            return FALSE;
        }
        memcpy(mono, p + info_at, WIN32_BITMAPINFOHEADER_SIZE);
        lx_guest_store_u32(mono + WIN32_BITMAPINFOHEADER_CLR_USED, 2);
        lx_guest_store_u32(mono + WIN32_BITMAPINFOHEADER_SIZE + WIN32_RGBQUAD_SIZE, 0xFFFFFF);
        size_t ib, pb;
        if (!open32DibBuffers(mono, 0, UINT32_MAX, 0, &ib, &pb) || pb > bits_size)
            return FALSE;
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          open32DibPatternBrush(mono, (PVOID)(p + bits_at), 0, TRUE));
    }
    if (type == WIN32_EMR_CREATEDIBPATTERNBRUSHPT && bytes >= 32)
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
    if (type == WIN32_EMR_EXTCREATEPEN && bytes >= 52)
    {
        uint8 brush[WIN32_LOGBRUSH_SIZE];
        ULONG style = lx_guest_load_u32(p + 28);
        lx_guest_store_u32(brush, lx_guest_load_u32(p + 36));
        lx_guest_store_u32(brush + 4, lx_guest_load_u32(p + 40));
        lx_guest_store_u32(brush + 8, lx_guest_load_u32(p + 44));
        /* User style arrays cannot be selected through PMWINX; they replay as solid pens. */
        if ((style & PS_STYLE_MASK) == PS_USERSTYLE)
            style &= ~(unsigned)PS_STYLE_MASK;
        return emf_object(table, count, lx_guest_load_u32(p + 8),
                          ExtCreatePen(style, lx_guest_load_u32(p + 32), brush, 0, NULL));
    }
#undef EMF_LONG
    TRACE_NATIVE("Unsupported EMF record %u (%u bytes)", type, bytes);
    open32SetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

static BOOL enhanced_bounds(ULONG dc, const Open32Metafile *file, PVOID bounds)
{
    if (!bounds)
        return TRUE;
    LONG l = (LONG)lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_LEFT),
         t = (LONG)lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_TOP),
         r = (LONG)lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_RIGHT),
         b = (LONG)lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_BOTTOM);
    const uint8 *header_bounds = file->data + WIN32_ENHMETAHEADER_BOUNDS,
                *header_frame = file->data + WIN32_ENHMETAHEADER_FRAME;
    LONG fl = (LONG)lx_guest_load_u32(header_bounds + WIN32_RECT_LEFT),
         ft = (LONG)lx_guest_load_u32(header_bounds + WIN32_RECT_TOP),
         fr = (LONG)lx_guest_load_u32(header_bounds + WIN32_RECT_RIGHT),
         fb = (LONG)lx_guest_load_u32(header_bounds + WIN32_RECT_BOTTOM);
    int64_t w = (int64_t)r - l, h = (int64_t)b - t, fw = (int64_t)fr - fl, fh = (int64_t)fb - ft;
    if (!fw || !fh)
    {
        /* Without device bounds the frame, in 0.01 mm, gives the picture's extent. */
        int64_t device_w = (LONG)lx_guest_load_u32(file->data + WIN32_ENHMETAHEADER_DEVICE),
                device_h = (LONG)lx_guest_load_u32(file->data + WIN32_ENHMETAHEADER_DEVICE + 4),
                metric_w = (LONG)lx_guest_load_u32(file->data + WIN32_ENHMETAHEADER_MILLIMETERS),
                metric_h =
                    (LONG)lx_guest_load_u32(file->data + WIN32_ENHMETAHEADER_MILLIMETERS + 4);
        if (metric_w <= 0 || metric_h <= 0)
            return TRUE;
        fl = 0;
        ft = 0;
        fw = ((int64_t)(LONG)lx_guest_load_u32(header_frame + WIN32_RECT_RIGHT) -
              (LONG)lx_guest_load_u32(header_frame + WIN32_RECT_LEFT)) *
             device_w / (metric_w * HUNDREDTHS_PER_MILLIMETER);
        fh = ((int64_t)(LONG)lx_guest_load_u32(header_frame + WIN32_RECT_BOTTOM) -
              (LONG)lx_guest_load_u32(header_frame + WIN32_RECT_TOP)) *
             device_h / (metric_h * HUNDREDTHS_PER_MILLIMETER);
        if (!fw || !fh)
            return TRUE;
    }
    if (!w || !h || w < INT32_MIN || w > INT32_MAX || h < INT32_MIN || h > INT32_MAX ||
        fw < INT32_MIN || fw > INT32_MAX || fh < INT32_MIN || fh > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
    context->map_mode = WIN32_MM_TEXT;
    context->window_x = context->window_y = context->viewport_x = context->viewport_y = 0;
    context->window_width = context->window_height = 1;
    context->viewport_width = context->viewport_height = 1;
    return TRUE;
}

BOOL PlayEnhMetaFile(ULONG dc, ULONG handle, PVOID bounds)
{
    LxHandleBorrow borrow = {0};
    if (bounds && (lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_LEFT) ==
                       lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_RIGHT) ||
                   lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_TOP) ==
                       lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_BOTTOM)))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!open32FindDC(dc) || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ENHMETAFILE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32Metafile *f = borrow.resource;
    ULONG count = lx_guest_load_u16(f->data + WIN32_ENHMETAHEADER_HANDLES);
    uint8 *table = calloc(count ? count : 1, 4);
    LONG saved = SaveDC(dc);
    BOOL ok = table && saved > 0;
    if (ok)
        ok = enhanced_bounds(dc, f, bounds);
    /* PMWINX walks the records after the header, ignores each handler's result and stops at the
       end record, so a record that fails neither ends playback nor fails the call. */
    for (ULONG offset = lx_guest_load_u32(f->data + WIN32_ENHMETAHEADER_HEADER_BYTES);
         ok && offset + WIN32_EMR_SIZE <= f->size;)
    {
        ULONG bytes = lx_guest_load_u32(f->data + offset + WIN32_EMR_BYTES);
        ULONG type = lx_guest_load_u32(f->data + offset + WIN32_EMR_TYPE);
        if (type == WIN32_EMR_EOF)
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (bounds && (lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_LEFT) ==
                       lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_RIGHT) ||
                   lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_TOP) ==
                       lx_guest_load_u32((uint8 *)bounds + WIN32_RECT_BOTTOM)))
    {
        lx_handle_release(&borrow);
        return FALSE;
    }
    Open32Metafile *f = borrow.resource;
    ULONG count = lx_guest_load_u16(f->data + WIN32_ENHMETAHEADER_HANDLES), maximum = 0;
    for (ULONG at = 0; at < f->size;)
    {
        ULONG bytes = lx_guest_load_u32(f->data + at + WIN32_EMR_BYTES);
        if (bytes > maximum)
            maximum = bytes;
        at += bytes;
    }
    ULONG table_bytes = count ? count * 4 : 4;
    size_t size =
        (table_bytes + (size_t)maximum + OPEN32_PAGE_SIZE - 1) & ~(size_t)(OPEN32_PAGE_SIZE - 1);
    LxGuestMapping *data = lx_execution_map(execution, 0, size, LX_GUEST_READ | LX_GUEST_WRITE, 0),
                   *code = data ? lx_execution_map(execution, 0, OPEN32_PAGE_SIZE,
                                                   LX_GUEST_READ | LX_GUEST_WRITE, 0)
                                : NULL;
    LONG saved = dc && bounds ? SaveDC(dc) : 0;
    BOOL ok = data && code && (!(dc && bounds) || (saved > 0 && enhanced_bounds(dc, f, bounds)));
    if (ok)
    {
        uint8 thunk[] = {0x55, 0x89, 0xe5, 0x68, 0,    0,    0,    0,    0xff, 0x75, 0x14,
                         0xff, 0x75, 0x10, 0xff, 0x75, 0x0c, 0xff, 0x75, 0x08, 0xb8, 0,
                         0,    0,    0,    0xff, 0xd0, 0x89, 0xec, 0x5d, 0xc3};
        lx_guest_store_u32(thunk + ENUM_THUNK_PARAMETER, parameter);
        lx_guest_store_u32(thunk + ENUM_THUNK_PROCEDURE, procedure);
        memcpy(lx_execution_mapping_host(code), thunk, sizeof thunk);
        ok = lx_execution_protect(execution, code, LX_GUEST_READ | LX_GUEST_EXECUTE);
    }
    if (ok)
    {
        ULONG address = lx_execution_mapping_address(data);
        uint8 *objects = lx_execution_mapping_host(data), *record = objects + table_bytes;
        for (ULONG at = 0; ok && at < f->size;)
        {
            ULONG bytes = lx_guest_load_u32(f->data + at + WIN32_EMR_BYTES);
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
