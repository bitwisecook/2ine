/* Public domain. No rights reserved. Wholly AI-authored. */
/* Byte-oriented entry selection and editing in the presentation space's codepage. */
#include "os2/include/private/pmwctlp.h"

void controlReplaceSelection(Window *window, const char *text, size_t bytes)
{
    PmControl *c = window->control;
    if (window->style & (c->kind == PM_CLASS_MLE ? MLS_READONLY : ES_READONLY))
        return;
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
        controlNotify(window, EN_OVERFLOW);
        return;
    }
    char *copy = malloc(length - (high - low) + bytes + 1);
    if (!copy)
    {
        controlNotify(window, EN_MEMERROR);
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
    controlNotify(window, EN_CHANGE);
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
            if (!(USHORT)(uintptr_t)first || (USHORT)(uintptr_t)first > CONTROL_TEXT_LIMIT_MAX)
                return 0;
            c->limit = (USHORT)(uintptr_t)first;
            return controlReply(TRUE);
        case EM_QUERYREADONLY:
            return controlReply((window->style & ES_READONLY) != 0);
        case EM_SETREADONLY:
            if ((USHORT)(uintptr_t)first)
                window->style |= ES_READONLY;
            else
                window->style &= ~(unsigned)ES_READONLY;
            return controlReply(TRUE);
        case EM_SETINSERTMODE:
            c->insert = (USHORT)(uintptr_t)first != 0;
            return controlReply(TRUE);
        case EM_QUERYFIRSTCHAR:
            return controlReply((LONG)c->view);
        case EM_SETFIRSTCHAR:
            c->view = (USHORT)(uintptr_t)first;
            controlRepaint(window);
            return controlReply(TRUE);
        case EM_CLEAR:
            controlReplaceSelection(window, "", 0);
            return controlReply(TRUE);
        case WM_SETFOCUS:
            controlRepaint(window);
            controlNotify(window, (uintptr_t)second ? EN_SETFOCUS : EN_KILLFOCUS);
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
    if (flags & KC_KEYUP)
        return 0;
    if ((flags & KC_VIRTUALKEY) && key == VK_INSERT) /* Insert mode is shared by all controls. */
    {
        BOOL insert = !WinQuerySysValue(HWND_DESKTOP, SV_INSERTMODE);
        WinSetSysValue(HWND_DESKTOP, SV_INSERTMODE, insert);
        c->insert = insert;
        return (MRESULT)(uintptr_t)TRUE;
    }
    if ((flags & KC_CTRL) && (character == 'a' || character == 'A'))
    {
        c->first = 0;
        c->last = (unsigned)length;
        controlRepaint(window);
        return 0;
    }
    if (flags & KC_VIRTUALKEY)
    {
        unsigned position = c->last;
        if (key == VK_BACKSPACE || key == VK_DELETE)
        {
            if (c->first == c->last)
            {
                if (key == VK_BACKSPACE && c->first)
                    c->first--;
                else if (key == VK_DELETE && c->last < length)
                    c->last++;
            }
            controlReplaceSelection(window, "", 0);
            return 0;
        }
        if (key == VK_LEFT && position)
            position--;
        else if (key == VK_RIGHT && position < length)
            position++;
        else if (key == VK_HOME)
            position = 0;
        else if (key == VK_END)
            position = (unsigned)length;
        else
            return WinDefWindowProc(window->hwnd, message, first, second);
        c->last = position;
        if (!(flags & KC_SHIFT)) /* Shift extends the selection from its anchor. */
            c->first = position;
        controlRepaint(window);
        return 0;
    }
    if ((flags & KC_CHAR) && !(flags & (KC_CTRL | KC_ALT)) && character >= ' ' && character <= 255)
    {
        char byte = (char)character;
        controlReplaceSelection(window, &byte, 1);
    }
    return 0;
}
