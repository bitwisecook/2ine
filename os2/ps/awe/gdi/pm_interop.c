/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinTranslateDeviceRects and WinTranslateGraphicsObjectHandle: Open32 and PM handles and
   coordinates for the same objects. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmgpip.h"
#include "os2/include/private/pmdevp.h"

/* Translating one object again yields the same partner while both handles stay valid. */
typedef struct TranslatedHandle
{
    ULONG open32, native, type;
    struct TranslatedHandle *next;
} TranslatedHandle;

static _Thread_local TranslatedHandle *translations;

static BOOL native_handle_valid(ULONG handle, ULONG type)
{
    LxHandleBorrow borrow = {0};
    BOOL valid = lx_handle_borrow(handle,
                                  type == 5   ? LX_HANDLE_PM_PALETTE
                                  : type == 8 ? LX_HANDLE_PM_REGION
                                              : LX_HANDLE_PM_METAFILE,
                                  &borrow);
    lx_handle_release(&borrow);
    return valid;
}

static ULONG translated_handle(ULONG object, ULONG direction, ULONG type)
{
    TranslatedHandle **link = &translations;
    while (*link)
    {
        TranslatedHandle *entry = *link;
        if (GetObjectType(entry->open32) != entry->type ||
            !native_handle_valid(entry->native, entry->type))
        {
            *link = entry->next;
            free(entry);
            continue;
        }
        if (entry->type == type && (direction ? entry->native : entry->open32) == object)
            return direction ? entry->open32 : entry->native;
        link = &entry->next;
    }
    return 0;
}

static ULONG remember_handle(ULONG open32, ULONG native, ULONG type, ULONG result)
{
    TranslatedHandle *entry = open32 && native ? malloc(sizeof *entry) : NULL;
    if (entry)
    {
        *entry = (TranslatedHandle){open32, native, type, translations};
        translations = entry;
    }
    return result;
}

BOOL WinTranslateDeviceRects(ULONG h, ULONG window, PVOID rectangles, LONG count, ULONG direction)
{
    (void)direction;
    if (!h && !window)
        return FALSE;
    if (count < 0 || count > 65536 || (count && !rectangles))
    {
        open32SetError(87);
        return FALSE;
    }
    LONG height;
    if (h)
    {
        if (!open32FindDC(h))
        {
            open32SetError(6);
            return FALSE;
        }
        height = open32DCHeight(h);
    }
    else
    {
        RECTL r;
        if (!WinQueryWindowRect(open32ClientWindow(window), &r))
        {
            open32SetError(1400);
            return FALSE;
        }
        height = r.yTop - r.yBottom;
    }
    for (LONG i = 0; i < count; i++)
    {
        uint8 *p = (uint8 *)rectangles + i * 16;
        LONG top = (LONG)lx_guest_load_u32(p + 4), bottom = (LONG)lx_guest_load_u32(p + 12);
        int64_t t = (int64_t)height - bottom, b = (int64_t)height - top;
        if (t < INT32_MIN || t > INT32_MAX || b < INT32_MIN || b > INT32_MAX)
        {
            open32SetError(87);
            return FALSE;
        }
        lx_guest_store_u32(p + 4, (ULONG)t);
        lx_guest_store_u32(p + 12, (ULONG)b);
    }
    return TRUE;
}

ULONG WinTranslateGraphicsObjectHandle(ULONG object, ULONG direction, ULONG type)
{
    direction &= 255;
    if (type != 5 && type != 7 && type != 8 && type != 9)
        return 0;
    ULONG cached = translated_handle(object, direction, type);
    if (cached)
        return cached;
    if (type == 7)
    {
        LxHandleBorrow borrow = {0};
        BOOL ok = lx_pm_bitmap(object, &borrow) != NULL;
        lx_handle_release(&borrow);
        if (ok)
            return object;
    }
    if (type == 5)
    {
        if (direction == 0)
        {
            ULONG n = GetPaletteEntries(object, 0, 0, NULL);
            if (!n || n > 256)
            {
                open32SetError(87);
                return 0;
            }
            uint8 entries[1024];
            ULONG colors[256];
            if (GetPaletteEntries(object, 0, n, entries) != n)
                return 0;
            for (ULONG i = 0; i < n; i++)
                colors[i] = (ULONG)entries[i * 4] << 16 | (ULONG)entries[i * 4 + 1] << 8 |
                            entries[i * 4 + 2];
            ULONG native = GpiCreatePalette(open32Presentation(), 0, 0, n, colors);
            return remember_handle(object, native, type, native);
        }
        LONG n = GpiQueryPaletteInfo(object, 0, 0, 0, 0, NULL);
        if (n < 1 || n > 256)
        {
            open32SetError(6);
            return 0;
        }
        ULONG colors[256];
        if (GpiQueryPaletteInfo(object, 0, 0, 0, (ULONG)n, colors) != n)
            return 0;
        uint8 entries[1028];
        lx_guest_store_u16(entries, 0x300);
        lx_guest_store_u16(entries + 2, (uint16)n);
        for (LONG i = 0; i < n; i++)
        {
            entries[4 + i * 4] = (uint8)(colors[i] >> 16);
            entries[5 + i * 4] = (uint8)(colors[i] >> 8);
            entries[6 + i * 4] = (uint8)colors[i];
            entries[7 + i * 4] = 0;
        }
        ULONG palette = CreatePalette(entries);
        return remember_handle(palette, object, type, palette);
    }
    if (type == 8)
    {
        LxHandleBorrow borrow = {0};
        LxHandleKind kind = direction ? LX_HANDLE_PM_REGION : LX_HANDLE_OPEN32_REGION;
        if (lx_handle_borrow(object, kind, &borrow))
        {
            PmRegion *r = borrow.resource;
            ULONG result = 0;
            if (direction)
            {
                result = CreateRectRgn(0, 0, 0, 0);
                LxHandleBorrow out = {0};
                if (result && lx_handle_borrow(result, LX_HANDLE_OPEN32_REGION, &out))
                {
                    if (!regionBoolean(out.resource, r, NULL, 2))
                    {
                        DeleteObject(result);
                        result = 0;
                    }
                    lx_handle_release(&out);
                }
            }
            else
            {
                ULONG ps = GetDC(0);
                if (ps)
                {
                    result = GpiCreateRegion(ps, (LONG)r->count, r->rects);
                    ReleaseDC(0, ps);
                }
            }
            lx_handle_release(&borrow);
            return direction ? remember_handle(result, object, type, result)
                             : remember_handle(object, result, type, result);
        }
    }
    /* A native metafile object carries Windows metafile bytes across the two APIs; GPI order
       streams have no WMF reading, so they do not translate back. */
    if (type == 9 && direction)
    {
        LONG length = GpiQueryMetaFileLength(object);
        uint8 *bytes = length > 0 ? malloc((size_t)length) : NULL;
        ULONG result = 0;
        if (bytes && GpiQueryMetaFileBits(object, 0, length, bytes))
            result = SetMetaFileBitsEx((ULONG)length, bytes);
        free(bytes);
        if (result)
            return remember_handle(result, object, type, result);
    }
    else if (type == 9)
    {
        ULONG length = GetMetaFileBitsEx(object, 0, NULL);
        uint8 *bytes = length ? malloc(length) : NULL;
        ULONG result = 0;
        if (bytes && GetMetaFileBitsEx(object, length, bytes) == length)
            result = lx_pm_metafile_create(bytes, length);
        free(bytes);
        if (result)
            return remember_handle(object, result, type, result);
    }
    open32SetError(6);
    return 0;
}
