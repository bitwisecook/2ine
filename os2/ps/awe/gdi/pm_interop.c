/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinTranslateDeviceRects and WinTranslateGraphicsObjectHandle: Open32 and PM handles and
   coordinates for the same objects. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmgpip.h"
#include "os2/include/private/pmdevp.h"

enum
{
    /* WinTranslateDeviceRects converts at most this many rectangles per call. */
    TRANSLATE_RECTS_MAX = 65536,
    /* Palettes translate with up to 256 entries in either direction. */
    TRANSLATE_PALETTE_ENTRIES_MAX = 256
};

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
                                  type == WIN32_OBJ_PAL      ? LX_HANDLE_PM_PALETTE
                                  : type == WIN32_OBJ_REGION ? LX_HANDLE_PM_REGION
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
    if (count < 0 || count > TRANSLATE_RECTS_MAX || (count && !rectangles))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG height;
    if (h)
    {
        if (!open32FindDC(h))
        {
            open32SetError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        height = open32DCHeight(h);
    }
    else
    {
        RECTL r;
        if (!WinQueryWindowRect(open32ClientWindow(window), &r))
        {
            open32SetError(ERROR_INVALID_WINDOW_HANDLE);
            return FALSE;
        }
        height = r.yTop - r.yBottom;
    }
    for (LONG i = 0; i < count; i++)
    {
        uint8 *p = (uint8 *)rectangles + i * WIN32_RECT_SIZE;
        LONG top = (LONG)lx_guest_load_u32(p + WIN32_RECT_TOP),
             bottom = (LONG)lx_guest_load_u32(p + WIN32_RECT_BOTTOM);
        int64_t t = (int64_t)height - bottom, b = (int64_t)height - top;
        if (t < INT32_MIN || t > INT32_MAX || b < INT32_MIN || b > INT32_MAX)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        lx_guest_store_u32(p + WIN32_RECT_TOP, (ULONG)t);
        lx_guest_store_u32(p + WIN32_RECT_BOTTOM, (ULONG)b);
    }
    return TRUE;
}

ULONG WinTranslateGraphicsObjectHandle(ULONG object, ULONG direction, ULONG type)
{
    direction &= 255;
    if (type != WIN32_OBJ_PAL && type != WIN32_OBJ_BITMAP && type != WIN32_OBJ_REGION &&
        type != WIN32_OBJ_METAFILE)
        return 0;
    ULONG cached = translated_handle(object, direction, type);
    if (cached)
        return cached;
    /* Open32 and PM share bitmap handles. */
    if (type == WIN32_OBJ_BITMAP)
    {
        LxHandleBorrow borrow = {0};
        BOOL ok = lx_pm_bitmap(object, &borrow) != NULL;
        lx_handle_release(&borrow);
        if (ok)
            return object;
    }
    if (type == WIN32_OBJ_PAL)
    {
        if (direction == WINX2PM)
        {
            ULONG n = GetPaletteEntries(object, 0, 0, NULL);
            if (!n || n > TRANSLATE_PALETTE_ENTRIES_MAX)
            {
                open32SetError(ERROR_INVALID_PARAMETER);
                return 0;
            }
            uint8 entries[TRANSLATE_PALETTE_ENTRIES_MAX * WIN32_PALETTEENTRY_SIZE];
            ULONG colors[TRANSLATE_PALETTE_ENTRIES_MAX];
            if (GetPaletteEntries(object, 0, n, entries) != n)
                return 0;
            for (ULONG i = 0; i < n; i++)
            {
                const uint8 *entry = entries + i * WIN32_PALETTEENTRY_SIZE;
                colors[i] = (ULONG)entry[WIN32_PALETTEENTRY_RED] << 16 |
                            (ULONG)entry[WIN32_PALETTEENTRY_GREEN] << 8 |
                            entry[WIN32_PALETTEENTRY_BLUE];
            }
            ULONG native = GpiCreatePalette(open32Presentation(), 0, 0, n, colors);
            return remember_handle(object, native, type, native);
        }
        LONG n = GpiQueryPaletteInfo(object, 0, 0, 0, 0, NULL);
        if (n < 1 || n > TRANSLATE_PALETTE_ENTRIES_MAX)
        {
            open32SetError(ERROR_INVALID_HANDLE);
            return 0;
        }
        ULONG colors[TRANSLATE_PALETTE_ENTRIES_MAX];
        if (GpiQueryPaletteInfo(object, 0, 0, 0, (ULONG)n, colors) != n)
            return 0;
        uint8 entries[WIN32_LOGPALETTE_ENTRIES +
                      TRANSLATE_PALETTE_ENTRIES_MAX * WIN32_PALETTEENTRY_SIZE];
        lx_guest_store_u16(entries + WIN32_LOGPALETTE_VERSION, WIN32_PALETTE_VERSION);
        lx_guest_store_u16(entries + WIN32_LOGPALETTE_ENTRY_COUNT, (uint16)n);
        for (LONG i = 0; i < n; i++)
        {
            uint8 *entry = entries + WIN32_LOGPALETTE_ENTRIES + i * WIN32_PALETTEENTRY_SIZE;
            entry[WIN32_PALETTEENTRY_RED] = (uint8)(colors[i] >> 16);
            entry[WIN32_PALETTEENTRY_GREEN] = (uint8)(colors[i] >> 8);
            entry[WIN32_PALETTEENTRY_BLUE] = (uint8)colors[i];
            entry[WIN32_PALETTEENTRY_FLAGS] = 0;
        }
        ULONG palette = CreatePalette(entries);
        return remember_handle(palette, object, type, palette);
    }
    if (type == WIN32_OBJ_REGION)
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
                    if (!regionBoolean(out.resource, r, NULL, CRGN_COPY))
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
    if (type == WIN32_OBJ_METAFILE && direction)
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
    else if (type == WIN32_OBJ_METAFILE)
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
    open32SetError(ERROR_INVALID_HANDLE);
    return 0;
}
