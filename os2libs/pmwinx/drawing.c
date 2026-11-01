/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows DC coordinates have their origin at the top left of the PM drawable. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwinx/drawing.h"
static _Thread_local Open32DC *contexts;
Open32DC *open32FindDC(ULONG handle)
{
    for (Open32DC *dc = contexts; dc; dc = dc->next)
        if (dc->ps == handle && lx_pm_valid_ps(handle))
            return dc;
    return NULL;
}
LONG open32DCHeight(ULONG handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps)
        return 0;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    return bitmap ? (LONG)bitmap->height : ps->window ? ps->window->h : 0;
}
static Open32DC *register_context(HPS ps, HWND window, ULONG kind)
{
    Open32DC *dc = calloc(1, sizeof *dc);
    if (!dc)
        return NULL;
    dc->ps = ps;
    dc->window = window;
    dc->kind = kind;
    dc->background_mode = 2;
    dc->background_color = 0xFFFFFF;
    GpiCreateLogColorTable(ps, 0, 3, 0, 0, NULL);
    GpiSetColor(ps, 0);
    GpiSetBackColor(ps, 0xFFFFFF);
    GpiSetBackMix(ps, 2);
    open32SelectFont(dc, GetStockObject(13));
    if (!dc->font.resource)
    {
        free(dc);
        return NULL;
    }
    open32SelectBrush(dc, GetStockObject(0));
    if (!dc->brush.resource)
    {
        lx_handle_release(&dc->font);
        free(dc);
        return NULL;
    }
    dc->next = contexts;
    contexts = dc;
    return dc;
}
static BOOL discard_context(Open32DC *dc)
{
    Open32DC **link = &contexts;
    while (*link != dc)
        link = &(*link)->next;
    *link = dc->next;
    if (dc->kind == 1)
    {
        GpiSetBitmap(dc->ps, 0);
        if (dc->default_bitmap)
            GpiDeleteBitmap(dc->default_bitmap);
        GpiDestroyPS(dc->ps);
    }
    else if (dc->kind == 2)
        WinEndPaint(dc->ps);
    else
        WinReleasePS(dc->ps);
    lx_handle_release(&dc->brush);
    lx_handle_release(&dc->font);
    free(dc);
    return TRUE;
}
ULONG GetDC(ULONG window)
{
    if (!open32Presentation())
    {
        open32SetError(8);
        return 0;
    }
    HWND target = window ? window : HWND_DESKTOP;
    HPS ps = WinGetPS(target);
    if (!ps || !register_context(ps, window, 3))
    {
        if (ps)
            WinReleasePS(ps);
        open32SetError(1400);
        return 0;
    }
    return ps;
}
LONG ReleaseDC(ULONG window, ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || dc->kind != 3 || dc->window != window)
    {
        open32SetError(6);
        return 0;
    }
    return discard_context(dc);
}
ULONG CreateCompatibleDC(ULONG source)
{
    if (source && !open32FindDC(source))
    {
        open32SetError(6);
        return 0;
    }
    if (!open32Presentation())
    {
        open32SetError(8);
        return 0;
    }
    SIZEL size = {0, 0};
    HPS ps = GpiCreatePS(0, 0, &size, 0);
    Open32DC *dc = ps ? register_context(ps, 0, 1) : NULL;
    if (!dc)
    {
        if (ps)
            GpiDestroyPS(ps);
        open32SetError(8);
        return 0;
    }
    dc->default_bitmap = lx_pm_bitmap_create(1, 1, 1, 1);
    if (!dc->default_bitmap || GpiSetBitmap(ps, dc->default_bitmap) == UINT32_MAX)
    {
        discard_context(dc);
        open32SetError(8);
        return 0;
    }
    return ps;
}
BOOL DeleteDC(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || dc->kind != 1)
    {
        open32SetError(6);
        return FALSE;
    }
    return discard_context(dc);
}
ULONG SelectObject(ULONG handle, ULONG object)
{
    Open32DC *dc = open32FindDC(handle);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(object, &borrow);
    if (dc && !bitmap)
    {
        lx_handle_release(&borrow);
        ULONG brush = open32SelectBrush(dc, object);
        return brush ? brush : open32SelectFont(dc, object);
    }
    if (!dc || dc->kind != 1 || !bitmap)
    {
        lx_handle_release(&borrow);
        open32SetError(6);
        return 0;
    }
    ULONG previous = GpiSetBitmap(handle, object);
    lx_handle_release(&borrow);
    if (previous == UINT32_MAX)
    {
        open32SetError(87);
        return 0;
    }
    return previous;
}
BOOL BitBlt(ULONG destination, LONG x, LONG y, LONG width, LONG height, ULONG source, LONG source_x,
            LONG source_y, ULONG operation)
{
    if (!open32FindDC(destination) || !open32FindDC(source) || width <= 0 || height <= 0 ||
        width > 16384 || height > 16384)
    {
        open32SetError(87);
        return FALSE;
    }
    int64_t right = (int64_t)x + width, bottom = (int64_t)open32DCHeight(destination) - y - height;
    int64_t src_bottom = (int64_t)open32DCHeight(source) - source_y - height;
    if (right < INT32_MIN || right > INT32_MAX || bottom < INT32_MIN || bottom > INT32_MAX ||
        src_bottom < INT32_MIN || src_bottom > INT32_MAX || (int64_t)source_x + width > INT32_MAX ||
        bottom + height > INT32_MAX || src_bottom + height > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    POINTL points[4] = {{x, (LONG)bottom},
                        {(LONG)right, (LONG)(bottom + height)},
                        {source_x, (LONG)src_bottom},
                        {source_x + width, (LONG)(src_bottom + height)}};
    return GpiBitBlt(destination, source, 4, points, (LONG)((operation >> 16) & 255), 0) == 1;
}
ULONG BeginPaint(ULONG window, PVOID output)
{
    if (!output)
    {
        open32SetError(87);
        return 0;
    }
    RECTL rect;
    HPS ps = WinBeginPaint(window, 0, &rect);
    if (!ps || !register_context(ps, window, 2))
    {
        if (ps)
            WinEndPaint(ps);
        open32SetError(1400);
        return 0;
    }
    uint8 bytes[64] = {0};
    lx_guest_store_u32(bytes, ps);
    lx_guest_store_u32(bytes + 4, !open32EraseBackground(window, ps));
    lx_guest_store_u32(bytes + 8, (ULONG)rect.xLeft);
    lx_guest_store_u32(bytes + 12, (ULONG)(open32DCHeight(ps) - rect.yTop));
    lx_guest_store_u32(bytes + 16, (ULONG)rect.xRight);
    lx_guest_store_u32(bytes + 20, (ULONG)(open32DCHeight(ps) - rect.yBottom));
    memcpy(output, bytes, sizeof bytes);
    return ps;
}
BOOL EndPaint(ULONG window, PVOID input)
{
    Open32DC *dc = input ? open32FindDC(lx_guest_load_u32(input)) : NULL;
    if (!dc || dc->kind != 2 || dc->window != window)
    {
        open32SetError(6);
        return FALSE;
    }
    return discard_context(dc);
}

LONG SetBkMode(ULONG handle, LONG mode)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (mode != 1 && mode != 2))
    {
        open32SetError(dc ? 87 : 6);
        return 0;
    }
    LONG previous = dc->background_mode;
    if (!GpiSetBackMix(handle, mode == 1 ? 5 : 2))
        return 0;
    dc->background_mode = mode;
    return previous;
}
static LONG rgb_color(ULONG color)
{
    return (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
}
ULONG SetTextColor(ULONG handle, ULONG color)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (color & 0xFF000000))
    {
        open32SetError(dc ? 87 : 6);
        return UINT32_MAX;
    }
    ULONG previous = dc->text_color;
    if (!GpiSetColor(handle, rgb_color(color)))
        return UINT32_MAX;
    dc->text_color = color;
    return previous;
}
ULONG SetBkColor(ULONG handle, ULONG color)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (color & 0xFF000000))
    {
        open32SetError(dc ? 87 : 6);
        return UINT32_MAX;
    }
    ULONG previous = dc->background_color;
    if (!GpiSetBackColor(handle, rgb_color(color)))
        return UINT32_MAX;
    dc->background_color = color;
    return previous;
}

void open32DrawingCleanup(void)
{
    while (contexts)
        discard_context(contexts);
}
ULONG CreateCompatibleBitmap(ULONG handle, LONG width, LONG height)
{
    if (!open32FindDC(handle) || width < 0 || height < 0)
    {
        open32SetError(87);
        return 0;
    }
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmBitmap *selected = ps->bitmap_lease.resource;
    ULONG result = width && height ? lx_pm_bitmap_create((ULONG)width, (ULONG)height, 1,
                                                         selected ? selected->depth : 24)
                                   : lx_pm_bitmap_create(1, 1, 1, 1);
    if (!result)
        open32SetError(8);
    return result;
}
ULONG GetPixel(ULONG handle, LONG x, LONG y)
{
    uint32 pixel;
    if (!open32FindDC(handle) || x < 0 || y < 0 || y >= open32DCHeight(handle) ||
        !lx_pm_raster_read(handle, x, open32DCHeight(handle) - 1 - y, 1, 1, &pixel))
    {
        open32SetError(87);
        return UINT32_MAX;
    }
    return (ULONG)rgb_color(pixel);
}
