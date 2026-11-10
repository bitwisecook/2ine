/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 text uses measured PM glyphs and Windows top-left rectangles. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/common/pm_text.h"
#include "host/font.h"
#include <math.h>

enum
{
    /* The longest string one call draws or measures. */
    TEXT_LENGTH_MAX = 65536,
    /* Guests write Windows Latin 1 (OS/2 code page 1004); the PM font tables use 437. */
    GUEST_CODEPAGE = 1004,
    FONT_CODEPAGE = 437,
    /* DT_EXPANDTABS stops every 8 characters, so one character expands to at most 8. */
    TAB_COLUMNS = 8,
    LONG_BYTES = 4,
    /* The TA_ horizontal (left, right, centre) and vertical (top, bottom, baseline) fields. */
    TA_HORIZONTAL_MASK = WIN32_TA_CENTER,
    TA_VERTICAL_MASK = WIN32_TA_BASELINE,
    /* Mapping scales are measured on a vector this long to keep rounding small. */
    SCALE_PROBE_LENGTH = 1024,
    COLORREF_RGB_MASK = 0xFFFFFF
};

/* DrawText refuses flags from 0x8000 up. */
static const ULONG DRAWTEXT_UNSUPPORTED_FLAGS = 0xFFFF8000u;

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

/* COLORREF is 0x00BBGGRR; PM takes 0x00RRGGBB. */
static LONG rgb_color(ULONG color)
{
    return (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
}

static char *text_copy(const char *text, LONG count, BOOL format, ULONG flags, LONG *length)
{
    if (count < -1 || count > TEXT_LENGTH_MAX || !text)
        return NULL;
    if (count == -1)
        count = (LONG)strlen(text);
    if (count > TEXT_LENGTH_MAX)
        return NULL;
    char *copy = malloc((size_t)count * TAB_COLUMNS + 1);
    if (!copy)
        return NULL;
    LONG used = 0, column = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint8 c = (uint8)text[i];
        if (format && !(flags & WIN32_DT_NOPREFIX) && c == '&')
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
        if (format && c == '\n' && (flags & WIN32_DT_SINGLELINE))
            c = ' ';
        if (format && c == '\t' && (flags & WIN32_DT_EXPANDTABS))
        {
            unsigned spaces = TAB_COLUMNS - (unsigned)column % TAB_COLUMNS;
            while (spaces--)
            {
                copy[used++] = ' ';
                column++;
            }
            continue;
        }
        copy[used++] = (char)WinCpTranslateChar(0, GUEST_CODEPAGE, c, FONT_CODEPAGE);
        column = c == '\n' ? 0 : column + 1;
    }
    copy[used] = 0;
    *length = used;
    return copy;
}

/* A metafile holds formatted text as one positioned, clipped record per line of the same layout
   the display uses, with the characters returned to the guest's code page. */
typedef struct TextRecording
{
    ULONG handle;
    LONG height, ascent;
    const uint8 *clip;
    ULONG options;
} TextRecording;

static void record_line(void *context, LONG x, LONG baseline, const char *line, LONG length)
{
    TextRecording *recording = context;
    char *ansi = malloc(length ? (size_t)length : 1);
    if (!ansi)
        return;
    for (LONG i = 0; i < length; i++)
        ansi[i] = (char)WinCpTranslateChar(0, FONT_CODEPAGE, (uint8)line[i], GUEST_CODEPAGE);
    open32MetafileExtText(recording->handle, x, recording->height - baseline - recording->ascent,
                          recording->options, recording->options ? (PVOID)recording->clip : NULL,
                          ansi, (ULONG)length, NULL);
    free(ansi);
}

LONG DrawText(ULONG handle, PSZ text, LONG count, PVOID rectangle, ULONG flags)
{
    TRACE_NATIVE("DrawText(%u, '%.*s', %d, %p, %X)", handle, count >= 0 ? count : 80, text, count,
                 rectangle, flags);
    Open32DC *dc = open32FindDC(handle);
    if (!dc || (!open32IdentityMapping(dc) && !(flags & WIN32_DT_CALCRECT)) ||
        (!open32CanDraw(handle) && !(flags & WIN32_DT_CALCRECT)) || !rectangle || !text ||
        count < -1 || (flags & DRAWTEXT_UNSUPPORTED_FLAGS))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const uint8 *wire = rectangle;
    LONG left = (LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
         top = (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP);
    LONG right = (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
         bottom = (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM);
    int64_t y_top = (int64_t)open32DCHeight(handle) - top,
            y_bottom = (int64_t)open32DCHeight(handle) - bottom;
    if (right < left || bottom < top || (int64_t)right - left > INT32_MAX ||
        (int64_t)bottom - top > INT32_MAX || y_top < INT32_MIN || y_top > INT32_MAX ||
        y_bottom < INT32_MIN || y_bottom > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG length;
    char *copy = text_copy(text, count, TRUE, flags, &length);
    if (!copy)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG pm_flags =
        (flags & WIN32_DT_CENTER ? DT_CENTER : 0) | (flags & WIN32_DT_RIGHT ? DT_RIGHT : 0) |
        (flags & WIN32_DT_VCENTER ? DT_VCENTER : 0) | (flags & WIN32_DT_BOTTOM ? DT_BOTTOM : 0) |
        ((flags & WIN32_DT_WORDBREAK) && !(flags & WIN32_DT_SINGLELINE) ? DT_WORDBREAK : 0) |
        (flags & WIN32_DT_EXTERNALLEADING ? DT_EXTERNALLEADING : 0);
    RECTL rect = {left, (LONG)y_bottom, right, (LONG)y_top}, measured = rect;
    LxHostFontMetrics line_metrics;
    if (!(flags & WIN32_DT_CALCRECT) && open32IsMetafile(handle) && length &&
        lx_host_font_metrics(lx_pm_font(handle), &line_metrics))
    {
        TextRecording recording = {handle, open32DCHeight(handle), line_metrics.ascent, wire,
                                   flags & WIN32_DT_NOCLIP ? 0 : WIN32_ETO_CLIPPED};
        /* Each line is recorded already positioned, so alignment is reset around them. */
        const LONG none = 0, restored = (LONG)dc->text_alignment;
        RECTL layout = rect;
        open32MetafileWords(handle, WIN32_META_SETTEXTALIGN, 1, &none);
        lx_pm_draw_text_lines(handle, length, copy, &layout, pm_flags, record_line, &recording);
        open32MetafileWords(handle, WIN32_META_SETTEXTALIGN, 1, &restored);
    }
    LONG foreground = rgb_color(dc->text_color), background = rgb_color(dc->background_color);
    LONG result = 0;
    if (length && lx_pm_draw_text(handle, length, copy, &measured, foreground, background,
                                  pm_flags | DT_QUERYEXTENT, TRUE, FALSE))
    {
        result = measured.yTop - measured.yBottom;
        if (flags & WIN32_DT_CALCRECT)
        {
            lx_guest_store_u32((uint8 *)rectangle + WIN32_RECT_RIGHT, (ULONG)measured.xRight);
            lx_guest_store_u32((uint8 *)rectangle + WIN32_RECT_BOTTOM, (ULONG)(top + result));
        }
        else if (!lx_pm_draw_text(handle, length, copy, &rect, foreground, background, pm_flags,
                                  !(flags & WIN32_DT_NOCLIP), dc->background_mode == OPAQUE))
            result = 0;
    }
    free(copy);
    return result;
}

BOOL TextOut(ULONG handle, LONG x, LONG y, PSZ text, LONG count)
{
    if (count < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
    if (!dc || !output || count < 0 || count > TEXT_LENGTH_MAX || (count && !text) ||
        (fit && maximum < 0))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(font, &metrics))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    int64_t width = 0;
    ULONG fitted = 0, spaces = 0;
    for (LONG i = 0; i < count; i++)
    {
        uint8 c = WinCpTranslateChar(0, GUEST_CODEPAGE, (uint8)text[i], FONT_CODEPAGE);
        int advance = text_step(dc, font, c, spaces);
        if (c == ' ')
            spaces++;
        if (advance == INT32_MIN)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        width += advance;
        if (width < INT32_MIN || width > INT32_MAX)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (width <= maximum)
            fitted = (ULONG)i + 1;
        if (advances)
            lx_guest_store_u32((uint8 *)advances + i * LONG_BYTES, (ULONG)width);
    }
    if (fit)
        lx_guest_store_u32(fit, fitted);
    lx_guest_store_u32((uint8 *)output + WIN32_SIZE_CX, (ULONG)width);
    lx_guest_store_u32((uint8 *)output + WIN32_SIZE_CY,
                       count ? (ULONG)(metrics.ascent + metrics.descent) : 0);
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
    if (!open32FindDC(handle) || count < 0 || count > TEXT_LENGTH_MAX || (count && !text) ||
        tab_count < 0 || tab_count > TEXT_LENGTH_MAX || (tab_count && !tabs) ||
        !lx_host_font_metrics(lx_pm_font(handle), &metrics))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG last = 0;
    for (LONG i = 0; i < tab_count; i++)
    {
        LONG next = (LONG)lx_guest_load_u32((uint8 *)tabs + i * LONG_BYTES);
        if (next <= last)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        last = next;
    }
    /* One tab stop sets the interval; otherwise stops fall every eight average characters. */
    int64_t interval = tab_count == 1 ? last : (int64_t)metrics.average_width * TAB_COLUMNS;
    if (interval <= 0)
        interval = TAB_COLUMNS;
    int64_t width = 0;
    LONG next_tab = 0;
    LxHostFont *font = lx_pm_font(handle);
    for (LONG i = 0; i < count; i++)
    {
        if (text[i] == '\t')
        {
            while (next_tab < tab_count &&
                   (LONG)lx_guest_load_u32((uint8 *)tabs + next_tab * LONG_BYTES) <= width)
                next_tab++;
            width = tab_count > 1 && next_tab < tab_count
                        ? lx_guest_load_u32((uint8 *)tabs + next_tab++ * LONG_BYTES)
                        : (width / interval + 1) * interval;
        }
        else
        {
            uint8 c = WinCpTranslateChar(0, GUEST_CODEPAGE, (uint8)text[i], FONT_CODEPAGE);
            int advance = lx_host_font_advance(font, lx_pm_codepoint(c));
            if (advance < 0)
            {
                open32SetError(ERROR_INVALID_PARAMETER);
                return 0;
            }
            width += advance;
        }
        if (width > INT32_MAX)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
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
    if (!dc || !open32CanDraw(handle) || count > TEXT_LENGTH_MAX || (count && !text) ||
        ((flags & (WIN32_ETO_OPAQUE | WIN32_ETO_CLIPPED)) && !rectangle))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & ~(unsigned)(WIN32_ETO_OPAQUE | WIN32_ETO_CLIPPED))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    RECTL area = {0};
    LONG height = open32DCHeight(handle);
    if (flags & (WIN32_ETO_OPAQUE | WIN32_ETO_CLIPPED))
    {
        const uint8 *wire = rectangle;
        LONG left = (LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
             top = (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP);
        LONG right = (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
             bottom = (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM);
        if (right < left || bottom < top || !open32MapPoint(dc, &left, &top, TRUE) ||
            !open32MapPoint(dc, &right, &bottom, TRUE))
        {
            open32SetError(ERROR_INVALID_PARAMETER);
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
            open32SetError(ERROR_INVALID_PARAMETER);
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    int64_t advance = 0;
    ULONG spaces = 0;
    for (LONG i = 0; i < length; i++)
    {
        advance += advances ? (LONG)lx_guest_load_u32((const uint8 *)advances + i * LONG_BYTES)
                            : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
    }
    LONG reference_x = dc->text_alignment & WIN32_TA_UPDATECP ? dc->position_x : x;
    LONG reference_y = dc->text_alignment & WIN32_TA_UPDATECP ? dc->position_y : y;
    LONG device_x = reference_x, device_y = reference_y;
    if (!open32MapPoint(dc, &device_x, &device_y, TRUE))
    {
        free(copy);
        return FALSE;
    }
    ULONG horizontal = dc->text_alignment & TA_HORIZONTAL_MASK,
          vertical = dc->text_alignment & TA_VERTICAL_MASK;
    int64_t start = (int64_t)device_x - (horizontal == WIN32_TA_RIGHT    ? advance
                                         : horizontal == WIN32_TA_CENTER ? advance / 2
                                                                         : 0);
    int64_t baseline = (int64_t)height - device_y +
                       (vertical == WIN32_TA_BASELINE ? 0
                        : vertical == WIN32_TA_BOTTOM ? metrics.descent
                                                      : -metrics.ascent);
    if (advance < INT32_MIN || advance > INT32_MAX || start < INT32_MIN || start > INT32_MAX ||
        ((dc->text_alignment & WIN32_TA_UPDATECP) &&
         ((int64_t)device_x + advance < INT32_MIN || (int64_t)device_x + advance > INT32_MAX)))
    {
        free(copy);
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    x = (LONG)start;
    int64_t position = start, leftmost = start, rightmost = start;
    BOOL valid = baseline - metrics.descent >= INT32_MIN && baseline + metrics.ascent <= INT32_MAX;
    spaces = 0;
    for (LONG i = 0; valid && i < length; i++)
    {
        int glyph = lx_host_font_advance(font, lx_pm_codepoint((uint8)copy[i]));
        int64_t step = advances ? (LONG)lx_guest_load_u32((const uint8 *)advances + i * LONG_BYTES)
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL result = TRUE;
    if (flags & WIN32_ETO_OPAQUE)
        result = WinFillRect(handle, &area, rgb_color(dc->background_color));
    else if (count && dc->background_mode == OPAQUE)
    {
        RECTL background = {(LONG)leftmost, (LONG)baseline - metrics.descent, (LONG)rightmost,
                            (LONG)baseline + metrics.ascent};
        if (!(flags & WIN32_ETO_CLIPPED) || WinIntersectRect(0, &background, &background, &area))
            result = WinFillRect(handle, &background, rgb_color(dc->background_color));
    }
    spaces = 0;
    POINTL point = {x, (LONG)baseline};
    for (LONG i = 0; result && i < length; i++)
    {
        result = lx_pm_text(handle, &point, 1, copy + i, rgb_color(dc->text_color),
                            (flags & WIN32_ETO_CLIPPED) ? &area : NULL);
        LONG step = advances ? (LONG)lx_guest_load_u32((const uint8 *)advances + i * LONG_BYTES)
                             : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
        point.x = (LONG)((int64_t)point.x + step);
    }
    if (result && (dc->text_alignment & WIN32_TA_UPDATECP))
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

/* Logical font heights scale with the mapping, so text is drawn with a font realized at the
   device size and explicit advances scale by the same horizontal factor. */
/* Under rotation or shear each glyph renders through the transform's direction, along a baseline
   that runs the same way. The font is already realized at the mapped size. */
static BOOL rotated_text(ULONG handle, Open32DC *dc, LONG x, LONG y, ULONG flags, PVOID rectangle,
                         PCHAR text, ULONG count, const uint8 *advances)
{
    LONG reference_x = dc->text_alignment & WIN32_TA_UPDATECP ? dc->position_x : x;
    LONG reference_y = dc->text_alignment & WIN32_TA_UPDATECP ? dc->position_y : y;
    LONG ox = 0, oy = 0, rx = SCALE_PROBE_LENGTH, ry = 0, ux = 0, uy = SCALE_PROBE_LENGTH,
         px = reference_x, py = reference_y;
    LxHostFont *font = lx_pm_font(handle);
    LxHostFontMetrics metrics;
    if (!open32MapPoint(dc, &ox, &oy, TRUE) || !open32MapPoint(dc, &rx, &ry, TRUE) ||
        !open32MapPoint(dc, &ux, &uy, TRUE) || !open32MapPoint(dc, &px, &py, TRUE) ||
        !lx_host_font_metrics(font, &metrics))
        return FALSE;
    double ax = (double)rx - ox, ay = (double)ry - oy, bx = (double)ux - ox, by = (double)uy - oy;
    double across = hypot(ax, ay), down = hypot(bx, by);
    if (!across || !down)
        return TRUE;
    ax /= across;
    ay /= across;
    bx /= down;
    by /= down;
    /* y-up glyph space into y-up device space: x along the baseline, y against logical down. */
    const double matrix[4] = {ax, -bx, -ay, by};
    LONG length = 0;
    char *copy = count ? text_copy(text, (LONG)count, FALSE, 0, &length) : NULL;
    if (count && !copy)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    double total = 0;
    ULONG spaces = 0;
    for (LONG i = 0; i < length; i++)
    {
        total += advances ? (LONG)lx_guest_load_u32(advances + i * LONG_BYTES)
                          : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
    }
    ULONG horizontal = dc->text_alignment & TA_HORIZONTAL_MASK,
          vertical = dc->text_alignment & TA_VERTICAL_MASK;
    double along = horizontal == WIN32_TA_RIGHT    ? -total
                   : horizontal == WIN32_TA_CENTER ? -total / 2
                                                   : 0;
    double rise = vertical == WIN32_TA_BASELINE ? 0
                  : vertical == WIN32_TA_BOTTOM ? -metrics.descent
                                                : metrics.ascent;
    double pen_x = px + ax * along + bx * rise, pen_y = py + ay * along + by * rise;
    LONG height = open32DCHeight(handle);
    if ((flags & WIN32_ETO_OPAQUE) || dc->background_mode == OPAQUE)
    {
        ULONG brush = CreateSolidBrush(dc->background_color & COLORREF_RGB_MASK);
        BOOL filled = brush != 0;
        if (brush && (flags & WIN32_ETO_OPAQUE))
        {
            const uint8 *wire = rectangle;
            filled = open32RotatedFill(handle, (LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
                                       (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP),
                                       (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
                                       (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM), brush);
        }
        else if (brush && length)
        {
            /* The cell's four corners, carried back into logical space for the fill. */
            uint8 cell[4 * WIN32_POINT_SIZE];
            const double corners[4][2] = {
                {pen_x - bx * metrics.ascent, pen_y - by * metrics.ascent},
                {pen_x + ax * total - bx * metrics.ascent,
                 pen_y + ay * total - by * metrics.ascent},
                {pen_x + ax * total + bx * metrics.descent,
                 pen_y + ay * total + by * metrics.descent},
                {pen_x + bx * metrics.descent, pen_y + by * metrics.descent}};
            for (unsigned i = 0; i < 4 && filled; i++)
            {
                LONG lx = (LONG)lround(corners[i][0]), ly = (LONG)lround(corners[i][1]);
                filled = open32MapPoint(dc, &lx, &ly, FALSE);
                lx_guest_store_u32(cell + i * WIN32_POINT_SIZE + WIN32_POINT_X, (ULONG)lx);
                lx_guest_store_u32(cell + i * WIN32_POINT_SIZE + WIN32_POINT_Y, (ULONG)ly);
            }
            ULONG old_brush = open32SelectBrush(dc, brush),
                  old_pen = open32SelectPen(dc, GetStockObject(NULL_PEN));
            filled = filled && Polygon(handle, cell, 4);
            open32SelectPen(dc, old_pen);
            open32SelectBrush(dc, old_brush);
        }
        if (brush)
            DeleteObject(brush);
        if (!filled)
        {
            free(copy);
            return FALSE;
        }
    }
    LONG saved = 0;
    if (flags & WIN32_ETO_CLIPPED)
    {
        /* The clip rectangle turns with the text, so it selects as a polygon in device space. */
        const uint8 *wire = rectangle;
        uint8 corners[4 * WIN32_POINT_SIZE];
        const LONG logical[8] = {(LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_TOP),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_RIGHT),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_LEFT),
                                 (LONG)lx_guest_load_u32(wire + WIN32_RECT_BOTTOM)};
        BOOL mapped = TRUE;
        for (unsigned i = 0; i < 4; i++)
        {
            LONG cx = logical[i * 2], cy = logical[i * 2 + 1];
            mapped = mapped && open32MapPoint(dc, &cx, &cy, TRUE);
            lx_guest_store_u32(corners + i * WIN32_POINT_SIZE + WIN32_POINT_X, (ULONG)cx);
            lx_guest_store_u32(corners + i * WIN32_POINT_SIZE + WIN32_POINT_Y, (ULONG)cy);
        }
        ULONG region = mapped ? CreatePolygonRgn(corners, 4, ALTERNATE) : 0;
        saved = region ? SaveDC(handle) : 0;
        if (saved)
            ExtSelectClipRgn(handle, region, WIN32_RGN_AND);
        if (region)
            DeleteObject(region);
        if (!saved)
        {
            free(copy);
            return FALSE;
        }
    }
    BOOL result = TRUE;
    spaces = 0;
    for (LONG i = 0; result && i < length; i++)
    {
        LxHostGlyph glyph;
        LONG step = advances ? (LONG)lx_guest_load_u32(advances + i * LONG_BYTES)
                             : text_step(dc, font, (uint8)copy[i], spaces);
        if (copy[i] == ' ')
            spaces++;
        result = lx_host_font_glyph_matrix(font, lx_pm_codepoint((uint8)copy[i]), matrix, &glyph);
        if (result)
        {
            LONG gx = (LONG)lround(pen_x), gy = height - (LONG)lround(pen_y);
            result = lx_pm_mask(handle, gx + glyph.left, gy + glyph.top - glyph.height, glyph.width,
                                glyph.height, glyph.coverage, rgb_color(dc->text_color), NULL);
            lx_host_glyph_free(&glyph);
        }
        pen_x += ax * step;
        pen_y += ay * step;
    }
    free(copy);
    if (saved)
        RestoreDC(handle, saved);
    if (result && (dc->text_alignment & WIN32_TA_UPDATECP))
    {
        LONG next_x = (LONG)lround(px + ax * total), next_y = (LONG)lround(py + ay * total);
        if (open32MapPoint(dc, &next_x, &next_y, FALSE))
        {
            dc->position_x = next_x;
            dc->position_y = next_y;
        }
    }
    return result;
}

static double axis_scale(Open32DC *dc, LONG dx, LONG dy)
{
    LONG ax = 0, ay = 0, bx = dx, by = dy;
    if (!open32MapPoint(dc, &ax, &ay, TRUE) || !open32MapPoint(dc, &bx, &by, TRUE))
        return 1;
    return hypot((double)bx - ax, (double)by - ay) / SCALE_PROBE_LENGTH;
}

BOOL ExtTextOut(ULONG handle, LONG x, LONG y, ULONG flags, PVOID rectangle, PCHAR text, ULONG count,
                PLONG advances)
{
    Open32DC *dc = open32FindDC(handle);
    BOOL result;
    if (!dc || count > TEXT_LENGTH_MAX || open32IdentityMapping(dc))
        result = text_out(handle, x, y, flags, rectangle, text, count, advances);
    else
    {
        double scale_x = axis_scale(dc, SCALE_PROBE_LENGTH, 0),
               scale_y = axis_scale(dc, 0, SCALE_PROBE_LENGTH);
        uint8 *scaled = advances && count ? malloc((size_t)count * LONG_BYTES) : NULL;
        if (advances && count && !scaled)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        for (ULONG i = 0; scaled && i < count; i++)
            lx_guest_store_u32(
                scaled + i * LONG_BYTES,
                (ULONG)(LONG)lround(
                    (LONG)lx_guest_load_u32((const uint8 *)advances + i * LONG_BYTES) * scale_x));
        uint8 logfont[WIN32_LOGFONT_SIZE];
        LxHostFontMetrics metrics;
        ULONG previous = dc->font_handle, realized = 0;
        if (open32FontObject(previous, sizeof logfont, logfont) == WIN32_LOGFONT_SIZE &&
            lx_host_font_metrics(lx_pm_font(handle), &metrics))
        {
            LONG height = (LONG)lx_guest_load_u32(logfont + WIN32_LOGFONT_HEIGHT),
                 width = (LONG)lx_guest_load_u32(logfont + WIN32_LOGFONT_WIDTH);
            lx_guest_store_u32(logfont + WIN32_LOGFONT_HEIGHT,
                               (ULONG)(LONG)lround((height ? height : -metrics.em) * scale_y));
            lx_guest_store_u32(logfont + WIN32_LOGFONT_WIDTH, (ULONG)(LONG)lround(width * scale_x));
            realized = CreateFontIndirect(logfont);
            if (realized && !open32SelectFont(dc, realized))
            {
                DeleteObject(realized);
                realized = 0;
            }
        }
        const uint8 *widths = scaled ? scaled : (const uint8 *)advances;
        result = open32Rotated(dc)
                     ? rotated_text(handle, dc, x, y, flags, rectangle, text, count, widths)
                     : text_out(handle, x, y, flags, rectangle, text, count, (PLONG)widths);
        if (realized)
        {
            open32SelectFont(dc, previous);
            DeleteObject(realized);
        }
        free(scaled);
    }
    if (result)
        open32MetafileExtText(handle, x, y, flags, rectangle, text, count, advances);
    return result;
}

ULONG SetTextAlign(ULONG handle, ULONG alignment)
{
    Open32DC *dc = open32FindDC(handle);
    /* 4 and 16 alone are not TA_ alignments; only 2 and 6, 8 and 24 are. */
    if (!dc || (alignment & ~(unsigned)WIN32_TA_MASK) || (alignment & TA_HORIZONTAL_MASK) == 4 ||
        (alignment & TA_VERTICAL_MASK) == 16)
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    if ((alignment & WIN32_TA_UPDATECP) && (alignment & TA_HORIZONTAL_MASK))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return UINT32_MAX;
    }
    ULONG previous = dc->text_alignment;
    dc->text_alignment = alignment;
    const LONG recorded = (LONG)alignment;
    open32MetafileWords(handle, WIN32_META_SETTEXTALIGN, 1, &recorded);
    return previous;
}

ULONG GetTextAlign(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
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
    if (!dc || count < 0 || count > TEXT_LENGTH_MAX || (count && !text) || tab_count < 0 ||
        tab_count > TEXT_LENGTH_MAX || !lx_host_font_metrics(lx_pm_font(h), &metrics))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG last = 0;
    for (LONG i = 0; i < tab_count; i++)
    {
        LONG stop = (LONG)lx_guest_load_u32((uint8 *)tabs + i * LONG_BYTES);
        if (stop <= last)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        last = stop;
    }
    int64_t position = x,
            interval = tab_count == 1 ? last : (int64_t)metrics.average_width * TAB_COLUMNS;
    if (interval < 1)
        interval = TAB_COLUMNS;
    LONG start = 0;
    ULONG alignment = dc->text_alignment;
    dc->text_alignment = 0;
    BOOL ok = TRUE;
    for (LONG i = 0; ok && i <= count; i++)
        if (i == count || text[i] == '\t')
        {
            uint8 size[WIN32_SIZE_SIZE] = {0};
            if (i > start)
            {
                ok = GetTextExtentPoint(h, text + start, i - start, size) &&
                     position >= INT32_MIN && position <= INT32_MAX &&
                     TextOut(h, (LONG)position, y, text + start, i - start);
                if (ok)
                    position += (LONG)lx_guest_load_u32(size + WIN32_SIZE_CX);
            }
            if (i < count)
            {
                int64_t relative = position - origin, next = (relative / interval + 1) * interval;
                BOOL found = FALSE;
                if (tab_count > 1)
                    for (LONG t = 0; t < tab_count; t++)
                    {
                        LONG stop = (LONG)lx_guest_load_u32((uint8 *)tabs + t * LONG_BYTES);
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (LONG)(((ULONG)(position - x) & 0xffff) |
                  ((ULONG)(metrics.ascent + metrics.descent) & 0xffff) << 16);
}
