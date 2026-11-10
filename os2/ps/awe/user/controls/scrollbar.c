/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard scrollbar controls paint their range and notify the owning window. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"

enum
{
    /* A system-colour brush handle is the COLOR_* index plus one. */
    BRUSH_SCROLLBAR = WIN32_COLOR_SCROLLBAR + 1,
    BRUSH_BTNFACE = WIN32_COLOR_BTNFACE + 1,
    BRUSH_BTNSHADOW = WIN32_COLOR_BTNSHADOW + 1,
    BRUSH_GRAYTEXT = WIN32_COLOR_GRAYTEXT + 1,
    BRUSH_BTNTEXT = WIN32_COLOR_BTNTEXT + 1,
    BRUSH_BTNHIGHLIGHT = WIN32_COLOR_BTNHIGHLIGHT + 1,
    /* The arrow is a four-row triangle, drawn only when the button can hold it. */
    ARROW_ROWS = 4,
    ARROW_MIN_BUTTON = 6,
    /* Keys without a scroll request leave the bar alone. */
    SCROLL_REQUEST_NONE = 9
};

typedef struct ScrollLayout
{
    BOOL vertical;
    LONG extent, across, button, track, thumb, position;
} ScrollLayout;

static ScrollLayout layout(Open32Window *window)
{
    Open32Scroll *state = &window->scroll[WIN32_SB_CTL];
    BOOL vertical = (window->style & WIN32_SBS_VERT) != 0;
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
    uint8 rect[WIN32_RECT_SIZE];
    LONG fields[4] = {s->vertical ? c : a, s->vertical ? a : c, s->vertical ? d : b,
                      s->vertical ? b : d};
    for (unsigned i = 0; i < 4; i++)
        lx_guest_store_u32(rect + i * 4, (ULONG)fields[i]);
    FillRect(dc, rect, brush);
}

static void button(ULONG dc, const ScrollLayout *s, LONG first, LONG last, BOOL arrow, BOOL end,
                   BOOL enabled)
{
    fill(dc, s, first, last, 0, s->across, BRUSH_BTNFACE);
    fill(dc, s, first, first + 1, 0, s->across, BRUSH_BTNHIGHLIGHT);
    fill(dc, s, first, last, 0, 1, BRUSH_BTNHIGHLIGHT);
    fill(dc, s, last - 1, last, 0, s->across, BRUSH_BTNSHADOW);
    fill(dc, s, first, last, s->across - 1, s->across, BRUSH_BTNSHADOW);
    if (arrow && last - first >= ARROW_MIN_BUTTON && s->across >= ARROW_MIN_BUTTON)
    {
        LONG middle = (first + last) / 2;
        for (LONG row = 0; row < ARROW_ROWS; row++)
        {
            LONG axis = end ? middle + 1 - row : middle - 2 + row;
            fill(dc, s, axis, axis + 1, s->across / 2 - row, s->across / 2 + row + 1,
                 enabled ? BRUSH_BTNTEXT : BRUSH_GRAYTEXT);
        }
    }
}

static void paint(Open32Window *window)
{
    uint8 record[WIN32_PAINTSTRUCT_SIZE];
    ULONG dc = BeginPaint(window->handle, record);
    if (!dc)
        return;
    ScrollLayout s = layout(window);
    BOOL enabled = IsWindowEnabled(window->handle) &&
                   window->scroll[WIN32_SB_CTL].minimum < window->scroll[WIN32_SB_CTL].maximum;
    fill(dc, &s, 0, s.extent, 0, s.across, BRUSH_SCROLLBAR);
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
    ULONG parent = window->parent,
          message = (window->style & WIN32_SBS_VERT) ? WIN32_WM_VSCROLL : WIN32_WM_HSCROLL;
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
    Open32Scroll *state = &window->scroll[WIN32_SB_CTL];
    return travel > 0 ? (LONG)(state->minimum +
                               offset * ((int64_t)state->maximum - state->minimum) / travel)
                      : state->minimum;
}

ULONG open32ScrollWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
        return 0;
    Open32Scroll *state = &window->scroll[WIN32_SB_CTL];
    switch (message)
    {
        case WIN32_WM_NCCREATE:
            state->maximum = 0;
            return 1;
        case WIN32_SBM_SETPOS:
            return (ULONG)SetScrollPos(handle, WIN32_SB_CTL, (LONG)first, second != 0);
        case WIN32_SBM_GETPOS:
            return (ULONG)state->position;
        case WIN32_SBM_SETRANGE:
        case WIN32_SBM_SETRANGEREDRAW:
            return SetScrollRange(handle, WIN32_SB_CTL, (LONG)first, (LONG)second,
                                  message == WIN32_SBM_SETRANGEREDRAW);
        case WIN32_SBM_GETRANGE:
        {
            uint8 minimum[4], maximum[4];
            lx_guest_store_u32(minimum, (ULONG)state->minimum);
            lx_guest_store_u32(maximum, (ULONG)state->maximum);
            if (!lx_execution_memory(GProcessState.execution, first, 4, LX_GUEST_WRITE) ||
                !lx_execution_memory(GProcessState.execution, second, 4, LX_GUEST_WRITE))
            {
                open32SetError(ERROR_INVALID_PARAMETER);
                return 0;
            }
            lx_execution_write(GProcessState.execution, first, minimum, 4);
            lx_execution_write(GProcessState.execution, second, maximum, 4);
            return 0;
        }
        case WIN32_WM_GETDLGCODE:
            return WIN32_DLGC_WANTARROWS;
        case WIN32_WM_PAINT:
            paint(window);
            return 0;
        case WIN32_WM_SIZE:
        case WIN32_WM_ENABLE:
            InvalidateRect(handle, NULL, FALSE);
            return 0;
        case WIN32_WM_LBUTTONDOWN:
        {
            if (!IsWindowEnabled(handle) || state->minimum == state->maximum)
                return 0;
            ScrollLayout s = layout(window);
            LONG coordinate = (SHORT)(s.vertical ? second >> 16 : second);
            WinSetFocus(HWND_DESKTOP, handle);
            WinSetCapture(HWND_DESKTOP, handle);
            if (coordinate < s.button)
                notify(handle, WIN32_SB_LINEUP, 0);
            else if (coordinate >= s.extent - s.button)
                notify(handle, WIN32_SB_LINEDOWN, 0);
            else if (coordinate < s.position)
                notify(handle, WIN32_SB_PAGEUP, 0);
            else if (coordinate >= s.position + s.thumb)
                notify(handle, WIN32_SB_PAGEDOWN, 0);
            else
            {
                window->scroll_dragging = TRUE;
                window->scroll_drag_offset = coordinate - s.position;
                window->scroll_track_position = state->position;
            }
            return 0;
        }
        case WIN32_WM_MOUSEMOVE:
            if (window->scroll_dragging && WinQueryCapture(HWND_DESKTOP) == handle)
            {
                ScrollLayout s = layout(window);
                LONG coordinate = (SHORT)(s.vertical ? second >> 16 : second);
                LONG position = dragged(window, &s, coordinate);
                window->scroll_track_position = position;
                InvalidateRect(handle, NULL, FALSE);
                notify(handle, WIN32_SB_THUMBTRACK, position);
            }
            return 0;
        case WIN32_WM_LBUTTONUP:
            if (WinQueryCapture(HWND_DESKTOP) == handle)
            {
                BOOL dragging = window->scroll_dragging;
                LONG position = window->scroll_track_position;
                window->scroll_dragging = FALSE;
                WinSetCapture(HWND_DESKTOP, 0);
                if (dragging)
                    notify(handle, WIN32_SB_THUMBPOSITION, position);
                if (open32FindWindow(handle))
                    notify(handle, WIN32_SB_ENDSCROLL, 0);
                if (open32FindWindow(handle))
                    InvalidateRect(handle, NULL, FALSE);
            }
            return 0;
        case WIN32_WM_KEYDOWN:
        {
            BOOL vertical = (window->style & WIN32_SBS_VERT) != 0;
            ULONG request = first == (vertical ? (ULONG)WIN32_VK_UP : (ULONG)WIN32_VK_LEFT)
                                ? WIN32_SB_LINEUP
                            : first == (vertical ? (ULONG)WIN32_VK_DOWN : (ULONG)WIN32_VK_RIGHT)
                                ? WIN32_SB_LINEDOWN
                            : first == WIN32_VK_PRIOR ? WIN32_SB_PAGEUP
                            : first == WIN32_VK_NEXT  ? WIN32_SB_PAGEDOWN
                            : first == WIN32_VK_HOME  ? WIN32_SB_TOP
                            : first == WIN32_VK_END   ? WIN32_SB_BOTTOM
                                                      : SCROLL_REQUEST_NONE;
            if (request != SCROLL_REQUEST_NONE && IsWindowEnabled(handle))
                notify(handle, request, 0);
            return 0;
        }
        default:
            return (ULONG)DefWindowProc(handle, message, first, second);
    }
}
