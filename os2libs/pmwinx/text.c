/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 text uses measured PM glyphs and Windows top-left rectangles. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pm_text.h"
#include "hal/font.h"
static LONG rgb_color(ULONG color)
{
    return (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
}
static char *text_copy(const char *text, LONG count, BOOL format, ULONG flags, LONG *length)
{
    if (count < -1 || count > 65536 || !text)
        return NULL;
    if (count == -1)
        count = (LONG)strlen(text);
    if (count > 65536)
        return NULL;
    char *copy = malloc((size_t)count * 8 + 1);
    if (!copy)
        return NULL;
    LONG used = 0, column = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint8 c = (uint8)text[i];
        if (format && !(flags & 0x800) && c == '&')
        {
            if (i + 1 == count)
                continue;
            if (text[i + 1] == '&')
                i++;
            else
                continue;
        }
        if (format && c == '\r')
        {
            if (i + 1 < count && text[i + 1] == '\n')
                i++;
            c = '\n';
        }
        if (format && c == '\n' && (flags & 0x20))
            c = ' ';
        if (format && c == '\t' && (flags & 0x40))
        {
            unsigned spaces = 8 - (unsigned)column % 8;
            while (spaces--)
            {
                copy[used++] = ' ';
                column++;
            }
            continue;
        }
        copy[used++] = (char)WinCpTranslateChar(0, 1004, c, 437);
        column = c == '\n' ? 0 : column + 1;
    }
    copy[used] = 0;
    *length = used;
    return copy;
}
LONG DrawText(ULONG handle, PSZ text, LONG count, PVOID rectangle, ULONG flags)
{
    TRACE_NATIVE("DrawText(%u, '%.*s', %d, %p, %X)", handle, count >= 0 ? count : 80, text, count,
                 rectangle, flags);
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (!open32CanDraw(handle) && !(flags & 0x400)) || !rectangle || !text || count < -1 ||
        (flags & 0xFFFF8000))
    {
        open32SetError(87);
        return 0;
    }
    const uint8 *wire = rectangle;
    LONG left = (LONG)lx_guest_load_u32(wire), top = (LONG)lx_guest_load_u32(wire + 4);
    LONG right = (LONG)lx_guest_load_u32(wire + 8), bottom = (LONG)lx_guest_load_u32(wire + 12);
    int64_t y_top = (int64_t)open32DCHeight(handle) - top,
            y_bottom = (int64_t)open32DCHeight(handle) - bottom;
    if (right < left || bottom < top || (int64_t)right - left > INT32_MAX ||
        (int64_t)bottom - top > INT32_MAX || y_top < INT32_MIN || y_top > INT32_MAX ||
        y_bottom < INT32_MIN || y_bottom > INT32_MAX)
    {
        open32SetError(87);
        return 0;
    }
    LONG length;
    char *copy = text_copy(text, count, TRUE, flags, &length);
    if (!copy)
    {
        open32SetError(87);
        return 0;
    }
    ULONG pm_flags = (flags & 1 ? 0x100 : 0) | (flags & 2 ? 0x200 : 0) | (flags & 4 ? 0x400 : 0) |
                     (flags & 8 ? 0x800 : 0) | ((flags & 0x10) && !(flags & 0x20) ? 0x4000 : 0) |
                     (flags & 0x200 ? 0x80 : 0);
    RECTL rect = {left, (LONG)y_bottom, right, (LONG)y_top}, measured = rect;
    LONG foreground = rgb_color(dc->text_color), background = rgb_color(dc->background_color);
    LONG result = 0;
    if (length && lx_pm_draw_text(handle, length, copy, &measured, foreground, background,
                                  pm_flags | 2, TRUE, FALSE))
    {
        result = measured.yTop - measured.yBottom;
        if (flags & 0x400)
        {
            lx_guest_store_u32((uint8 *)rectangle + 8, (ULONG)measured.xRight);
            lx_guest_store_u32((uint8 *)rectangle + 12, (ULONG)(top + result));
        }
        else if (!lx_pm_draw_text(handle, length, copy, &rect, foreground, background, pm_flags,
                                  !(flags & 0x100), dc->background_mode == 2))
            result = 0;
    }
    free(copy);
    return result;
}
BOOL TextOut(ULONG handle, LONG x, LONG y, PSZ text, LONG count)
{
    Open32DC *dc = open32FindDC(handle);
    LONG length;
    char *copy = dc && open32CanDraw(handle) && count >= 0
                     ? text_copy(text, count, FALSE, 0, &length)
                     : NULL;
    LxHostFontMetrics metrics;
    if (!copy || !lx_host_font_metrics(lx_pm_font(handle), &metrics))
    {
        free(copy);
        open32SetError(87);
        return FALSE;
    }
    int64_t baseline = (int64_t)open32DCHeight(handle) - y - metrics.ascent, width = 0;
    for (LONG i = 0; i < length; i++)
        width += lx_host_font_advance(lx_pm_font(handle), lx_pm_codepoint((uint8)copy[i]));
    if (baseline - metrics.descent < INT32_MIN || baseline + metrics.ascent > INT32_MAX ||
        (int64_t)x + width < INT32_MIN || (int64_t)x + width > INT32_MAX)
    {
        free(copy);
        open32SetError(87);
        return FALSE;
    }
    if (dc->background_mode == 2)
    {
        RECTL rect = {x, (LONG)baseline - metrics.descent, (LONG)(x + width),
                      (LONG)baseline + metrics.ascent};
        WinFillRect(handle, &rect, rgb_color(dc->background_color));
    }
    POINTL point = {x, (LONG)baseline};
    BOOL result = lx_pm_text(handle, &point, length, copy, rgb_color(dc->text_color), NULL);
    free(copy);
    return result;
}

BOOL GetTextExtentPoint32(ULONG handle, PCHAR text, LONG count, PVOID output)
{
    if (!open32FindDC(handle) || !output || count < 0 || count > 65536 || (count && !text))
    {
        open32SetError(87);
        return FALSE;
    }
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
    {
        open32SetError(6);
        return FALSE;
    }
    int64_t width = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint8 c = WinCpTranslateChar(0, 1004, (uint8)text[i], 437);
        int advance = lx_host_font_advance(font, lx_pm_codepoint(c));
        if (advance < 0)
        {
            open32SetError(87);
            return FALSE;
        }
        width += advance;
    }
    if (width > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)width);
    lx_guest_store_u32((uint8 *)output + 4, count ? (ULONG)(metrics.ascent + metrics.descent) : 0);
    return TRUE;
}

BOOL GetTextExtentPoint(ULONG dc, PCHAR text, LONG count, PVOID size)
{
    return GetTextExtentPoint32(dc, text, count, size);
}
