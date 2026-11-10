/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 device-dependent bitmaps share PM's checked bitmap objects. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/awe/gdi/dc.h"

enum
{
    /* The largest bitmap and transfer: 16384 pels a side and 16 Mi pels in all. */
    BITMAP_SIDE_MAX = 16384,
    BITMAP_PIXELS_MAX = 16 * 1024 * 1024,
    /* CreateBitmapIndirect copies at most 64 MiB of caller rows. */
    BITMAP_RECORD_BYTES_MAX = 64 * 1024 * 1024,
    BITMAP_BITS_MAX = 32,
    TRUE_COLOR_BITS = 24,
    INDEXED_DEPTH_MAX = 8,
    INDEXED_COLORS_MAX = 256,
    MONOCHROME_COLORS = 2,
    RGB_WHITE = 0xFFFFFF,
    RGB_MASK = 0xFFFFFF,
    /* DDB rows are padded to whole 16-bit words. */
    BITMAP_ROW_ALIGN_BITS = 16,
    BITMAP_ROW_ALIGN_BYTES = 2,
    /* StretchBlt passes its pels to StretchDIBits as a 32-bit BGRX DIB. */
    STRETCH_DIB_BITS = 32,
    STRETCH_DIB_BYTES_PER_PEL = 4
};

/* Windows mask transfers convert between monochrome bits and DC colors before
   applying the raster operation. PM's RGB raster backing has no implicit GDI
   conversion: copying colored pixels into a one-bit mask retains color noise. */
/* A source rectangle reaching past its bitmap transfers only the part inside it, and the
   destination shrinks in proportion. FALSE means nothing of the source remains. */
static BOOL clip_source(const POINTL *points, const PmBitmap *bitmap, POINTL *clipped)
{
    memcpy(clipped, points, 4 * sizeof *clipped);
    int64_t source_width = points[3].x - points[2].x, source_height = points[3].y - points[2].y;
    int64_t target_width = points[1].x - points[0].x, target_height = points[1].y - points[0].y;
    if (source_width <= 0 || source_height <= 0)
        return TRUE;
    int64_t left = points[2].x < 0 ? -(int64_t)points[2].x : 0;
    int64_t bottom = points[2].y < 0 ? -(int64_t)points[2].y : 0;
    int64_t right = points[3].x > (LONG)bitmap->width ? points[3].x - (int64_t)bitmap->width : 0;
    int64_t top = points[3].y > (LONG)bitmap->height ? points[3].y - (int64_t)bitmap->height : 0;
    if (left + right >= source_width || bottom + top >= source_height)
        return FALSE;
    clipped[0].x += (LONG)(left * target_width / source_width);
    clipped[1].x -= (LONG)(right * target_width / source_width);
    clipped[0].y += (LONG)(bottom * target_height / source_height);
    clipped[1].y -= (LONG)(top * target_height / source_height);
    clipped[2].x += (LONG)left;
    clipped[3].x -= (LONG)right;
    clipped[2].y += (LONG)bottom;
    clipped[3].y -= (LONG)top;
    return TRUE;
}

static BOOL bitmap_blt(ULONG destination, ULONG source, PPOINTL points, LONG rop)
{
    PresentationSpace *source_ps = lx_pm_font_space(source);
    PresentationSpace *destination_ps = lx_pm_font_space(destination);
    Open32DC *source_dc = open32FindDC(source), *destination_dc = open32FindDC(destination);
    if (!source_ps || !destination_ps || !source_dc || !destination_dc)
        return FALSE;
    PmBitmap *source_bitmap = source_ps->bitmap_lease.resource;
    PmBitmap *destination_bitmap = destination_ps->bitmap_lease.resource;
    BOOL source_mono = source_bitmap && source_bitmap->planes * source_bitmap->depth == 1;
    BOOL destination_mono =
        destination_bitmap && destination_bitmap->planes * destination_bitmap->depth == 1;
    POINTL clipped[4];
    if (source_bitmap)
    {
        if (!clip_source(points, source_bitmap, clipped))
            return TRUE;
        points = clipped;
    }
    if (source_mono == destination_mono)
        return GpiBitBlt(destination, source, 4, points, rop, 0) == 1;
    LONG width = points[3].x - points[2].x, height = points[3].y - points[2].y;
    if (width <= 0 || height <= 0 || (uint64)width * height > BITMAP_PIXELS_MAX)
        return FALSE;
    ULONG converted = lx_pm_bitmap_create((unsigned)width, (unsigned)height, 1, TRUE_COLOR_BITS);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = converted ? lx_pm_bitmap(converted, &borrow) : NULL;
    SIZEL size = {0, 0};
    HPS memory = bitmap ? GpiCreatePS(0, 0, &size, 0) : 0;
    BOOL ok = bitmap && memory &&
              lx_pm_raster_read(source, points[2].x, points[2].y, (unsigned)width, (unsigned)height,
                                bitmap->pixels);
    if (ok)
    {
        ULONG background =
            source_mono ? destination_dc->background_color : source_dc->background_color;
        ULONG foreground = destination_dc->text_color;
        background = (background & 255) << 16 | (background & 0xFF00) | (background >> 16 & 255);
        foreground = (foreground & 255) << 16 | (foreground & 0xFF00) | (foreground >> 16 & 255);
        for (size_t i = 0; i < (size_t)width * height; i++)
        {
            uint32 pixel = bitmap->pixels[i];
            BOOL bit = source_mono ? (source_bitmap->palette_count == MONOCHROME_COLORS
                                          ? pixel == source_bitmap->palette[1]
                                          : pixel != 0)
                                   : pixel == background;
            bitmap->pixels[i] =
                destination_mono ? (bit ? RGB_WHITE : 0) : (bit ? background : foreground);
        }
        POINTL copy[4] = {points[0], points[1], {0, 0}, {width, height}};
        ok = GpiSetBitmap(memory, converted) != UINT32_MAX &&
             GpiBitBlt(destination, memory, 4, copy, rop, 0) == 1;
    }
    if (memory)
    {
        GpiSetBitmap(memory, 0);
        GpiDestroyPS(memory);
    }
    lx_handle_release(&borrow);
    if (converted)
        GpiDeleteBitmap(converted);
    if (!ok)
        open32SetError(ERROR_INVALID_PARAMETER);
    return ok;
}

/* A raster operation that reads the pattern takes the selected brush, which PM holds
   as area attributes of the presentation space for the duration of the transfer. */
BOOL open32BitmapBlt(ULONG destination, ULONG source, PPOINTL points, LONG rop)
{
    PresentationSpace *space = lx_pm_font_space(destination);
    Open32DC *dc = open32FindDC(destination);
    uint32 rgb = 0;
    /* A ROP3 index reads the pattern when its high nibble (pattern set) differs from its low
       nibble (pattern clear). */
    if (!space || !dc || !((rop ^ (rop >> 4)) & 15) ||
        !open32BrushPixel(dc, points[0].x, points[0].y, &rgb))
        return bitmap_blt(destination, source, points, rop);
    LONG saved_color = space->primitive_color[PRIM_AREA - 1];
    LONG saved_pattern = space->pattern, saved_set = space->pattern_set;
    BOOL saved_mode = space->rgb_colors;
    space->rgb_colors = TRUE;
    space->primitive_color[PRIM_AREA - 1] = (LONG)(rgb & RGB_MASK);
    space->pattern = PATSYM_SOLID;
    space->pattern_set = 0;
    BOOL result = bitmap_blt(destination, source, points, rop);
    space->rgb_colors = saved_mode;
    space->primitive_color[PRIM_AREA - 1] = saved_color;
    space->pattern = saved_pattern;
    space->pattern_set = saved_set;
    return result;
}

ULONG LoadBitmap(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadBitmap(%u, %p)", module, name);
    ULONG resource = FindResource(module, name, (PSZ)(uintptr_t)WIN32_RT_BITMAP);
    if (!resource)
        return 0;
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    HPS ps = WinGetPS(HWND_DESKTOP);
    ULONG bitmap = ps ? GpiLoadBitmap(ps, module, resource & 0xFFFF, 0, 0) : 0;
    if (ps)
        WinReleasePS(ps);
    if (!bitmap)
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return bitmap;
}

BOOL open32BitmapBytes(LONG width, LONG height, ULONG planes, ULONG depth, size_t *bytes)
{
    if (width < 1 || height < 1 || width > BITMAP_SIDE_MAX || height > BITMAP_SIDE_MAX ||
        (uint64_t)width * height > BITMAP_PIXELS_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Plane count multiplies the depth of the chunky raster that backs every bitmap. */
    ULONG bits = planes * depth;
    if (!planes || planes > BITMAP_BITS_MAX || depth > BITMAP_BITS_MAX ||
        (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16 && bits != 24 &&
         bits != 32))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    *bytes = ((size_t)width * bits + BITMAP_ROW_ALIGN_BITS - 1) / BITMAP_ROW_ALIGN_BITS *
             BITMAP_ROW_ALIGN_BYTES * (size_t)height;
    return TRUE;
}

void open32BitmapPalette(ULONG depth, uint32 colors[256])
{
    /* 1-bit: black and white; 2-bit: a grey ramp; 4-bit: the VGA colours; 8-bit: 3-3-2 RGB. */
    static const uint32 vga[16] = {0,        0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
                                   0x008080, 0xC0C0C0, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
                                   0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    memset(colors, 0, INDEXED_COLORS_MAX * sizeof *colors);
    unsigned count = depth <= INDEXED_DEPTH_MAX ? 1u << depth : 0;
    for (unsigned i = 0; i < count; i++)
        colors[i] = depth == 1   ? (i ? RGB_WHITE : 0)
                    : depth == 2 ? i * 0x555555
                    : depth == 4 ? vga[i]
                                 : ((i >> 5) * 255 / 7) << 16 | (((i >> 2) & 7) * 255 / 7) << 8 |
                                       (i & 3) * 85;
}

ULONG CreateBitmap(LONG width, LONG height, ULONG planes, ULONG depth, PVOID bits)
{
    TRACE_NATIVE("CreateBitmap(%d, %d, %u, %u, %p)", width, height, planes, depth, bits);
    size_t bytes;
    if (!open32BitmapBytes(width, height, planes, depth, &bytes) || !open32Presentation())
        return 0;
    depth *= planes;
    planes = 1;
    ULONG handle = lx_pm_bitmap_create((unsigned)width, (unsigned)height, planes, depth);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = handle ? lx_pm_bitmap(handle, &borrow) : NULL;
    if (!bitmap)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    if (bits)
    {
        size_t stride = bytes / (size_t)height;
        if (depth <= INDEXED_DEPTH_MAX)
        {
            bitmap->palette_count = 1u << depth;
            open32BitmapPalette(depth, bitmap->palette);
        }
        for (unsigned y = 0; y < (unsigned)height; y++)
        {
            const uint8 *row = (const uint8 *)bits + y * stride;
            for (unsigned x = 0; x < (unsigned)width; x++)
            {
                uint32 rgb;
                if (depth <= INDEXED_DEPTH_MAX)
                {
                    unsigned bit = x * depth,
                             index = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                    rgb = bitmap->palette[index];
                }
                else if (depth == 16)
                {
                    /* 16-bit DDB pels are 5-5-5 RGB. */
                    ULONG value = lx_guest_load_u16(row + x * 2);
                    rgb = (value & 0x7c00) << 9 | (value & 0x3e0) << 6 | (value & 0x1f) << 3;
                }
                else
                {
                    const uint8 *pixel = row + x * (depth / 8);
                    rgb = (uint32)pixel[2] << 16 | (uint32)pixel[1] << 8 | pixel[0];
                }
                bitmap->pixels[(size_t)((unsigned)height - 1 - y) * width + x] = rgb;
            }
        }
    }
    lx_handle_release(&borrow);
    return handle;
}

static unsigned bitmap_nearest(const uint32 *palette, unsigned count, uint32 rgb)
{
    unsigned best = 0, distance = UINT32_MAX;
    for (unsigned i = 0; i < count; i++)
    {
        int r = (int)((rgb >> 16) & 255) - (int)((palette[i] >> 16) & 255),
            g = (int)((rgb >> 8) & 255) - (int)((palette[i] >> 8) & 255),
            b = (int)(rgb & 255) - (int)(palette[i] & 255);
        unsigned d = (unsigned)(r * r + g * g + b * b);
        if (d < distance)
        {
            distance = d;
            best = i;
            if (!d)
                break;
        }
    }
    return best;
}

/* DDB scanlines are WORD aligned, unlike DWORD-aligned DIB scanlines, and run from the top row;
   PM bitmap rows run from the bottom. */
static LONG bitmap_bits(ULONG handle, ULONG capacity, PVOID data, BOOL write)
{
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    size_t size = 0;
    if (!open32BitmapBytes((LONG)bitmap->width, (LONG)bitmap->height, bitmap->planes, bitmap->depth,
                           &size))
    {
        lx_handle_release(&borrow);
        return 0;
    }
    if (!data)
    {
        lx_handle_release(&borrow);
        return write ? 0 : (LONG)size;
    }
    size_t count = capacity < size ? capacity : size, stride = size / bitmap->height;
    uint8 *encoded = calloc(size, 1);
    if (!encoded)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    uint32 palette[INDEXED_COLORS_MAX];
    open32BitmapPalette(bitmap->depth, palette);
    if (bitmap->palette_count)
        memcpy(palette, bitmap->palette, bitmap->palette_count * 4);
    for (unsigned y = 0; y < bitmap->height; y++)
        for (unsigned x = 0; x < bitmap->width; x++)
        {
            uint32 rgb = bitmap->pixels[(size_t)(bitmap->height - 1 - y) * bitmap->width + x];
            uint8 *row = encoded + y * stride;
            if (bitmap->depth <= INDEXED_DEPTH_MAX)
            {
                unsigned bit = x * bitmap->depth,
                         index = bitmap_nearest(palette, 1u << bitmap->depth, rgb);
                row[bit / 8] |= (uint8)(index << (8 - bitmap->depth - bit % 8));
            }
            else if (bitmap->depth == 16)
                lx_guest_store_u16(row + x * 2, (uint16)((rgb >> 9 & 0x7c00) | (rgb >> 6 & 0x3e0) |
                                                         (rgb >> 3 & 0x1f)));
            else
            {
                uint8 *p = row + x * (bitmap->depth / 8);
                p[0] = (uint8)rgb;
                p[1] = (uint8)(rgb >> 8);
                p[2] = (uint8)(rgb >> 16);
            }
        }
    if (write)
    {
        memcpy(encoded, data, count);
        for (unsigned y = 0; y < bitmap->height; y++)
            for (unsigned x = 0; x < bitmap->width; x++)
            {
                uint8 *row = encoded + y * stride;
                uint32 rgb;
                if (bitmap->depth <= INDEXED_DEPTH_MAX)
                {
                    unsigned bit = x * bitmap->depth,
                             index = (row[bit / 8] >> (8 - bitmap->depth - bit % 8)) &
                                     ((1u << bitmap->depth) - 1);
                    rgb = palette[index];
                }
                else if (bitmap->depth == 16)
                {
                    ULONG value = lx_guest_load_u16(row + x * 2);
                    rgb = (value & 0x7c00) << 9 | (value & 0x3e0) << 6 | (value & 0x1f) << 3;
                }
                else
                {
                    uint8 *p = row + x * (bitmap->depth / 8);
                    rgb = (uint32)p[2] << 16 | (uint32)p[1] << 8 | p[0];
                }
                bitmap->pixels[(size_t)(bitmap->height - 1 - y) * bitmap->width + x] = rgb;
            }
    }
    else
        memcpy(data, encoded, count);
    free(encoded);
    lx_handle_release(&borrow);
    return (LONG)count;
}

LONG GetBitmapBits(ULONG h, LONG count, PVOID output)
{
    if (count < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* A zero capacity is a size query, as is a NULL buffer. */
    return bitmap_bits(h, (ULONG)count, count ? output : NULL, FALSE);
}

LONG SetBitmapBits(ULONG h, ULONG count, PVOID input)
{
    if (!count || !input)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return bitmap_bits(h, count, input, TRUE);
}

ULONG _CreateBitmapIndirect(PVOID input)
{
    if (!input)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const uint8 *p = input;
    LONG w = (LONG)lx_guest_load_u32(p + WIN32_BITMAP_WIDTH),
         h = (LONG)lx_guest_load_u32(p + WIN32_BITMAP_HEIGHT),
         stride = (LONG)lx_guest_load_u32(p + WIN32_BITMAP_WIDTH_BYTES);
    ULONG planes = lx_guest_load_u16(p + WIN32_BITMAP_PLANES),
          depth = lx_guest_load_u16(p + WIN32_BITMAP_BITS_PIXEL),
          address = lx_guest_load_u32(p + WIN32_BITMAP_BITS);
    /* An odd row pitch fails without an error code. */
    if (stride & 1)
        return 0;
    if (planes > BITMAP_BITS_MAX || depth > BITMAP_BITS_MAX || w < 0 || h < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!w || !h)
    {
        ULONG empty = lx_pm_bitmap_create(1, 1, 1, 1);
        if (!empty)
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return empty;
    }
    size_t bytes;
    if (!open32BitmapBytes(w, h, planes, depth, &bytes))
        return 0;
    if (!address)
        return CreateBitmap(w, h, planes, depth, NULL);
    size_t exact = bytes / (size_t)h, pitch = (size_t)(ULONG)stride;
    LxGuestBorrow bits = {0};
    if (pitch < exact || pitch * (size_t)h > (size_t)BITMAP_RECORD_BYTES_MAX ||
        !lx_execution_borrow(GProcessState.execution, address, pitch * (size_t)h, LX_GUEST_READ,
                             &bits))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Rows are repacked when the record's pitch exceeds the bitmap's WORD-aligned rows. */
    uint8 *packed = malloc(bytes);
    if (!packed)
    {
        lx_execution_release(&bits);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    for (LONG row = 0; row < h; row++)
        memcpy(packed + (size_t)row * exact, (const uint8 *)bits.host + (size_t)row * pitch, exact);
    lx_execution_release(&bits);
    ULONG result = CreateBitmap(w, h, planes, depth, packed);
    free(packed);
    return result;
}

ULONG CreateDIBitmap(ULONG dc, PVOID header, ULONG options, PVOID bits, PVOID info, ULONG usage)
{
    if (!open32FindDC(dc))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!bits)
        options = 0;
    if (!header ||
        lx_guest_load_u32((uint8 *)header + WIN32_BITMAPINFOHEADER_STRUCT_SIZE) !=
            WIN32_BITMAPINFOHEADER_SIZE ||
        ((options & WIN32_CBM_INIT) && !info))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG w = (LONG)lx_guest_load_u32((uint8 *)header + WIN32_BITMAPINFOHEADER_WIDTH),
         h = (LONG)lx_guest_load_u32((uint8 *)header + WIN32_BITMAPINFOHEADER_HEIGHT);
    if (h == INT32_MIN)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG height = h < 0 ? -h : h;
    ULONG result = CreateCompatibleBitmap(dc, w, height);
    if (!result)
        return 0;
    if (options & WIN32_CBM_INIT)
    {
        if (SetDIBits(dc, result, 0, (ULONG)height, bits, info, usage) != height)
        {
            DeleteObject(result);
            return 0;
        }
    }
    return result;
}

BOOL StretchBlt(ULONG destination, LONG x, LONG y, LONG w, LONG h, ULONG source, LONG sx, LONG sy,
                LONG sw, LONG sh, ULONG rop)
{
    /* Raster operations that do not read the source accept a NULL source DC. */
    if (!source && open32FindDC(destination))
        return !w || !h || PatBlt(destination, x, y, w, h, rop);
    Open32DC *dc = open32FindDC(source);
    if (!dc || !open32FindDC(destination))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!w || !h || !sw || !sh)
        return TRUE;
    int64_t ex = (int64_t)sx + sw, ey = (int64_t)sy + sh;
    if (ex < INT32_MIN || ex > INT32_MAX || ey < INT32_MIN || ey > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG xx = (LONG)ex, yy = (LONG)ey;
    if (!open32MapPoint(dc, &sx, &sy, TRUE) || !open32MapPoint(dc, &xx, &yy, TRUE))
        return FALSE;
    int64_t width = llabs((int64_t)xx - sx), height = llabs((int64_t)yy - sy);
    if (!width || !height)
        return TRUE;
    if (width > BITMAP_SIDE_MAX || height > BITMAP_SIDE_MAX || width * height > BITMAP_PIXELS_MAX)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    LONG left = sx < xx ? sx : xx, top = sy < yy ? sy : yy;
    uint32 *pixels = malloc((size_t)width * height * sizeof *pixels);
    uint8 *bits = malloc((size_t)width * height * STRETCH_DIB_BYTES_PER_PEL);
    BOOL ok = pixels && bits &&
              lx_pm_raster_read(source, left, open32DCHeight(source) - top - (LONG)height,
                                (unsigned)width, (unsigned)height, pixels);
    if (ok)
    {
        PresentationSpace *source_ps = lx_pm_font_space(source),
                          *target_ps = lx_pm_font_space(destination);
        PmBitmap *source_bitmap = source_ps ? source_ps->bitmap_lease.resource : NULL,
                 *target_bitmap = target_ps ? target_ps->bitmap_lease.resource : NULL;
        BOOL source_mono = source_bitmap && source_bitmap->depth == 1,
             target_mono = target_bitmap && target_bitmap->depth == 1;
        Open32DC *target_dc = open32FindDC(destination);
        for (size_t i = 0; i < (size_t)(width * height); i++)
        {
            uint32 rgb = pixels[i];
            if (source_mono != target_mono)
            {
                if (source_mono)
                {
                    BOOL bit = source_bitmap->palette_count >= MONOCHROME_COLORS
                                   ? rgb == source_bitmap->palette[1]
                                   : rgb != 0;
                    ULONG color = bit ? target_dc->background_color : target_dc->text_color;
                    rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
                }
                else
                {
                    ULONG color = dc->background_color;
                    uint32 background =
                        (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
                    rgb = rgb == background ? RGB_WHITE : 0;
                }
            }
            uint8 *pel = bits + i * STRETCH_DIB_BYTES_PER_PEL;
            pel[0] = (uint8)rgb;
            pel[1] = (uint8)(rgb >> 8);
            pel[2] = (uint8)(rgb >> 16);
            pel[3] = 0;
        }
        uint8 info[WIN32_BITMAPINFOHEADER_SIZE] = {0};
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_STRUCT_SIZE, WIN32_BITMAPINFOHEADER_SIZE);
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_WIDTH, (ULONG)width);
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_HEIGHT, (ULONG)height);
        lx_guest_store_u16(info + WIN32_BITMAPINFOHEADER_PLANES, 1);
        lx_guest_store_u16(info + WIN32_BITMAPINFOHEADER_BIT_COUNT, STRETCH_DIB_BITS);
        ok = StretchDIBits(destination, x, y, w, h, sx > xx ? (LONG)width : 0,
                           sy < yy ? 0 : (LONG)height, sx > xx ? -(LONG)width : (LONG)width,
                           sy < yy ? (LONG)height : -(LONG)height, bits, info, WIN32_DIB_RGB_COLORS,
                           rop) != 0;
    }
    free(bits);
    free(pixels);
    if (!ok)
        open32SetError(ERROR_INVALID_PARAMETER);
    return ok;
}
