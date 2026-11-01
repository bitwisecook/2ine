/* Public domain. No rights reserved. Wholly AI-authored. */
/* LOGFONT32 records retain requested names while GPI selects available outline faces. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmgpi.h"
#include "hal/font.h"
typedef struct Open32Font
{
    uint8 record[60];
    FATTRS attributes;
    BOOL stock;
} Open32Font;
ULONG CreateFontIndirect(PVOID record)
{
    if (!record || !memchr((uint8 *)record + 28, 0, 32))
    {
        open32SetError(87);
        return 0;
    }
    const uint8 *bytes = record;
    LONG height = (LONG)lx_guest_load_u32(bytes);
    LONG width = (LONG)lx_guest_load_u32(bytes + 4);
    if (height < -512 || height > 512 || width < 0 || width > 512 || lx_guest_load_u32(bytes + 8) ||
        lx_guest_load_u32(bytes + 12))
    {
        open32SetError(87);
        return 0;
    }
    Open32Font *font = calloc(1, sizeof *font);
    if (!font)
    {
        open32SetError(8);
        return 0;
    }
    memcpy(font->record, bytes, 60);
    unsigned family = 0;
    const char *face = (const char *)bytes + 28;
    if (strcasestr(face, "courier") || strcasestr(face, "mono"))
        family = 2;
    else if (strcasestr(face, "times") || strcasestr(face, "tms") || strcasestr(face, "roman"))
        family = 1;
    BOOL bold = lx_guest_load_u32(bytes + 16) >= 600;
    BOOL italic = bytes[20] != 0;
    unsigned index = family * 4 + (bold ? 1 : 0) + (italic ? 2 : 0);
    unsigned pixels = height < 0 ? (unsigned)-height : height ? (unsigned)height : 16;
    if (height > 0)
    {
        unsigned difference = UINT32_MAX, selected = pixels;
        for (unsigned n = 1; n <= (unsigned)height; n++)
        {
            LxHostFont *host = lx_host_font_open(index, n);
            LxHostFontMetrics metrics;
            if (host && lx_host_font_metrics(host, &metrics))
            {
                LONG delta = metrics.ascent + metrics.descent - height;
                unsigned distance = delta < 0 ? (unsigned)-delta : (unsigned)delta;
                if (distance < difference)
                {
                    selected = n;
                    difference = distance;
                }
            }
            lx_host_font_close(host);
        }
        pixels = selected;
    }
    font->attributes.usRecordLength = sizeof(FATTRS);
    const char *families[] = {"Helv", "Tms Rmn", "Courier"};
    strcpy(font->attributes.szFacename, families[family]);
    font->attributes.fsSelection =
        (bold ? 0x20 : 0) | (italic ? 1 : 0) | (bytes[21] ? 2 : 0) | (bytes[22] ? 8 : 0);
    font->attributes.lMaxBaselineExt = (LONG)pixels;
    font->attributes.lAveCharWidth = width;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_FONT, font, free);
    if (!handle)
    {
        free(font);
        open32SetError(8);
    }
    return handle;
}
ULONG open32SelectFont(Open32DC *dc, ULONG handle)
{
    LxHandleBorrow next = {0};
    if (!dc || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &next))
        return 0;
    Open32Font *font = next.resource;
    ULONG previous = dc->font_handle;
    if (!GpiSetCharSet(dc->ps, 0) || !GpiDeleteSetId(dc->ps, 1) ||
        !GpiCreateLogFont(dc->ps, NULL, 1, &font->attributes) || !GpiSetCharSet(dc->ps, 1))
    {
        lx_handle_release(&next);
        open32SetError(8);
        return 0;
    }
    lx_handle_release(&dc->font);
    dc->font = next;
    dc->font_handle = handle;
    return previous;
}
BOOL open32DeleteFont(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &borrow))
        return FALSE;
    Open32Font *font = borrow.resource;
    BOOL result =
        !font->stock && lx_handle_close_guarded(handle, LX_HANDLE_OPEN32_FONT, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}
LONG open32FontObject(ULONG handle, LONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &borrow))
        return 0;
    Open32Font *font = borrow.resource;
    LONG result = !output ? 60 : capacity >= 60 ? 60 : 0;
    if (output && result)
        memcpy(output, font->record, 60);
    lx_handle_release(&borrow);
    return result;
}

ULONG GetStockObject(LONG index)
{
    if (index >= 0 && index <= 5)
        return open32StockBrush(index);
    static _Thread_local ULONG stocks[18];
    if (index < 10 || index == 15 || index > 17)
    {
        open32SetError(87);
        return 0;
    }
    if (!stocks[index])
    {
        uint8 record[60] = {0};
        lx_guest_store_u32(record, 16);
        lx_guest_store_u32(record + 16, 400);
        strcpy((char *)record + 28, index == 10 || index == 11 || index == 16 ? "Courier" : "Helv");
        ULONG handle = CreateFontIndirect(record);
        LxHandleBorrow borrow = {0};
        if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &borrow))
            return 0;
        ((Open32Font *)borrow.resource)->stock = TRUE;
        lx_handle_release(&borrow);
        stocks[index] = handle;
    }
    return stocks[index];
}
