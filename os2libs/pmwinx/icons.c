/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared Open32 icons retain validated copies of original OS/2 pointer resources. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwin/pointer_image.h"
#include "hal/host_fs.h"
#include "os2libs/pmwinx/batch_services.h"
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
    if ((!module || module == UINT32_MAX) && GLoaderState.main_module)
        module = GLoaderState.main_module->handle;
    ULONG resource = FindResource(module, name, (PSZ)(uintptr_t)3);
    if (!resource)
        return 0;
    for (Open32Icon *icon = icons; icon; icon = icon->next)
        if (icon->module == module && icon->identity == resource)
            return icon->handle;
    ULONG size = 0;
    void *data = NULL;
    if (!GLoaderState.queryResourceSize || !GLoaderState.getResource ||
        GLoaderState.queryResourceSize(module, 1, resource & 0xFFFF, &size) || size < 26 ||
        size > 1048576 || GLoaderState.getResource(module, 1, resource & 0xFFFF, &data))
    {
        open32SetError(1814);
        return 0;
    }
    const uint8 *bytes = data;
    ULONG offset = lx_guest_load_u16(bytes) == 0x4142 ? 14 : 0;
    Open32Icon *icon = NULL;
    if (size - offset >= 26 && (lx_guest_load_u16(bytes + offset) == 0x4943 ||
                                lx_guest_load_u16(bytes + offset) == 0x4349))
    {
        ULONG header = lx_guest_load_u32(bytes + offset + 14);
        if ((header == 12 || header == 40 || header == 64) && header <= size - offset - 14 &&
            lx_guest_load_u32(bytes + offset + 10) < size)
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
    if (GLoaderState.freeResource)
        GLoaderState.freeResource(data);
    if (!icon)
    {
        open32SetError(1814);
        return 0;
    }
    icon->size = size;
    icon->module = module;
    icon->identity = resource;
    icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
    if (!icon->handle)
    {
        destroy_icon(icon);
        open32SetError(8);
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
        open32SetError(6);
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
        open32SetError(6);
    return ok;
}
ULONG CreateIcon(ULONG module, LONG width, LONG height, ULONG planes, ULONG depth, PVOID and_bits,
                 PVOID xor_bits)
{
    (void)module;
    size_t mask_bytes, pixel_bytes;
    if (!xor_bits)
        planes = depth = 1;
    if (!and_bits || !open32BitmapBytes(width, height, 1, 1, &mask_bytes) ||
        !open32BitmapBytes(width, height, planes, depth, &pixel_bytes))
        return 0;
    ULONG bitmap = CreateBitmap(width, height, planes, depth, xor_bits);
    LxHandleBorrow borrow = {0};
    PmBitmap *source = bitmap ? lx_pm_bitmap(bitmap, &borrow) : NULL;
    Open32Icon *icon = source ? calloc(1, sizeof *icon) : NULL;
    PmPointerImage *image = icon ? calloc(1, sizeof *image) : NULL;
    if (image)
    {
        image->pixels = malloc((size_t)width * height * 4);
        image->mask = malloc((size_t)width * height);
    }
    BOOL ok = image && image->pixels && image->mask;
    if (ok)
    {
        image->width = (unsigned)width;
        image->height = (unsigned)height;
        memcpy(image->pixels, source->pixels, (size_t)width * height * 4);
        size_t stride = mask_bytes / (size_t)height;
        for (LONG y = 0; y < height; y++)
            for (LONG x = 0; x < width; x++)
                image->mask[(size_t)y * width + x] =
                    (((uint8 *)and_bits)[y * stride + x / 8] >> (7 - x % 8)) & 1;
        icon->image = image;
        icon->owned = TRUE;
        icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
        ok = icon->handle != 0;
    }
    lx_handle_release(&borrow);
    if (bitmap)
        DeleteObject(bitmap);
    if (!ok)
    {
        if (icon && icon->image)
            destroy_icon(icon);
        else
        {
            pmPointerImageFree(image);
            free(icon);
        }
        open32SetError(8);
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
        open32SetError(6);
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
        uint8 info[40] = {0};
        lx_guest_store_u32(info, 40);
        lx_guest_store_u32(info + 4, image->width);
        lx_guest_store_u32(info + 8, image->height);
        lx_guest_store_u16(info + 12, 1);
        lx_guest_store_u16(info + 14, 32);
        size_t bytes = (size_t)image->width * image->height * 4;
        uint8 *bits = malloc(bytes);
        if (!bits)
            ok = FALSE;
        if (bits)
        {
            for (unsigned pass = 0; pass < 2; pass++)
            {
                for (size_t i = 0; i < bytes / 4; i++)
                {
                    uint32 rgb = pass ? image->pixels[i] : (image->mask[i] ? 0xffffff : 0);
                    bits[i * 4] = (uint8)rgb;
                    bits[i * 4 + 1] = (uint8)(rgb >> 8);
                    bits[i * 4 + 2] = (uint8)(rgb >> 16);
                    bits[i * 4 + 3] = 0;
                }
                open32MetafileDib(dc, x, y, (LONG)image->width, (LONG)image->height, 0, 0,
                                  (LONG)image->width, (LONG)image->height, bits, info, 0,
                                  pass ? 0x660046 : 0x8800c6);
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
                uint32 pixel = (destination & (image->mask[i] ? 0xffffff : 0)) ^ image->pixels[i];
                ok = lx_pm_raster_write(dc, (LONG)px, (LONG)py, 1, 1, &pixel);
            }
        }
    lx_handle_release(&borrow);
    if (!ok)
        open32SetError(13);
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
        open32SetError(87);
        return 0;
    }
    uint32 error = 0;
    char *host = GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(path, &error) : NULL;
    size_t size = 0;
    LxHostFsError status;
    uint8 *bytes = host ? lx_host_file_snapshot(host, 16u * 1024u * 1024u, &size, &status) : NULL;
    free(host);
    if (!bytes)
    {
        open32SetError(error ? error : 2);
        return 0;
    }
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
            open32SetError(8);
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
            open32SetError(8);
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
    for (ULONG id = 1; id <= 65535; id++)
    {
        ULONG resource_size;
        if (GLoaderState.queryResourceSize &&
            !GLoaderState.queryResourceSize(module, 1, id, &resource_size) && resource_size)
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
        if (!GLoaderState.queryResourceSize(module, 1, resource, &length) &&
            !GLoaderState.getResource(module, 1, resource, &data))
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
            if (GLoaderState.freeResource)
                GLoaderState.freeResource(data);
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
