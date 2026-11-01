/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window scroll positions use Win32 inclusive limits and retained visibility. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
static Open32Scroll *scroll_state(ULONG handle, LONG bar, Open32Window **owner)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window || bar < 0 || bar > 2)
    {
        open32SetError(window ? 87 : 1400);
        return NULL;
    }
    if (owner)
        *owner = window;
    return &window->scroll[bar];
}
static LONG clamp_position(Open32Scroll *state, LONG position)
{
    return position < state->minimum   ? state->minimum
           : position > state->maximum ? state->maximum
                                       : position;
}
static void scroll_visibility(Open32Window *window, LONG bar, BOOL show, BOOL redraw)
{
    window->scroll[bar].visible = show;
    ULONG style = bar == 0 ? 0x00100000u : bar == 1 ? 0x00200000u : 0;
    if (show)
        window->style |= style;
    else
        window->style &= ~style;
    if (redraw)
        InvalidateRect(window->handle, NULL, FALSE);
}
BOOL SetScrollRange(ULONG handle, LONG bar, LONG minimum, LONG maximum, BOOL redraw)
{
    Open32Window *window;
    Open32Scroll *state = scroll_state(handle, bar, &window);
    if (!state)
        return FALSE;
    if (maximum < minimum || (int64_t)maximum - minimum > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    state->minimum = minimum;
    state->maximum = maximum;
    state->position = clamp_position(state, state->position);
    scroll_visibility(window, bar, minimum != maximum, redraw);
    return TRUE;
}
BOOL GetScrollRange(ULONG handle, LONG bar, PVOID minimum, PVOID maximum)
{
    Open32Scroll *state = scroll_state(handle, bar, NULL);
    if (!state || !minimum || !maximum)
    {
        if (state)
            open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u32(minimum, (ULONG)state->minimum);
    lx_guest_store_u32(maximum, (ULONG)state->maximum);
    return TRUE;
}
LONG SetScrollPos(ULONG handle, LONG bar, LONG position, BOOL redraw)
{
    Open32Window *window;
    Open32Scroll *state = scroll_state(handle, bar, &window);
    if (!state)
        return 0;
    LONG previous = state->position;
    state->position = clamp_position(state, position);
    if (redraw)
        InvalidateRect(window->handle, NULL, FALSE);
    return previous;
}
LONG GetScrollPos(ULONG handle, LONG bar)
{
    Open32Scroll *state = scroll_state(handle, bar, NULL);
    return state ? state->position : 0;
}
BOOL ShowScrollBar(ULONG handle, LONG bar, BOOL show)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window || bar < 0 || bar > 3)
    {
        open32SetError(window ? 87 : 1400);
        return FALSE;
    }
    if (bar == 3)
    {
        scroll_visibility(window, 0, show, FALSE);
        scroll_visibility(window, 1, show, TRUE);
    }
    else
        scroll_visibility(window, bar, show, TRUE);
    return TRUE;
}
