/* Public domain. No rights reserved. Wholly AI-authored. */
/* WC_LISTBOX: the item list, selection and the LM_* messages. */
#include "os2/ps/pmmerge/controls/pmwctls/controls.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/ps/common/pm_text.h"

MRESULT listMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    int index = (SHORT)(uintptr_t)first;
    if (message == LM_SEARCHSTRING) /* The command and start index share mp1. */
    {
        unsigned flags = (USHORT)(uintptr_t)first;
        int start = (SHORT)((uintptr_t)first >> 16);
        if (!(flags & 4) || (flags & ~7u) || (flags & 3) == 3 || start < -1 ||
            (start >= 0 && (size_t)start >= c->count))
            return controlReply(-3);
        char *needle = controlCopyText(second);
        if (!needle)
            return controlReply(-3);
        size_t length = strlen(needle);
        LONG match = -1;
        for (size_t step = 0; step < c->count; step++)
        {
            size_t candidate = ((size_t)(start + 1) + step) % c->count;
            const char *text = c->items[candidate].text;
            BOOL equal = flags & 1   ? strstr(text, needle) != NULL
                         : flags & 2 ? !strncmp(text, needle, length)
                                     : !strcmp(text, needle);
            if (equal)
            {
                match = (LONG)candidate;
                break;
            }
        }
        free(needle);
        return controlReply(match);
    }
    if (message == LM_QUERYITEMCOUNT) /* LM_QUERYITEMCOUNT */
        return controlReply((LONG)c->count);
    if (message == LM_DELETEALL) /* LM_DELETEALL */
    {
        for (size_t i = 0; i < c->count; i++)
            free(c->items[i].text);
        c->count = 0;
        c->selected = -1;
        c->top = 0;
        controlRepaint(window);
        return controlReply(TRUE);
    }
    if (message == LM_INSERTITEM) /* LM_INSERTITEM: END=-1, SORTASCENDING=-2, SORTDESCENDING=-3 */
    {
        char *text = controlCopyText(second);
        if (!text)
            return controlReply(-2); /* LIT_MEMERROR */
        if (index == -1)
            index = (int)c->count;
        else if (index == -2 || index == -3)
        {
            BOOL ascending = index == -2;
            for (index = 0; (size_t)index < c->count; index++)
            {
                int comparison = strcmp(text, c->items[index].text);
                if (ascending ? comparison < 0 : comparison > 0)
                    break;
            }
        }
        if (index < 0 || (size_t)index > c->count || c->count >= INT16_MAX)
        {
            free(text);
            return controlReply(-3); /* LIT_ERROR */
        }
        ListItem *items = realloc(c->items, (c->count + 1) * sizeof *items);
        if (!items)
        {
            free(text);
            return controlReply(-2);
        }
        c->items = items;
        memmove(items + index + 1, items + index, (c->count - index) * sizeof *items);
        items[index] = (ListItem){text, 0, FALSE};
        c->count++;
        if ((window->style & 2) && window->owner)
        {
            ULONG measured = (ULONG)(uintptr_t)WinSendMsg(window->owner->hwnd, WM_MEASUREITEM,
                                                          (MPARAM)(uintptr_t)window->id,
                                                          (MPARAM)(intptr_t)index);
            if ((SHORT)measured > c->item_height)
                c->item_height = (SHORT)measured;
        }
        if (c->selected >= index)
            c->selected++;
        controlRepaint(window);
        return controlReply(index);
    }
    if (message == LM_QUERYSELECTION) /* LM_QUERYSELECTION */
    {
        size_t start = !(window->style & 0x11) || index < 0 ? 0 : (size_t)index + 1;
        for (size_t i = start; i < c->count; i++)
            if (c->items[i].selected)
                return controlReply((LONG)i);
        return controlReply(-1);
    }
    if (message == LM_SELECTITEM && index == -1)
    {
        BOOL changed = FALSE;
        for (size_t i = 0; i < c->count; i++)
        {
            changed |= c->items[i].selected;
            c->items[i].selected = FALSE;
        }
        c->selected = -1;
        controlRepaint(window);
        if (changed && !(window->style & 0x11) && !(USHORT)(uintptr_t)second)
            controlNotify(window, 1); /* LN_SELECT */
        return controlReply(TRUE);
    }
    if (message == LM_QUERYTOPINDEX) /* LM_QUERYTOPINDEX */
        return controlReply(c->count ? c->top : -1);
    if (index < 0 || (size_t)index >= c->count)
        return controlReply(message == LM_SELECTITEM || message == LM_SETTOPINDEX ||
                                    message == LM_SETITEMTEXT || message == LM_SETITEMHANDLE ||
                                    message == LM_QUERYITEMHANDLE || message == LM_QUERYITEMTEXT
                                ? 0
                                : -3);
    switch (message)
    {
        case LM_SETTOPINDEX:
            c->top = index;
            break;
        case LM_DELETEITEM:
            free(c->items[index].text);
            memmove(c->items + index, c->items + index + 1,
                    (c->count - index - 1) * sizeof *c->items);
            c->count--;
            if (c->selected == index)
                c->selected = -1;
            else if (c->selected > index)
                c->selected--;
            if ((size_t)c->top >= c->count)
                c->top = 0;
            controlRepaint(window);
            return controlReply((LONG)c->count);
        case LM_SELECTITEM:
        {
            if (!(USHORT)(uintptr_t)second && !c->items[index].selected)
                return controlReply(FALSE);
            BOOL selected = (USHORT)(uintptr_t)second != 0;
            BOOL changed = c->items[index].selected != selected;
            if (!(window->style & 0x11) && (uintptr_t)second)
                for (size_t i = 0; i < c->count; i++)
                    c->items[i].selected = FALSE;
            c->items[index].selected = selected;
            c->selected = selected ? index : -1;
            controlRepaint(window);
            if (changed)
                controlNotify(window, 1); /* LN_SELECT */
            return controlReply(TRUE);
        }
        case LM_SETITEMTEXT:
        {
            char *text = controlCopyText(second);
            if (!text)
                return controlReply(FALSE);
            free(c->items[index].text);
            c->items[index].text = text;
            break;
        }
        case LM_QUERYITEMTEXTLENGTH:
            return controlReply((LONG)strlen(c->items[index].text));
        case LM_QUERYITEMTEXT:
        {
            size_t capacity = ((uintptr_t)first >> 16) & 0xffff;
            size_t length = strlen(c->items[index].text);
            return controlReply(controlTextOutput(second, c->items[index].text, capacity)
                                    ? (LONG)(length < capacity ? length : capacity - 1)
                                    : 0);
        }
        case LM_SETITEMHANDLE:
            c->items[index].handle = (ULONG)(uintptr_t)second;
            return controlReply(TRUE);
        case LM_QUERYITEMHANDLE:
            return controlReply((LONG)c->items[index].handle);
        default:
            return controlReply(-3);
    }
    controlRepaint(window);
    return controlReply(TRUE);
}
