/* Public domain. No rights reserved. Wholly AI-authored. */
/* Byte-oriented entry selection and editing in the presentation space's codepage. */
#include "os2libs/pmwin/controls.h"
enum
{
    EM_QUERYCHANGED = 0x140,
    EM_QUERYSEL = 0x141,
    EM_SETSEL = 0x142,
    EM_SETTEXTLIMIT = 0x143,
    EM_CLEAR = 0x146,
    EM_QUERYFIRST = 0x148,
    EM_SETFIRST = 0x149,
    EM_QUERYREADONLY = 0x14a,
    EM_SETREADONLY = 0x14b,
    EM_SETINSERT = 0x14c
};
void controlReplaceSelection(Window *window, const char *text, size_t bytes)
{
    PmControl *c = window->control;
    if (window->style & (c->kind == 10 ? 0x10 : 0x20))
        return; /* ES_READONLY */
    const char *old = window->text ? window->text : "";
    size_t length = strlen(old), low = c->first < c->last ? c->first : c->last,
           high = c->first < c->last ? c->last : c->first;
    if (low > length)
        low = length;
    if (high > length)
        high = length;
    if (!c->insert && high == low && bytes && high < length)
        high++;
    if (length - (high - low) + bytes > c->limit)
    {
        controlNotify(window, 0x20);
        return;
    }
    char *copy = malloc(length - (high - low) + bytes + 1);
    if (!copy)
    {
        controlNotify(window, 0x10);
        return;
    }
    memcpy(copy, old, low);
    memcpy(copy + low, text, bytes);
    memcpy(copy + low + bytes, old + high, length - high + 1);
    free(window->text);
    window->text = copy;
    c->first = c->last = (unsigned)(low + bytes);
    c->changed = TRUE;
    controlRepaint(window);
    controlNotify(window, 4); /* EN_CHANGE */
}
MRESULT entryMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    size_t length = window->text ? strlen(window->text) : 0;
    switch (message)
    {
        case EM_QUERYCHANGED:
        {
            BOOL changed = c->changed;
            c->changed = FALSE;
            return controlReply(changed);
        }
        case EM_QUERYSEL:
            return controlReply((LONG)(c->first | c->last << 16));
        case EM_SETSEL:
            c->first = (USHORT)(uintptr_t)first;
            c->last = (USHORT)((uintptr_t)first >> 16);
            if (c->first > length)
                c->first = (unsigned)length;
            if (c->last > length)
                c->last = (unsigned)length;
            controlRepaint(window);
            return controlReply(TRUE);
        case EM_SETTEXTLIMIT:
            if (!(USHORT)(uintptr_t)first || (USHORT)(uintptr_t)first > 32767)
                return 0;
            c->limit = (USHORT)(uintptr_t)first;
            return controlReply(TRUE);
        case EM_QUERYREADONLY:
            return controlReply((window->style & 0x20) != 0);
        case EM_SETREADONLY:
            if ((USHORT)(uintptr_t)first)
                window->style |= 0x20;
            else
                window->style &= ~0x20u;
            return controlReply(TRUE);
        case EM_SETINSERT:
            c->insert = (USHORT)(uintptr_t)first != 0;
            return controlReply(TRUE);
        case EM_QUERYFIRST:
            return controlReply((LONG)c->view);
        case EM_SETFIRST:
            c->view = (USHORT)(uintptr_t)first;
            controlRepaint(window);
            return controlReply(TRUE);
        case EM_CLEAR:
            controlReplaceSelection(window, "", 0);
            return controlReply(TRUE);
        case WM_SETFOCUS:
            controlRepaint(window);
            controlNotify(window, (uintptr_t)second ? 1 : 2);
            return 0;
        case WM_BUTTON1DOWN:
            c->first = c->last = (unsigned)length;
            controlRepaint(window);
            return 0;
        case WM_CHAR:
            break;
        default:
            return WinDefWindowProc(window->hwnd, message, first, second);
    }
    ULONG flags = (USHORT)(uintptr_t)first;
    unsigned character = (USHORT)(uintptr_t)second, key = (USHORT)((uintptr_t)second >> 16);
    if (flags & 0x40)
        return 0; /* KC_KEYUP */
    if ((flags & 0x10) && (character == 'a' || character == 'A'))
    {
        c->first = 0;
        c->last = (unsigned)length;
        controlRepaint(window);
        return 0;
    }
    if (flags & 2) /* KC_VIRTUALKEY */
    {
        unsigned position = c->last;
        if (key == 5 || key == 0x1b) /* VK_BACKSPACE / VK_DELETE */
        {
            if (c->first == c->last)
            {
                if (key == 5 && c->first)
                    c->first--;
                else if (key == 0x1b && c->last < length)
                    c->last++;
            }
            controlReplaceSelection(window, "", 0);
            return 0;
        }
        if (key == 0x15 && position)
            position--;
        else if (key == 0x17 && position < length)
            position++;
        else if (key == 0x14)
            position = 0;
        else if (key == 0x13)
            position = (unsigned)length;
        else
            return WinDefWindowProc(window->hwnd, message, first, second);
        c->last = position;
        if (!(flags & 8))
            c->first = position;
        controlRepaint(window);
        return 0;
    }
    if ((flags & 1) && !(flags & (0x10 | 0x20)) && character >= 32 && character <= 255)
    {
        char byte = (char)character;
        controlReplaceSelection(window, &byte, 1);
    }
    return 0;
}
