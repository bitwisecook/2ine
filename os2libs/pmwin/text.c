/* Public domain. No rights reserved. Wholly AI-authored. */
/* Byte strings use the PS codepage and the selected measured outline face. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pm_text.h"
LONG lx_pm_text(HPS handle, const POINTL *point, LONG count, const char *text, LONG color,
                const RECTL *bounds)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point || count < 0 || count > 65536 || (count && !text))
        return 0;
    LxHostFont *font = lx_pm_font(handle);
    LONG rgb = lx_pm_resolve_color(handle, color);
    if (!font || rgb < 0)
        return 0;
    int64_t x = point->x;
    for (LONG i = 0; i < count; i++)
    {
        LxHostGlyph glyph;
        if (!lx_host_font_glyph(font, lx_pm_codepoint((uint8)text[i]), &glyph))
            return 0;
        BOOL ok = x >= INT32_MIN && x <= INT32_MAX &&
                  lx_pm_mask(handle, (LONG)x + glyph.left, point->y + glyph.top - glyph.height,
                             glyph.width, glyph.height, glyph.coverage, rgb, bounds);
        x += glyph.advance;
        lx_host_glyph_free(&glyph);
        if (!ok)
            return 0;
    }
    ps->position = (POINTL){(LONG)x, point->y};
    return 1;
}
LONG WinDrawText(HPS handle, LONG count, PCH text, PRECTL rect, LONG foreground, LONG background,
                 ULONG flags)
{
    TRACE_NATIVE("WinDrawText(%u, %d, %p, %p, %d, %d, %u)", (uint)handle, (int)count, text, rect,
                 (int)foreground, (int)background, (uint)flags);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect || count < -1 || (!text && count))
        return 0;
    if (count == -1)
        count = (LONG)strlen(text);
    if (count > 65536)
        return 0;
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
        return 0;
    char *copy = malloc((size_t)count + 1);
    LONG *starts = malloc(((size_t)count + 2) * sizeof *starts);
    LONG *widths = malloc(((size_t)count + 2) * sizeof *widths);
    LONG *lengths = malloc(((size_t)count + 2) * sizeof *lengths);
    if (!copy || !starts || !widths || !lengths)
    {
        free(copy);
        free(starts);
        free(widths);
        free(lengths);
        return 0;
    }
    LONG used = 0;
    for (LONG i = 0; i < count; i++)
    {
        if ((flags & 0x2000) && text[i] == '~' && i + 1 < count)
        {
            if (text[i + 1] != '~')
                continue;
            i++;
        }
        copy[used++] = text[i];
    }
    copy[used] = 0;
    LONG lines = 0, start = 0, width = 0, last_space = -1, space_width = 0;
    starts[0] = 0;
    LONG available = rect->xRight - rect->xLeft;
    for (LONG i = 0; i < used; i++)
    {
        int advance = lx_host_font_advance(font, lx_pm_codepoint((uint8)copy[i]));
        if (advance < 0)
            advance = 0;
        if (copy[i] == ' ')
        {
            last_space = i;
            space_width = width;
        }
        if (copy[i] == '\n' || ((flags & 0x4000) && width + advance > available && i > start))
        {
            LONG end = i;
            if (copy[i] != '\n' && last_space >= start)
            {
                end = last_space;
                width = space_width;
            }
            lengths[lines] = end - start;
            widths[lines++] = width;
            start = end == i && copy[i] != '\n' ? end : end + 1;
            starts[lines] = start;
            i = start - 1;
            width = 0;
            last_space = -1;
        }
        else if (copy[i] != '\r')
            width += advance;
    }
    lengths[lines] = used - start;
    widths[lines++] = width;
    LONG height = metrics.ascent + metrics.descent + ((flags & 0x80) ? metrics.leading : 0);
    LONG total_height = height * lines, max_width = 0;
    for (LONG i = 0; i < lines; i++)
        if (widths[i] > max_width)
            max_width = widths[i];
    LONG top = rect->yTop;
    if (flags & 0x400)
        top = rect->yBottom + (rect->yTop - rect->yBottom + total_height) / 2;
    else if (flags & 0x800)
        top = rect->yBottom + total_height;
    LONG result = count;
    if (flags & 2)
    {
        rect->xRight = rect->xLeft + max_width;
        rect->yBottom = rect->yTop - total_height;
    }
    else
    {
        if ((flags & 0x8000) && !WinFillRect(handle, rect, background))
            result = 0;
        for (LONG line = 0; result && line < lines; line++)
        {
            LONG x = rect->xLeft;
            if (flags & 0x100)
                x += (available - widths[line]) / 2;
            else if (flags & 0x200)
                x += available - widths[line];
            POINTL point = {x, top - line * height - metrics.ascent};
            if (!lx_pm_text(handle, &point, lengths[line], copy + starts[line],
                            (flags & 0x40) ? ps->primitive_color[1] : foreground, rect))
                result = 0;
        }
    }
    free(widths);
    free(lengths);
    free(starts);
    free(copy);
    return result;
}

BOOL lx_pm_mask(HPS handle, LONG x, LONG y, int width, int height, const uint8_t *coverage,
                LONG rgb, const RECTL *bounds)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || width < 0 || height < 0 || (width && height && !coverage) || rgb < 0)
        return FALSE;
    if (!width || !height)
        return TRUE;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    if (bitmap)
    {
        for (int row = 0; row < height; row++)
            for (int col = 0; col < width; col++)
            {
                int64_t dx = (int64_t)x + col, dy = (int64_t)y + row;
                if (dx < 0 || dy < 0 || dx >= bitmap->width || dy >= bitmap->height ||
                    !lx_pm_clip_point(handle, (LONG)dx, (LONG)dy))
                    continue;
                POINTL point = {(LONG)dx, (LONG)dy};
                if (bounds && !WinPtInRect(0, (PRECTL)bounds, &point))
                    continue;
                unsigned alpha = coverage[(size_t)(height - 1 - row) * width + col];
                uint32_t old = bitmap->pixels[(size_t)dy * bitmap->width + dx], value = 0;
                for (unsigned shift = 0; shift < 24; shift += 8)
                    value |= ((((ULONG)rgb >> shift & 255) * alpha +
                               (old >> shift & 255) * (255 - alpha) + 127) /
                              255)
                             << shift;
                bitmap->pixels[(size_t)dy * bitmap->width + dx] = value;
            }
        return TRUE;
    }
    Window *window = ps->window;
    if (!window || !window->heavy.window)
        return FALSE;
    LxHostRect origin;
    calcHeavyweightRect(window, &origin);
    RECTL drawing = {0, 0, window->w, window->h};
    if (bounds && !WinIntersectRect(0, &drawing, &drawing, (PRECTL)bounds))
        return TRUE;
    PmRegion visible = {0};
    if (!lx_pm_window_clip(handle, &drawing, &visible))
        return FALSE;
    BOOL result = TRUE;
    for (size_t i = 0; result && i < visible.count; i++)
    {
        RECTL part = visible.rects[i];
        LxHostRect area = {origin.x + part.xLeft, origin.y + origin.h - part.yTop,
                           part.xRight - part.xLeft, part.yTop - part.yBottom};
        result = lx_host_window_mask(window->heavy.window, &area, origin.x + x,
                                     origin.y + origin.h - y - height, width, height, coverage,
                                     (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    }
    free(visible.rects);
    return result;
}
