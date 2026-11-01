/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard scrollbar controls paint their range and notify the owning window. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
typedef struct ScrollLayout
{
    BOOL vertical;
    LONG extent, across, button, track, thumb, position;
} ScrollLayout;
static ScrollLayout layout(Open32Window *window)
{
    Open32Scroll *state = &window->scroll[2];
    BOOL vertical = (window->style & 1) != 0;
    LONG extent = vertical ? window->height : window->width;
    LONG across = vertical ? window->width : window->height;
    LONG button = across < extent / 2 ? across : extent / 2;
    LONG track = extent - button * 2;
    LONG thumb = track < across ? track : across;
    LONG position = window->scroll_dragging ? window->scroll_track_position : state->position;
    int64_t range = (int64_t)state->maximum - state->minimum;
    LONG pixel = range ? (LONG)((int64_t)(track - thumb) * (position - state->minimum) / range) : 0;
    return (ScrollLayout){vertical, extent, across, button, track, thumb, button + pixel};
}
static void fill(ULONG dc, const ScrollLayout *s, LONG a, LONG b, LONG c, LONG d, ULONG brush)
{
    uint8 rect[16];
    LONG fields[4] = {s->vertical ? c : a, s->vertical ? a : c, s->vertical ? d : b,
                      s->vertical ? b : d};
    for (unsigned i = 0; i < 4; i++)
        lx_guest_store_u32(rect + i * 4, (ULONG)fields[i]);
    FillRect(dc, rect, brush);
}
static void button(ULONG dc, const ScrollLayout *s, LONG first, LONG last, BOOL arrow, BOOL end,
                   BOOL enabled)
{
    fill(dc, s, first, last, 0, s->across, 16); /* COLOR_BTNFACE */
    fill(dc, s, first, first + 1, 0, s->across, 21);
    fill(dc, s, first, last, 0, 1, 21);
    fill(dc, s, last - 1, last, 0, s->across, 17);
    fill(dc, s, first, last, s->across - 1, s->across, 17);
    if (arrow && last - first >= 6 && s->across >= 6)
    {
        LONG middle = (first + last) / 2;
        for (LONG row = 0; row < 4; row++)
        {
            LONG axis = end ? middle + 1 - row : middle - 2 + row;
            fill(dc, s, axis, axis + 1, s->across / 2 - row, s->across / 2 + row + 1,
                 enabled ? 19 : 18);
        }
    }
}
static void paint(Open32Window *window)
{
    uint8 record[64];
    ULONG dc = BeginPaint(window->handle, record);
    if (!dc)
        return;
    ScrollLayout s = layout(window);
    BOOL enabled =
        IsWindowEnabled(window->handle) && window->scroll[2].minimum < window->scroll[2].maximum;
    fill(dc, &s, 0, s.extent, 0, s.across, 1);
    if (s.button)
    {
        button(dc, &s, 0, s.button, TRUE, FALSE, enabled);
        button(dc, &s, s.extent - s.button, s.extent, TRUE, TRUE, enabled);
    }
    if (s.thumb && enabled)
        button(dc, &s, s.position, s.position + s.thumb, FALSE, FALSE, TRUE);
    EndPaint(window->handle, record);
}
static void notify(ULONG handle, ULONG request, LONG position)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
        return;
    ULONG parent = window->parent, message = (window->style & 1) ? 0x115 : 0x114;
    if (parent)
        SendMessage(parent, message, request | ((ULONG)(USHORT)position << 16), handle);
}
static LONG dragged(Open32Window *window, const ScrollLayout *s, LONG coordinate)
{
    LONG travel = s->track - s->thumb;
    int64_t offset = (int64_t)coordinate - window->scroll_drag_offset - s->button;
    if (offset < 0)
        offset = 0;
    if (offset > travel)
        offset = travel;
    Open32Scroll *state = &window->scroll[2];
    return travel > 0 ? (LONG)(state->minimum +
                               offset * ((int64_t)state->maximum - state->minimum) / travel)
                      : state->minimum;
}
ULONG open32ScrollWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
        return 0;
    Open32Scroll *state = &window->scroll[2];
    switch (message)
    {
        case 0x81:
            state->maximum = 0;
            return 1;
        case 0xe0:
            return (ULONG)SetScrollPos(handle, 2, (LONG)first, second != 0);
        case 0xe1:
            return (ULONG)state->position;
        case 0xe2:
        case 0xe6:
            return SetScrollRange(handle, 2, (LONG)first, (LONG)second, message == 0xe6);
        case 0xe3:
        {
            uint8 minimum[4], maximum[4];
            lx_guest_store_u32(minimum, (ULONG)state->minimum);
            lx_guest_store_u32(maximum, (ULONG)state->maximum);
            if (!lx_execution_memory(GLoaderState.execution, first, 4, LX_GUEST_WRITE) ||
                !lx_execution_memory(GLoaderState.execution, second, 4, LX_GUEST_WRITE))
            {
                open32SetError(87);
                return 0;
            }
            lx_execution_write(GLoaderState.execution, first, minimum, 4);
            lx_execution_write(GLoaderState.execution, second, maximum, 4);
            return 0;
        }
        case 0x87:
            return 1; /* DLGC_WANTARROWS */
        case 15:
            paint(window);
            return 0;
        case 5:
        case 10:
            InvalidateRect(handle, NULL, FALSE);
            return 0;
        case 0x201:
        {
            if (!IsWindowEnabled(handle) || state->minimum == state->maximum)
                return 0;
            ScrollLayout s = layout(window);
            LONG coordinate = (SHORT)(s.vertical ? second >> 16 : second);
            WinSetFocus(HWND_DESKTOP, handle);
            WinSetCapture(HWND_DESKTOP, handle);
            if (coordinate < s.button)
                notify(handle, 0, 0);
            else if (coordinate >= s.extent - s.button)
                notify(handle, 1, 0);
            else if (coordinate < s.position)
                notify(handle, 2, 0);
            else if (coordinate >= s.position + s.thumb)
                notify(handle, 3, 0);
            else
            {
                window->scroll_dragging = TRUE;
                window->scroll_drag_offset = coordinate - s.position;
                window->scroll_track_position = state->position;
            }
            return 0;
        }
        case 0x200:
            if (window->scroll_dragging && WinQueryCapture(HWND_DESKTOP) == handle)
            {
                ScrollLayout s = layout(window);
                LONG coordinate = (SHORT)(s.vertical ? second >> 16 : second);
                LONG position = dragged(window, &s, coordinate);
                window->scroll_track_position = position;
                InvalidateRect(handle, NULL, FALSE);
                notify(handle, 5, position);
            }
            return 0;
        case 0x202:
            if (WinQueryCapture(HWND_DESKTOP) == handle)
            {
                BOOL dragging = window->scroll_dragging;
                LONG position = window->scroll_track_position;
                window->scroll_dragging = FALSE;
                WinSetCapture(HWND_DESKTOP, 0);
                if (dragging)
                    notify(handle, 4, position);
                if (open32FindWindow(handle))
                    notify(handle, 8, 0);
                if (open32FindWindow(handle))
                    InvalidateRect(handle, NULL, FALSE);
            }
            return 0;
        case 0x100:
        {
            BOOL vertical = (window->style & 1) != 0;
            ULONG request = first == (vertical ? 0x26u : 0x25u)   ? 0
                            : first == (vertical ? 0x28u : 0x27u) ? 1
                            : first == 0x21                       ? 2
                            : first == 0x22                       ? 3
                            : first == 0x24                       ? 6
                            : first == 0x23                       ? 7
                                                                  : 9;
            if (request != 9 && IsWindowEnabled(handle))
                notify(handle, request, 0);
            return 0;
        }
        default:
            return (ULONG)DefWindowProc(handle, message, first, second);
    }
}
