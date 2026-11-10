/* Public domain. No rights reserved. Wholly AI-authored. */
/* Byte strings use the PS codepage and the selected measured outline face. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/common/pm_text.h"

enum
{
    /* FIXED: a 16.16 fixed-point number. */
    FIXED_ONE = 65536,
    /* Longest string drawn in one call. */
    TEXT_MAX_LENGTH = 65536,
    /* The PM default font is 16 pels high on a 96 DPI display. */
    DEFAULT_FONT_PELS = 16,
    REFERENCE_DPI = 96,
    POINTS_PER_INCH = 72,
    MAX_FONT_POINTS = 384
};

/* Host font catalogue slots: a family's first slot plus its style bits. */
enum
{
    HOST_FONT_SANS = 0,
    HOST_FONT_SERIF = 4,
    HOST_FONT_MONOSPACED = 8,
    HOST_FONT_BOLD = 1,
    HOST_FONT_ITALIC = 2
};

LONG lx_pm_char_extra(PresentationSpace *ps)
{
    if (!ps || !ps->gpi.initialized || !ps->gpi.char_extra)
        return 0;
    /* Transforming the raw 16.16 value keeps fractional world spacing through scaling. */
    POINTL origin = {0, 0}, extra = {ps->gpi.char_extra, 0};
    if (!lx_pm_transform_point(ps, &origin, TRUE) || !lx_pm_transform_point(ps, &extra, TRUE))
        return 0;
    int64_t delta = (int64_t)extra.x - origin.x;
    return (LONG)((delta + (delta < 0 ? -FIXED_ONE / 2 : FIXED_ONE / 2)) / FIXED_ONE);
}

LONG lx_pm_text(HPS handle, const POINTL *point, LONG count, const char *text, LONG color,
                const RECTL *bounds)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point || count < 0 || count > TEXT_MAX_LENGTH || (count && !text))
        return 0;
    LxHostFont *font = lx_pm_font(handle);
    LONG rgb = lx_pm_resolve_color(handle, color);
    if (!font || rgb < 0)
        return 0;
    POINTL origin = *point;
    if (!lx_pm_transform_point(ps, &origin, TRUE))
        return 0;
    RECTL device_bounds;
    if (bounds)
    {
        POINTL corners[2] = {{bounds->xLeft, bounds->yBottom}, {bounds->xRight, bounds->yTop}};
        if (!lx_pm_transform_point(ps, &corners[0], TRUE) ||
            !lx_pm_transform_point(ps, &corners[1], TRUE))
            return 0;
        device_bounds = (RECTL){corners[0].x, corners[0].y, corners[1].x, corners[1].y};
        bounds = &device_bounds;
    }
    int64_t x = origin.x;
    LONG extra = lx_pm_char_extra(ps);
    for (LONG i = 0; i < count; i++)
    {
        LxHostGlyph glyph;
        if (!lx_host_font_glyph(font, lx_pm_codepoint((uint8)text[i]), &glyph))
            return 0;
        BOOL ok = x >= INT32_MIN && x <= INT32_MAX &&
                  lx_pm_mask(handle, (LONG)x + glyph.left, origin.y + glyph.top - glyph.height,
                             glyph.width, glyph.height, glyph.coverage, rgb, bounds);
        x += glyph.advance;
        x += extra;
        lx_host_glyph_free(&glyph);
        if (!ok)
            return 0;
    }
    if (x < INT32_MIN || x > INT32_MAX)
        return 0;
    POINTL end = {(LONG)x, origin.y};
    if (!lx_pm_transform_point(ps, &end, FALSE))
        return 0;
    ps->position = end;
    return lx_pm_hits(ps);
}

static LONG draw_text(HPS handle, LONG count, PCH text, PRECTL rect, LONG foreground,
                      LONG background, ULONG flags, BOOL clip, BOOL opaque_runs, LxPmTextLine sink,
                      void *context)
{
    TRACE_NATIVE("WinDrawText(%u, %d, %p, %p, %d, %d, %u)", (uint)handle, (int)count, text, rect,
                 (int)foreground, (int)background, (uint)flags);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect || count < -1 || (!text && count))
        return 0;
    if (count == -1)
        count = (LONG)strlen(text);
    if (count > TEXT_MAX_LENGTH)
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
        if ((flags & DT_MNEMONIC) && text[i] == '~' && i + 1 < count)
        {
            if (text[i + 1] != '~')
                continue;
            i++;
        }
        copy[used++] = text[i];
    }
    copy[used] = 0;
    LONG lines = 0, start = 0, width = 0, last_space = -1, space_width = 0;
    LONG extra = lx_pm_char_extra(ps);
    starts[0] = 0;
    LONG available = rect->xRight - rect->xLeft;
    for (LONG i = 0; i < used; i++)
    {
        int advance = lx_host_font_advance(font, lx_pm_codepoint((uint8)copy[i]));
        if (advance < 0)
            advance = 0;
        advance += extra;
        if (copy[i] == ' ')
        {
            last_space = i;
            space_width = width;
        }
        if (copy[i] == '\n' || ((flags & DT_WORDBREAK) && width + advance > available && i > start))
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
    LONG height =
        metrics.ascent + metrics.descent + ((flags & DT_EXTERNALLEADING) ? metrics.leading : 0);
    LONG total_height = height * lines, max_width = 0;
    for (LONG i = 0; i < lines; i++)
        if (widths[i] > max_width)
            max_width = widths[i];
    LONG top = rect->yTop;
    if (flags & DT_VCENTER)
        top = rect->yBottom + (rect->yTop - rect->yBottom + total_height) / 2;
    else if (flags & DT_BOTTOM)
        top = rect->yBottom + total_height;
    LONG result = count;
    if (flags & DT_QUERYEXTENT)
    {
        rect->xRight = rect->xLeft + max_width;
        rect->yBottom = rect->yTop - total_height;
    }
    else
    {
        if (!sink && (flags & DT_ERASERECT) && !WinFillRect(handle, rect, background))
            result = 0;
        for (LONG line = 0; result && line < lines; line++)
        {
            LONG x = rect->xLeft;
            if (flags & DT_CENTER)
                x += (available - widths[line]) / 2;
            else if (flags & DT_RIGHT)
                x += available - widths[line];
            if (sink)
            {
                sink(context, x, top - line * height - metrics.ascent, copy + starts[line],
                     lengths[line]);
                continue;
            }
            if (opaque_runs && widths[line])
            {
                RECTL run = {x, top - (line + 1) * height, x + widths[line], top - line * height};
                if ((!clip || WinIntersectRect(0, &run, &run, rect)) &&
                    !WinFillRect(handle, &run, background))
                    result = 0;
            }
            POINTL point = {x, top - line * height - metrics.ascent};
            if (!lx_pm_text(handle, &point, lengths[line], copy + starts[line],
                            (flags & DT_TEXTATTRS) ? ps->primitive_color[PRIM_CHAR - 1]
                                                   : foreground,
                            clip ? rect : NULL))
                result = 0;
        }
    }
    free(widths);
    free(lengths);
    free(starts);
    free(copy);
    return result;
}

LONG lx_pm_draw_text(HPS handle, LONG count, PCH text, PRECTL rect, LONG foreground,
                     LONG background, ULONG flags, BOOL clip, BOOL opaque_runs)
{
    return draw_text(handle, count, text, rect, foreground, background, flags, clip, opaque_runs,
                     NULL, NULL);
}

LONG lx_pm_draw_text_lines(HPS handle, LONG count, PCH text, PRECTL rect, ULONG flags,
                           LxPmTextLine sink, void *context)
{
    return draw_text(handle, count, text, rect, 0, 0, flags & ~(unsigned)DT_QUERYEXTENT, FALSE,
                     FALSE, sink, context);
}

LONG WinDrawText(HPS handle, LONG count, PCH text, PRECTL rect, LONG foreground, LONG background,
                 ULONG flags)
{
    return lx_pm_draw_text(handle, count, text, rect, foreground, background, flags, TRUE, FALSE);
}

BOOL lx_pm_mask(HPS handle, LONG x, LONG y, int width, int height, const uint8_t *coverage,
                LONG rgb, const RECTL *bounds)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || width < 0 || height < 0 || (width && height && !coverage) || rgb < 0)
        return FALSE;
    if (!width || !height)
        return TRUE;
    lx_pm_boundary(ps, x, y, width, height);
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

LxHostFont *lx_pm_font(HPS hps)
{
    PresentationSpace *ps = lx_pm_font_space(hps);
    if (!ps)
        return NULL;
    unsigned dpi = lx_pm_dc_dpi(ps->dc);
    unsigned index = HOST_FONT_SANS,
             pixels = (DEFAULT_FONT_PELS * dpi + REFERENCE_DPI / 2) / REFERENCE_DPI;
    char attribute[128] = {0};
    ULONG bytes = ps->window ? WinQueryPresParam(ps->window->hwnd, PP_FONTNAMESIZE, 0, NULL,
                                                 sizeof attribute - 1, attribute, 0)
                             : 0;
    if (bytes && memchr(attribute, 0, bytes))
    {
        char *end;
        unsigned long points = strtoul(attribute, &end, 10);
        if (points && points <= MAX_FONT_POINTS && *end == '.')
        {
            const char *family = end + 1;
            pixels = (unsigned)(points * dpi + POINTS_PER_INCH / 2) / POINTS_PER_INCH;
            if (!strncasecmp(family, "Courier", 7) || !strncasecmp(family, "System Monospaced", 17))
                index = HOST_FONT_MONOSPACED;
            else if (!strncasecmp(family, "Tms Rmn", 7) ||
                     !strncasecmp(family, "Times New Roman", 15))
                index = HOST_FONT_SERIF;
            unsigned style = 0;
            for (const char *p = family; *p; p++)
            {
                if (!strncasecmp(p, "Bold", 4))
                    style |= HOST_FONT_BOLD;
                if (!strncasecmp(p, "Italic", 6))
                    style |= HOST_FONT_ITALIC;
            }
            index += style;
        }
    }
    if (ps->logical_fonts[ps->charset].defined)
    {
        index = ps->logical_fonts[ps->charset].index;
        pixels = ps->logical_fonts[ps->charset].pixels;
    }
    if (ps->char_height > 0)
        pixels = (unsigned)((ps->char_height + (long long)FIXED_ONE / 2) / FIXED_ONE);
    if (!pixels)
        pixels = 1;
    if (!ps->font || ps->font_index != index || ps->font_pixels != pixels)
    {
        LxHostFont *font = lx_host_font_open(index, pixels);
        if (!font)
            return NULL;
        lx_host_font_close(ps->font);
        ps->font = font;
        ps->font_index = index;
        ps->font_pixels = pixels;
    }

    return ps->font;
}

unsigned lx_pm_font_index(HPS hps)
{
    PresentationSpace *ps = lx_pm_font_space(hps);
    return ps ? ps->logical_fonts[ps->charset].defined ? ps->logical_fonts[ps->charset].metric_index
                                                       : ps->font_index
              : 0;
}
