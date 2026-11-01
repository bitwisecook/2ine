/* Public domain. No rights reserved. Wholly AI-authored. */
/* Multiline edit storage uses byte offsets and CR/LF line endings. */
#include "os2libs/pmwin/controls.h"
MRESULT mleMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    const char *text = window->text ? window->text : "";
    size_t length = strlen(text);
    LONG a = (LONG)(intptr_t)first, b = (LONG)(intptr_t)second;
    switch (message)
    {
        case 0x1b0:
        {
            LONG old = (LONG)c->limit;
            if (a >= 0)
                c->limit = (unsigned)a;
            return controlReply(old);
        }
        case 0x1b1:
            return controlReply((LONG)c->limit);
        case 0x1b4:
        {
            BOOL old = (window->style & 1) != 0;
            if (a)
                window->style |= 1;
            else
                window->style &= ~1u;
            controlRepaint(window);
            return controlReply(old);
        }
        case 0x1b5:
            return controlReply((window->style & 1) != 0);
        case 0x1b8:
        {
            BOOL old = (window->style & 0x10) != 0;
            if (a)
                window->style |= 0x10;
            else
                window->style &= ~0x10u;
            return controlReply(old);
        }
        case 0x1b9:
            return controlReply((window->style & 0x10) != 0);
        case 0x1ba:
            return controlReply(c->changed);
        case 0x1bb:
        {
            BOOL old = c->changed;
            c->changed = a != 0;
            return controlReply(old);
        }
        case 0x1bc:
        {
            LONG count = 1;
            for (const char *p = text; *p; p++)
                if (*p == '\n')
                    count++;
            return controlReply(count);
        }
        case 0x1bd:
        {
            LONG line = 0;
            size_t i = 0;
            for (; i < length && line < a; i++)
                if (text[i] == '\n')
                    line++;
            return controlReply((LONG)i);
        }
        case 0x1be:
        {
            LONG line = 0;
            for (size_t i = 0; i < length && i < (ULONG)a; i++)
                if (text[i] == '\n')
                    line++;
            return controlReply(line);
        }
        case 0x1bf:
        {
            size_t position = a < 0 ? c->last : (size_t)a;
            if (position > length)
                position = length;
            while (position && text[position - 1] != '\n')
                position--;
            size_t end = position;
            while (end < length && text[end] != '\r' && text[end] != '\n')
                end++;
            return controlReply((LONG)(end - position));
        }
        case 0x1c0:
        case 0x1c8:
            return controlReply((LONG)length);
        case 0x1ca:
            c->first = a < 0 ? c->first : (ULONG)a > length ? (unsigned)length : (unsigned)a;
            c->last = b < 0 ? c->last : (ULONG)b > length ? (unsigned)length : (unsigned)b;
            controlRepaint(window);
            return 0;
        case 0x1cb:
            return controlReply(a == 0   ? (LONG)c->first
                                : a == 1 ? (LONG)c->last
                                : a == 2 ? (LONG)(c->first < c->last ? c->first : c->last)
                                         : (LONG)(c->first > c->last ? c->first : c->last));
        case 0x1c9:
        {
            char *insert = controlCopyText(first);
            if (!insert)
                return 0;
            size_t bytes = strlen(insert);
            controlReplaceSelection(window, insert, bytes);
            free(insert);
            return controlReply((LONG)bytes);
        }
        case WM_CHAR:
        {
            ULONG flags = (ULONG)(uintptr_t)first, key = (ULONG)(uintptr_t)second >> 16;
            if (!(flags & 0x40) && (flags & 2) && (key == 0x1e || key == 8))
            {
                controlReplaceSelection(window, "\r\n", 2);
                return 0;
            }
            if (!(flags & 0x40) && (flags & 2) && key == 6 && !(window->style & 0x20))
            {
                controlReplaceSelection(window, "\t", 1);
                return 0;
            }
            return entryMessage(window, message, first, second);
        }
        case WM_BUTTON1DOWN:
        case WM_SETFOCUS:
            return entryMessage(window, message, first, second);
        default:
            return WinDefWindowProc(window->hwnd, message, first, second);
    }
}
