/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared Open32 icons retain validated copies of original OS/2 pointer resources. */
#include "os2/include/private/export.h"
#include "os2/include/bsedos.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmgrep.h"
#include "host/fs.h"

/* OS/2 pointer resource and file layouts: an optional BITMAPARRAYFILEHEADER, then a
   BITMAPFILEHEADER (usType, cbSize, hotspot, offBits) and the BITMAPINFOHEADER's cbFix. */
enum
{
    BITMAPARRAYFILEHEADER_SIZE = 14,
    BITMAPFILEHEADER_OFFBITS = 10,
    BITMAPFILEHEADER_SIZE = 14,
    /* A file header and the smallest (12-byte) bitmap header. */
    POINTER_MIN_SIZE = 26,
    /* cbFix of BITMAPINFOHEADER, of the Windows-sized and of the full BITMAPINFOHEADER2. */
    BITMAPINFOHEADER_SIZE = 12,
    BITMAPINFOHEADER2_WINDOWS_SIZE = 40,
    BITMAPINFOHEADER2_SIZE = 64
};

enum
{
    /* Bytes; refuses implausible pointer resources and files before copying them. */
    ICON_RESOURCE_SIZE_MAX = 1024 * 1024,
    ICON_FILE_SIZE_MAX = 16 * 1024 * 1024,
    /* Colour icons beyond 8 bits per pel are created without the colour bitmap. */
    ICON_COLOR_BITS_LIMIT = 16,
    ICON_MASK_BITS = 0xffffff,
    /* ExtractIcon's result for a caller that gives no file, as Open32 returns it. */
    EXTRACT_ICON_NO_FILE = 1,
    DIB_PIXEL_SIZE = 4
};

typedef struct Open32Icon
{
    ULONG handle, module, identity, size;
    uint8 *bytes;
    PmPointerImage *image;
    BOOL owned;
    struct Open32Icon *next;
} Open32Icon;

static Open32Icon *icons;

static void destroy_icon(void *resource)
{
    Open32Icon *icon = resource;
    pmPointerImageFree(icon->image);
    free(icon->bytes);
    free(icon);
}

ULONG LoadIcon(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadIcon(%u, %p)", module, name);
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG resource = FindResource(module, name, (PSZ)(uintptr_t)WIN32_RT_ICON);
    if (!resource)
        return 0;
    for (Open32Icon *icon = icons; icon; icon = icon->next)
        if (icon->module == module && icon->identity == resource)
            return icon->handle;
    ULONG size = 0;
    void *data = NULL;
    if (!GProcessState.queryResourceSize || !GProcessState.getResource ||
        GProcessState.queryResourceSize(module, RT_POINTER, resource & 0xFFFF, &size) ||
        size < POINTER_MIN_SIZE || size > ICON_RESOURCE_SIZE_MAX ||
        GProcessState.getResource(module, RT_POINTER, resource & 0xFFFF, &data))
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    const uint8 *bytes = data;
    ULONG offset = lx_guest_load_u16(bytes) == BFT_BITMAPARRAY ? BITMAPARRAYFILEHEADER_SIZE : 0;
    Open32Icon *icon = NULL;
    if (size - offset >= POINTER_MIN_SIZE && (lx_guest_load_u16(bytes + offset) == BFT_COLORICON ||
                                              lx_guest_load_u16(bytes + offset) == BFT_ICON))
    {
        ULONG header = lx_guest_load_u32(bytes + offset + BITMAPFILEHEADER_SIZE);
        if ((header == BITMAPINFOHEADER_SIZE || header == BITMAPINFOHEADER2_WINDOWS_SIZE ||
             header == BITMAPINFOHEADER2_SIZE) &&
            header <= size - offset - BITMAPFILEHEADER_SIZE &&
            lx_guest_load_u32(bytes + offset + BITMAPFILEHEADER_OFFBITS) < size)
        {
            icon = calloc(1, sizeof *icon);
            if (icon)
                icon->bytes = malloc(size);
            if (icon && !icon->bytes)
            {
                free(icon);
                icon = NULL;
            }
            if (icon)
                memcpy(icon->bytes, bytes, size);
        }
    }
    if (GProcessState.freeResource)
        GProcessState.freeResource(data);
    if (!icon)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    icon->size = size;
    icon->module = module;
    icon->identity = resource;
    icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
    if (!icon->handle)
    {
        destroy_icon(icon);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    icon->next = icons;
    icons = icon;
    return icon->handle;
}

BOOL DestroyIcon(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_ICON, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32Icon *icon = borrow.resource;
    BOOL ok = icon->owned;
    if (ok)
    {
        Open32Icon **link = &icons;
        while (*link && *link != icon)
            link = &(*link)->next;
        if (*link)
            *link = icon->next;
        ok = lx_handle_close(handle, LX_HANDLE_OPEN32_ICON);
    }
    lx_handle_release(&borrow);
    if (!ok)
        open32SetError(ERROR_INVALID_HANDLE);
    return ok;
}

/* PMWINX hands both masks to GpiCreateBitmap unchanged, so rows are DWORD aligned and the first
   row is the bottom of the image, in PM order. Colors above 8 bits per pel make no color bitmap. */
ULONG CreateIcon(ULONG module, LONG width, LONG height, ULONG planes, ULONG depth, PVOID and_bits,
                 PVOID xor_bits)
{
    (void)module;
    size_t mask_bytes;
    if (!xor_bits || planes * depth >= ICON_COLOR_BITS_LIMIT)
    {
        planes = depth = 1;
        xor_bits = NULL;
    }
    if (!and_bits || !open32BitmapBytes(width, height, 1, 1, &mask_bytes))
        return 0;
    ULONG bits = planes * depth;
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    size_t mask_stride = ((size_t)width + 31) / 32 * 4,
           color_stride = ((size_t)width * bits + 31) / 32 * 4;
    uint32 colors[256];
    open32BitmapPalette(bits, colors);
    Open32Icon *icon = calloc(1, sizeof *icon);
    PmPointerImage *image = icon ? calloc(1, sizeof *image) : NULL;
    if (image)
    {
        image->pixels = calloc((size_t)width * height, sizeof *image->pixels);
        image->mask = malloc((size_t)width * height);
    }
    BOOL ok = image && image->pixels && image->mask;
    if (ok)
    {
        image->width = (unsigned)width;
        image->height = (unsigned)height;
        for (LONG y = 0; y < height; y++)
            for (LONG x = 0; x < width; x++)
            {
                size_t at = (size_t)y * width + x;
                image->mask[at] = (((uint8 *)and_bits)[y * mask_stride + x / 8] >> (7 - x % 8)) & 1;
                if (xor_bits)
                {
                    const uint8 *row = (uint8 *)xor_bits + y * color_stride;
                    unsigned bit = (unsigned)x * bits;
                    image->pixels[at] =
                        colors[(row[bit / 8] >> (8 - bits - bit % 8)) & ((1u << bits) - 1)];
                }
            }
        icon->image = image;
        icon->owned = TRUE;
        icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
        ok = icon->handle != 0;
    }
    if (!ok)
    {
        if (icon && icon->image)
            destroy_icon(icon);
        else
        {
            pmPointerImageFree(image);
            free(icon);
        }
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    icon->next = icons;
    icons = icon;
    return icon->handle;
}

BOOL DrawIcon(ULONG dc, LONG x, LONG y, ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!open32FindDC(dc) || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ICON, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32Icon *icon = borrow.resource;
    if (!icon->image && icon->bytes)
        icon->image = pmPointerImage(icon->bytes, icon->size);
    PmPointerImage *image = icon->image;
    BOOL ok = image != NULL;
    LONG height = open32DCHeight(dc), device_x = x, device_y = y;
    ok = ok && open32MapPoint(open32FindDC(dc), &device_x, &device_y, TRUE);
    if (ok && open32IsMetafile(dc))
    {
        /* The metafile receives the mask (SRCAND) and then the image (SRCINVERT) as 32-bit
           DIBs. */
        uint8 info[WIN32_BITMAPINFOHEADER_SIZE] = {0};
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_STRUCT_SIZE, WIN32_BITMAPINFOHEADER_SIZE);
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_WIDTH, image->width);
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_HEIGHT, image->height);
        lx_guest_store_u16(info + WIN32_BITMAPINFOHEADER_PLANES, 1);
        lx_guest_store_u16(info + WIN32_BITMAPINFOHEADER_BIT_COUNT, 32);
        size_t bytes = (size_t)image->width * image->height * DIB_PIXEL_SIZE;
        uint8 *bits = malloc(bytes);
        if (!bits)
            ok = FALSE;
        if (bits)
        {
            for (unsigned pass = 0; pass < 2; pass++)
            {
                for (size_t i = 0; i < bytes / DIB_PIXEL_SIZE; i++)
                {
                    uint32 rgb = pass ? image->pixels[i] : (image->mask[i] ? ICON_MASK_BITS : 0);
                    /* 32-bit DIB pixels are blue, green, red and an unused byte. */
                    bits[i * DIB_PIXEL_SIZE] = (uint8)rgb;
                    bits[i * DIB_PIXEL_SIZE + 1] = (uint8)(rgb >> 8);
                    bits[i * DIB_PIXEL_SIZE + 2] = (uint8)(rgb >> 16);
                    bits[i * DIB_PIXEL_SIZE + 3] = 0;
                }
                open32MetafileDib(dc, x, y, (LONG)image->width, (LONG)image->height, 0, 0,
                                  (LONG)image->width, (LONG)image->height, bits, info, 0,
                                  pass ? SRCINVERT : SRCAND);
            }
            free(bits);
        }
    }

    for (unsigned row = 0; ok && row < image->height; row++)
        for (unsigned col = 0; ok && col < image->width; col++)
        {
            int64_t px = (int64_t)device_x + col,
                    py = (int64_t)height - device_y - image->height + row;
            if (px < 0 || py < 0 || px >= open32DCWidth(dc) || py >= height)
                continue;
            uint32 destination;
            ok = lx_pm_raster_read(dc, (LONG)px, (LONG)py, 1, 1, &destination);
            if (ok)
            {
                size_t i = (size_t)row * image->width + col;
                uint32 pixel =
                    (destination & (image->mask[i] ? ICON_MASK_BITS : 0)) ^ image->pixels[i];
                ok = lx_pm_raster_write(dc, (LONG)px, (LONG)py, 1, 1, &pixel);
            }
        }
    lx_handle_release(&borrow);
    if (!ok)
        open32SetError(ERROR_INVALID_DATA);
    return ok;
}

void open32IconsCleanup(void)
{
    while (icons)
    {
        Open32Icon *icon = icons;
        icons = icon->next;
        lx_handle_close(icon->handle, LX_HANDLE_OPEN32_ICON);
    }
}

ULONG ExtractIcon(ULONG instance, PSZ path, ULONG index)
{
    (void)instance;
    if (!path || !*path)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return EXTRACT_ICON_NO_FILE;
    }
    uint32 error = 0;
    char *host = GProcessState.makeUnixPath ? GProcessState.makeUnixPath(path, &error) : NULL;
    size_t size = 0;
    LxHostFsError status;
    uint8 *bytes = host ? lx_host_file_snapshot(host, ICON_FILE_SIZE_MAX, &size, &status) : NULL;
    free(host);
    if (!bytes)
    {
        open32SetError(error ? error : ERROR_FILE_NOT_FOUND);
        return 0;
    }
    /* An index of -1 asks for the number of icons in the file. */
    PmPointerImage *image = pmPointerImage(bytes, size);
    if (image)
    {
        if (index == UINT32_MAX)
        {
            pmPointerImageFree(image);
            free(bytes);
            return 1;
        }
        if (index)
        {
            pmPointerImageFree(image);
            free(bytes);
            return 0;
        }
        Open32Icon *icon = calloc(1, sizeof *icon);
        if (!icon)
        {
            pmPointerImageFree(image);
            free(bytes);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        icon->image = image;
        icon->bytes = bytes;
        icon->size = size;
        icon->owned = TRUE;
        icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
        if (!icon->handle)
        {
            destroy_icon(icon);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        icon->next = icons;
        icons = icon;
        return icon->handle;
    }
    free(bytes);
    ULONG module = LoadLibrary(path);
    if (!module)
        return 0;
    ULONG found = 0, resource = 0;
    for (ULONG id = 1; id <= UINT16_MAX; id++)
    {
        ULONG resource_size;
        if (GProcessState.queryResourceSize &&
            !GProcessState.queryResourceSize(module, RT_POINTER, id, &resource_size) &&
            resource_size)
        {
            if (found == index)
                resource = id;
            found++;
        }
    }
    ULONG result = 0;
    if (index == UINT32_MAX)
        result = found;
    else if (resource)
    {
        void *data = NULL;
        ULONG length;
        if (!GProcessState.queryResourceSize(module, RT_POINTER, resource, &length) &&
            !GProcessState.getResource(module, RT_POINTER, resource, &data))
        {
            Open32Icon *icon = calloc(1, sizeof *icon);
            if (icon)
            {
                icon->bytes = malloc(length);
                if (icon->bytes)
                {
                    memcpy(icon->bytes, data, length);
                    icon->size = length;
                    icon->owned = TRUE;
                    icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
                }
            }
            if (GProcessState.freeResource)
                GProcessState.freeResource(data);
            if (icon && icon->handle)
            {
                icon->next = icons;
                icons = icon;
                result = icon->handle;
            }
            else if (icon)
                destroy_icon(icon);
        }
    }
    FreeLibrary(module);
    return result;
}
