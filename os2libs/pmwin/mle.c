/* Public domain. No rights reserved. Wholly AI-authored. */
/* Multiline edit storage uses byte offsets and CR/LF line endings. */
#include "os2libs/pmwin/controls.h"

static BOOL replace(Window *window, size_t position, size_t removed, const char *data, size_t size)
{
    PmControl *c = window->control;
    const char *old = window->text ? window->text : "";
    size_t length = strlen(old);
    if (position > length || removed > length - position || size > c->limit ||
        length - removed > c->limit - size)
        return FALSE;
    char *copy = malloc(length - removed + size + 1);
    if (!copy)
        return FALSE;
    memcpy(copy, old, position);
    if (size)
        memcpy(copy + position, data, size);
    memcpy(copy + position + size, old + position + removed, length - position - removed + 1);
    free(window->text);
    window->text = copy;
    c->first = c->last = (unsigned)(position + size);
    c->changed = TRUE;
    controlRepaint(window);
    controlNotify(window, 7); /* MLN_CHANGE */
    return TRUE;
}
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
        case 0x1c1: /* MLM_FORMAT */
        {
            unsigned old = c->transfer_format;
            if ((unsigned)a <= 2)
                c->transfer_format = (unsigned)a;
            return controlReply((LONG)old);
        }
        case 0x1c2: /* MLM_SETIMPORTEXPORT: caller retains the transfer buffer. */
            c->transfer = first;
            c->transfer_size = (USHORT)b;
            return controlReply(first != NULL && c->transfer_size != 0);
        case 0x1c3: /* MLM_IMPORT */
        case 0x1c4: /* MLM_EXPORT */
        {
            uint8 offset[4];
            if (!controlRead(first, offset, sizeof offset) || !c->transfer)
                return 0;
            LONG position = (LONG)lx_guest_load_u32(offset);
            if (position == -1)
                position = (LONG)c->last;
            uint8 requested[4];
            ULONG wanted = (ULONG)b;
            if (message == 0x1c4)
            {
                if (!controlRead(second, requested, sizeof requested))
                    return 0;
                wanted = lx_guest_load_u32(requested);
            }
            if (position < 0 || (size_t)position > length)
                return 0;
            size_t count = wanted < c->transfer_size ? wanted : c->transfer_size;
            if (message == 0x1c3)
            {
                char *data = malloc(count ? count : 1);
                if (!data)
                    return 0;
                BOOL ok = controlRead(c->transfer, data, count);
                if (ok)
                {
                    count = strnlen(data, count);
                    TRACE_NATIVE("MLE import: position=%d, bytes=%u", (int)position, (uint)count);
                    ok = replace(window, position, 0, data, count);
                }
                free(data);
                if (!ok)
                    return 0;
            }
            else
            {
                if (count > length - (size_t)position)
                    count = length - (size_t)position;
                if (!controlWrite(c->transfer, text + position, count))
                    return 0;
                lx_guest_store_u32(requested, wanted - (ULONG)count);
                if (!controlWrite(second, requested, sizeof requested))
                    return 0;
            }
            lx_guest_store_u32(offset, (uint32)position + (uint32)count);
            if (!controlWrite(first, offset, sizeof offset))
                return 0;
            return controlReply((LONG)count);
        }
        case 0x1c6: /* MLM_DELETE: programmatic edits also apply to read-only controls. */
        {
            size_t position = a < 0 ? c->last : (size_t)a;
            if (position > length || b < 0)
                return 0;
            size_t count = (size_t)b < length - position ? (size_t)b : length - position;
            return controlReply(replace(window, position, count, NULL, 0) ? (LONG)count : 0);
        }
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
