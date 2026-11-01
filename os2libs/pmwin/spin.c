/* Public domain. No rights reserved. Wholly AI-authored. */
/* Numeric and string-array spin values retain validated limits and current state. */
#include "os2libs/pmwin/controls.h"
#include "guest/abi.h"
#include <errno.h>
static BOOL number(const char *text, LONG *value)
{
    if (!text || !*text)
        return FALSE;
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno || *end || n < INT32_MIN || n > INT32_MAX)
        return FALSE;
    *value = (LONG)n;
    return TRUE;
}
static void set_value(Window *window, LONG value)
{
    PmControl *c = window->control;
    char text[32];
    c->value = value;
    const char *string;
    if (c->count)
        string = c->items[value].text;
    else
    {
        snprintf(text, sizeof text, "%d", (int)value);
        string = text;
    }
    if (!window->text || strcmp(window->text, string))
        WinSetWindowText(window->hwnd, (PSZ)string);
}
MRESULT spinMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    LONG a = (LONG)(uintptr_t)first, b = (LONG)(uintptr_t)second;
    if (message == 0x206) /* SPBM_SETARRAY */
    {
        ULONG count = (ULONG)(uintptr_t)second;
        if (!count || count > 32767)
            return 0;
        ListItem *items = calloc(count, sizeof *items);
        if (!items)
            return 0;
        ULONG i;
        for (i = 0; i < count; i++)
        {
            uint8 wire[4];
            if (!controlRead((MPARAM)((uintptr_t)first + 4 * i), wire, 4) ||
                !(items[i].text = controlCopyText((MPARAM)(uintptr_t)lx_guest_load_u32(wire))))
                break;
        }
        if (i != count)
        {
            while (i)
                free(items[--i].text);
            free(items);
            return 0;
        }
        for (size_t j = 0; j < c->count; j++)
            free(c->items[j].text);
        free(c->items);
        c->items = items;
        c->count = count;
        set_value(window, 0);
        return controlReply(TRUE);
    }
    if (message == 0x207 || message == 0x200) /* SETLIMITS / OVERRIDESETLIMITS */
    {
        if (a < b)
            return 0;
        for (size_t i = 0; i < c->count; i++)
            free(c->items[i].text);
        free(c->items);
        c->items = NULL;
        c->count = 0;
        c->lower = b;
        c->upper = a;
        LONG value = c->value;
        if (message == 0x207)
            number(window->text, &value);
        if (value < b)
            value = b;
        if (value > a)
            value = a;
        set_value(window, value);
        return controlReply(TRUE);
    }
    if (message == 0x201) /* QUERYLIMITS */
    {
        uint8 wire[4];
        lx_guest_store_u32(wire, (uint32)c->upper);
        if (first && !controlWrite(first, wire, 4))
            return 0;
        lx_guest_store_u32(wire, (uint32)c->lower);
        return controlReply(!second || controlWrite(second, wire, 4));
    }
    if (message == 0x202) /* SETTEXTLIMIT */
    {
        if (a <= 0 || a > 32767)
            return 0;
        c->limit = (unsigned)a;
        return controlReply(TRUE);
    }
    if (message == 0x208 || message == 0x203 || message == 0x204) /* SETCURRENT / SPINUP / DOWN */
    {
        LONG value = a;
        if (message != 0x208)
        {
            sint64 next = (sint64)c->value + (message == 0x203 ? 1 : -1);
            LONG low = c->count ? 0 : c->lower, high = c->count ? (LONG)c->count - 1 : c->upper;
            value = next > high ? low : next < low ? high : (LONG)next;
        }
        if (c->count ? value < 0 || (size_t)value >= c->count
                     : value < c->lower || value > c->upper)
            return 0;
        set_value(window, value);
        return controlReply(TRUE);
    }
    if (message == 0x205) /* QUERYVALUE */
    {
        unsigned capacity = (USHORT)(uintptr_t)second, policy = (USHORT)((uintptr_t)second >> 16);
        if (policy != 0 && policy != 1 && policy != 3)
            return 0;
        LONG value = c->value;
        BOOL valid = FALSE;
        if (c->count)
        {
            for (size_t i = 0; i < c->count; i++)
                if (window->text && !strcasecmp(window->text, c->items[i].text))
                {
                    value = (LONG)i;
                    valid = TRUE;
                    break;
                }
        }
        else
            valid = number(window->text, &value) && value >= c->lower && value <= c->upper;
        if (policy != 3)
        {
            if (valid)
                set_value(window, value);
            else if (policy == 1)
                set_value(window, c->value);
        }
        if (!first)
            return controlReply(valid);
        if (capacity)
            return controlReply(
                controlTextOutput(first, window->text ? window->text : "", capacity) && valid);
        uint8 wire[4];
        lx_guest_store_u32(wire, (uint32)c->value);
        return controlReply(controlWrite(first, wire, 4) && valid);
    }
    if (message == WM_BUTTON1DOWN)
    {
        unsigned y = (USHORT)((uintptr_t)first >> 16);
        return spinMessage(window, y >= (unsigned)window->h / 2 ? 0x203 : 0x204, 0, 0);
    }
    if (message == WM_CHAR && !(window->style & 2))
        return entryMessage(window, message, first, second);
    return WinDefWindowProc(window->hwnd, message, first, second);
}
