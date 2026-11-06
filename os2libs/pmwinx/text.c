/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 text uses measured PM glyphs and Windows top-left rectangles. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pm_text.h"
#include "hal/font.h"
static LONG text_step(Open32DC *dc, LxHostFont *font, uint8 character, ULONG space_index)
{
    int64_t result = lx_host_font_advance(font, lx_pm_codepoint(character));
    result += dc->character_extra;
    if (character == ' ' && dc->justify_breaks > 0 && space_index < (ULONG)dc->justify_breaks)
    {
        LONG quotient = dc->justify_extra / dc->justify_breaks,
             remainder = dc->justify_extra % dc->justify_breaks;
        result += quotient;
        if (space_index < (ULONG)abs(remainder))
            result += remainder < 0 ? -1 : 1;
    }
    return result < INT32_MIN ? INT32_MIN : result > INT32_MAX ? INT32_MAX : (LONG)result;
}
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
    if (!dc || (!open32IdentityMapping(dc) && !(flags & 0x400)) ||
        (!open32CanDraw(handle) && !(flags & 0x400)) || !rectangle || !text || count < -1 ||
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
    if (count < 0)
    {
        open32SetError(87);
        return FALSE;
    }
    return ExtTextOut(handle, x, y, 0, NULL, text, (ULONG)count, NULL);
}

BOOL GetTextExtentExPoint(ULONG handle, PCHAR text, LONG count, LONG maximum, PVOID fit,
                          PVOID advances, PVOID output)
{
    TRACE_NATIVE("GetTextExtentExPoint(%u, %p, %d, %d, %p, %p, %p)", handle, text, count, maximum,
                 fit, advances, output);
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !output || count < 0 || count > 65536 || (count && !text) || (fit && maximum < 0))
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
    ULONG fitted = 0, spaces = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint8 c = WinCpTranslateChar(0, 1004, (uint8)text[i], 437);
        int advance = text_step(dc, font, c, spaces);
        if (c == ' ')
            spaces++;
        if (advance == INT32_MIN)
        {
            open32SetError(87);
            return FALSE;
        }
        width += advance;
        if (width < INT32_MIN || width > INT32_MAX)
        {
            open32SetError(87);
            return FALSE;
        }
        if (width <= maximum)
            fitted = (ULONG)i + 1;
        if (advances)
            lx_guest_store_u32((uint8 *)advances + i * 4, (ULONG)width);
    }
    if (fit)
        lx_guest_store_u32(fit, fitted);
    lx_guest_store_u32(output, (ULONG)width);
    lx_guest_store_u32((uint8 *)output + 4, count ? (ULONG)(metrics.ascent + metrics.descent) : 0);
    return TRUE;
}
BOOL GetTextExtentPoint32(ULONG handle, PCHAR text, LONG count, PVOID output)
{
    return GetTextExtentExPoint(handle, text, count, 0, NULL, NULL, output);
}

BOOL GetTextExtentPoint(ULONG dc, PCHAR text, LONG count, PVOID size)
{
    return GetTextExtentPoint32(dc, text, count, size);
}
ULONG GetTabbedTextExtent(ULONG handle, PCHAR text, LONG count, LONG tab_count, PVOID tabs)
{
    TRACE_NATIVE("GetTabbedTextExtent(%u, %p, %d, %d, %p)", handle, text, count, tab_count, tabs);
    LxHostFontMetrics metrics;
    if (!open32FindDC(handle) || count < 0 || count > 65536 || (count && !text) || tab_count < 0 ||
        tab_count > 65536 || (tab_count && !tabs) ||
        !lx_host_font_metrics(lx_pm_font(handle), &metrics))
    {
        open32SetError(87);
        return 0;
    }
    LONG last = 0;
    for (LONG i = 0; i < tab_count; i++)
    {
        LONG next = (LONG)lx_guest_load_u32((uint8 *)tabs + i * 4);
        if (next <= last)
        {
            open32SetError(87);
            return 0;
        }
        last = next;
    }
    int64_t interval = tab_count == 1 ? last : (int64_t)metrics.average_width * 8;
    if (interval <= 0)
        interval = 8;
    int64_t width = 0;
    LONG next_tab = 0;
    LxHostFont *font = lx_pm_font(handle);
    for (LONG i = 0; i < count; i++)
    {
        if (text[i] == '\t')
        {
            while (next_tab < tab_count &&
                   (LONG)lx_guest_load_u32((uint8 *)tabs + next_tab * 4) <= width)
                next_tab++;
            width = tab_count > 1 && next_tab < tab_count
                        ? lx_guest_load_u32((uint8 *)tabs + next_tab++ * 4)
                        : (width / interval + 1) * interval;
        }
        else
        {
            uint8 c = WinCpTranslateChar(0, 1004, (uint8)text[i], 437);
            int advance = lx_host_font_advance(font, lx_pm_codepoint(c));
            if (advance < 0)
            {
                open32SetError(87);
                return 0;
            }
            width += advance;
        }
        if (width > INT32_MAX)
        {
            open32SetError(87);
            return 0;
        }
    }
    return ((ULONG)width & 0xFFFF) |
           (count ? ((ULONG)(metrics.ascent + metrics.descent) & 0xFFFF) << 16 : 0);
}

static BOOL text_out(ULONG handle, LONG x, LONG y, ULONG flags, PVOID rectangle, PCHAR text,
                     ULONG count, PLONG advances)
{
    TRACE_NATIVE("ExtTextOut(%u, %d, %d, %X, %p, %p, %u, %p)", handle, x, y, flags, rectangle, text,
                 count, advances);
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !open32CanDraw(handle) || count > 65536 || (count && !text) ||
        ((flags & 6) && !rectangle))
    {
        open32SetError(87);
        return FALSE;
    }
    if (flags & ~6u)
    {
        open32SetError(50);
        return FALSE;
    }
    RECTL area = {0};
    LONG height = open32DCHeight(handle);
    if (flags & 6)
    {
        const uint8 *wire = rectangle;
        LONG left = (LONG)lx_guest_load_u32(wire), top = (LONG)lx_guest_load_u32(wire + 4);
        LONG right = (LONG)lx_guest_load_u32(wire + 8), bottom = (LONG)lx_guest_load_u32(wire + 12);
        if (right < left || bottom < top || !open32MapPoint(dc, &left, &top, TRUE) ||
            !open32MapPoint(dc, &right, &bottom, TRUE))
        {
            open32SetError(87);
            return FALSE;
        }
        if (left > right)
        {
            LONG swap = left;
            left = right;
            right = swap;
        }
        if (top > bottom)
        {
            LONG swap = top;
            top = bottom;
            bottom = swap;
        }
        int64_t lower = (int64_t)height - bottom, upper = (int64_t)height - top;
        if (lower < INT32_MIN || upper > INT32_MAX)
        {
            open32SetError(87);
            return FALSE;
        }
        area = (RECTL){left, (LONG)lower, right, (LONG)upper};
    }
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    LONG length = 0;
    char *copy = count ? text_copy(text, (LONG)count, FALSE, 0, &length) : NULL;
    if ((count && !copy) || !lx_host_font_metrics(font, &metrics))
    {
        free(copy);
        open32SetError(87);
        return FALSE;
    }
    int64_t advance = 0;
    ULONG spaces = 0;
    for (LONG i = 0; i < length; i++)
    {
        advance += advances ? (LONG)lx_guest_load_u32((const uint8 *)advances + i * 4)
                            : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
    }
    LONG reference_x = dc->text_alignment & 1 ? dc->position_x : x;
    LONG reference_y = dc->text_alignment & 1 ? dc->position_y : y;
    LONG logical_x = x, device_x = reference_x, device_y = reference_y;
    if (!open32MapPoint(dc, &device_x, &device_y, TRUE))
    {
        free(copy);
        return FALSE;
    }
    ULONG horizontal = dc->text_alignment & 6, vertical = dc->text_alignment & 24;
    int64_t start = (int64_t)device_x - (horizontal == 2   ? advance
                                         : horizontal == 6 ? advance / 2
                                                           : 0);
    int64_t baseline = (int64_t)height - device_y +
                       (vertical == 24  ? 0
                        : vertical == 8 ? metrics.descent
                                        : -metrics.ascent);
    if (advance < INT32_MIN || advance > INT32_MAX || start < INT32_MIN || start > INT32_MAX ||
        ((dc->text_alignment & 1) &&
         ((int64_t)device_x + advance < INT32_MIN || (int64_t)device_x + advance > INT32_MAX)))
    {
        free(copy);
        open32SetError(87);
        return FALSE;
    }
    x = (LONG)start;
    int64_t position = start, leftmost = start, rightmost = start;
    BOOL valid = baseline - metrics.descent >= INT32_MIN && baseline + metrics.ascent <= INT32_MAX;
    spaces = 0;
    for (LONG i = 0; valid && i < length; i++)
    {
        int glyph = lx_host_font_advance(font, lx_pm_codepoint((uint8)copy[i]));
        int64_t step = advances ? (LONG)lx_guest_load_u32((const uint8 *)advances + i * 4)
                                : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
        valid = glyph >= 0 && position + glyph <= INT32_MAX;
        if (position < leftmost)
            leftmost = position;
        if (position + glyph > rightmost)
            rightmost = position + glyph;
        position += step;
        valid = valid && position >= INT32_MIN && position <= INT32_MAX;
    }
    if (!valid)
    {
        free(copy);
        open32SetError(87);
        return FALSE;
    }
    BOOL result = TRUE;
    if (flags & 2)
        result = WinFillRect(handle, &area, rgb_color(dc->background_color));
    else if (count && dc->background_mode == 2)
    {
        RECTL background = {(LONG)leftmost, (LONG)baseline - metrics.descent, (LONG)rightmost,
                            (LONG)baseline + metrics.ascent};
        if (!(flags & 4) || WinIntersectRect(0, &background, &background, &area))
            result = WinFillRect(handle, &background, rgb_color(dc->background_color));
    }
    open32MetafileText(handle, logical_x, y, text, count);
    spaces = 0;
    POINTL point = {x, (LONG)baseline};
    for (LONG i = 0; result && i < length; i++)
    {
        result = lx_pm_text(handle, &point, 1, copy + i, rgb_color(dc->text_color),
                            (flags & 4) ? &area : NULL);
        LONG step = advances ? (LONG)lx_guest_load_u32((const uint8 *)advances + i * 4)
                             : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
        point.x = (LONG)((int64_t)point.x + step);
    }
    if (result && (dc->text_alignment & 1))
    {
        LONG next_x = (LONG)((int64_t)device_x + advance), next_y = device_y;
        if (open32MapPoint(dc, &next_x, &next_y, FALSE))
        {
            dc->position_x = next_x;
            dc->position_y = reference_y;
        }
    }
    free(copy);
    return result;
}
/* Glyphs keep their device size under a mapping mode; origins and explicit advances scale. */
BOOL ExtTextOut(ULONG handle, LONG x, LONG y, ULONG flags, PVOID rectangle, PCHAR text, ULONG count,
                PLONG advances)
{
    Open32DC *dc = open32FindDC(handle);
    if (!advances || !dc || !count || count > 65536 || open32IdentityMapping(dc))
        return text_out(handle, x, y, flags, rectangle, text, count, advances);
    uint8 *scaled = malloc((size_t)count * 4);
    if (!scaled)
    {
        open32SetError(8);
        return FALSE;
    }
    int64_t numerator = llabs((int64_t)dc->viewport_width),
            denominator = llabs((int64_t)dc->window_width);
    for (ULONG i = 0; i < count; i++)
        lx_guest_store_u32(scaled + i * 4,
                           (ULONG)(LONG)((LONG)lx_guest_load_u32((const uint8 *)advances + i * 4) *
                                         numerator / denominator));
    BOOL result = text_out(handle, x, y, flags, rectangle, text, count, (PLONG)scaled);
    free(scaled);
    return result;
}

ULONG SetTextAlign(ULONG handle, ULONG alignment)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (alignment & ~0x1fu) || (alignment & 6) == 4 || (alignment & 24) == 16)
    {
        open32SetError(dc ? 87 : 6);
        return UINT32_MAX;
    }
    if ((alignment & 1) && (alignment & 6))
    {
        open32SetError(50);
        return UINT32_MAX;
    }
    ULONG previous = dc->text_alignment;
    dc->text_alignment = alignment;
    const LONG recorded = (LONG)alignment;
    open32MetafileWords(handle, 0x12e, 1, &recorded);
    return previous;
}
ULONG GetTextAlign(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return UINT32_MAX;
    }
    return dc->text_alignment;
}

LONG TabbedTextOut(ULONG h, LONG x, LONG y, PCHAR text, LONG count, LONG tab_count, PVOID tabs,
                   LONG origin)
{
    Open32DC *dc = open32FindDC(h);
    LxHostFontMetrics metrics;
    if (!tabs)
        tab_count = 0;
    if (!dc || count < 0 || count > 65536 || (count && !text) || tab_count < 0 ||
        tab_count > 65536 || !lx_host_font_metrics(lx_pm_font(h), &metrics))
    {
        open32SetError(87);
        return 0;
    }
    LONG last = 0;
    for (LONG i = 0; i < tab_count; i++)
    {
        LONG stop = (LONG)lx_guest_load_u32((uint8 *)tabs + i * 4);
        if (stop <= last)
        {
            open32SetError(87);
            return 0;
        }
        last = stop;
    }
    int64_t position = x, interval = tab_count == 1 ? last : (int64_t)metrics.average_width * 8;
    if (interval < 1)
        interval = 8;
    LONG start = 0;
    ULONG alignment = dc->text_alignment;
    dc->text_alignment = 0;
    BOOL ok = TRUE;
    for (LONG i = 0; ok && i <= count; i++)
        if (i == count || text[i] == '\t')
        {
            uint8 size[8] = {0};
            if (i > start)
            {
                ok = GetTextExtentPoint(h, text + start, i - start, size) &&
                     position >= INT32_MIN && position <= INT32_MAX &&
                     TextOut(h, (LONG)position, y, text + start, i - start);
                if (ok)
                    position += (LONG)lx_guest_load_u32(size);
            }
            if (i < count)
            {
                int64_t relative = position - origin, next = (relative / interval + 1) * interval;
                BOOL found = FALSE;
                if (tab_count > 1)
                    for (LONG t = 0; t < tab_count; t++)
                    {
                        LONG stop = (LONG)lx_guest_load_u32((uint8 *)tabs + t * 4);
                        if (stop > relative)
                        {
                            next = stop;
                            found = TRUE;
                            break;
                        }
                    }
                (void)found;
                position = (int64_t)origin + next;
                start = i + 1;
            }
        }
    dc->text_alignment = alignment;
    if (!ok || position - x < 0 || position - x > INT32_MAX)
    {
        open32SetError(87);
        return 0;
    }
    return (LONG)(((ULONG)(position - x) & 0xffff) |
                  ((ULONG)(metrics.ascent + metrics.descent) & 0xffff) << 16);
}
