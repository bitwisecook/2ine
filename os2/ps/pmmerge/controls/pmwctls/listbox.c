/* Public domain. No rights reserved. Wholly AI-authored. */
/* WC_LISTBOX: the item list, selection and the LM_* messages. */
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/common/pm_text.h"

enum
{
    LIST_SEARCH_FLAGS = LSS_SUBSTRING | LSS_PREFIX | LSS_CASESENSITIVE,
    /* Styles where any number of items may be selected at once. */
    LIST_MULTIPLE_SELECTION = LS_MULTIPLESEL | LS_EXTENDEDSEL
};

MRESULT listMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    int index = (SHORT)(uintptr_t)first;
    if (message == LM_SEARCHSTRING) /* The command and start index share mp1. */
    {
        unsigned flags = (USHORT)(uintptr_t)first;
        int start = (SHORT)((uintptr_t)first >> 16);
        if (!(flags & LSS_CASESENSITIVE) || (flags & ~(unsigned)LIST_SEARCH_FLAGS) ||
            (flags & (LSS_SUBSTRING | LSS_PREFIX)) == (LSS_SUBSTRING | LSS_PREFIX) ||
            start < LIT_FIRST || (start >= 0 && (size_t)start >= c->count))
            return controlReply(LIT_ERROR);
        char *needle = controlCopyText(second);
        if (!needle)
            return controlReply(LIT_ERROR);
        size_t length = strlen(needle);
        LONG match = LIT_NONE;
        for (size_t step = 0; step < c->count; step++)
        {
            size_t candidate = ((size_t)(start + 1) + step) % c->count;
            const char *text = c->items[candidate].text;
            BOOL equal = flags & LSS_SUBSTRING ? strstr(text, needle) != NULL
                         : flags & LSS_PREFIX  ? !strncmp(text, needle, length)
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
    if (message == LM_QUERYITEMCOUNT)
        return controlReply((LONG)c->count);
    if (message == LM_DELETEALL)
    {
        for (size_t i = 0; i < c->count; i++)
            free(c->items[i].text);
        c->count = 0;
        c->selected = -1;
        c->top = 0;
        controlRepaint(window);
        return controlReply(TRUE);
    }
    if (message == LM_INSERTITEM)
    {
        char *text = controlCopyText(second);
        if (!text)
            return controlReply(LIT_MEMERROR);
        if (index == LIT_END)
            index = (int)c->count;
        else if (index == LIT_SORTASCENDING || index == LIT_SORTDESCENDING)
        {
            BOOL ascending = index == LIT_SORTASCENDING;
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
            return controlReply(LIT_ERROR);
        }
        ListItem *items = realloc(c->items, (c->count + 1) * sizeof *items);
        if (!items)
        {
            free(text);
            return controlReply(LIT_MEMERROR);
        }
        c->items = items;
        memmove(items + index + 1, items + index, (c->count - index) * sizeof *items);
        items[index] = (ListItem){text, 0, FALSE};
        c->count++;
        if ((window->style & LS_OWNERDRAW) && window->owner)
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
    if (message == LM_QUERYSELECTION)
    {
        size_t start =
            !(window->style & LIST_MULTIPLE_SELECTION) || index < 0 ? 0 : (size_t)index + 1;
        for (size_t i = start; i < c->count; i++)
            if (c->items[i].selected)
                return controlReply((LONG)i);
        return controlReply(LIT_NONE);
    }
    if (message == LM_SELECTITEM && index == LIT_NONE)
    {
        BOOL changed = FALSE;
        for (size_t i = 0; i < c->count; i++)
        {
            changed |= c->items[i].selected;
            c->items[i].selected = FALSE;
        }
        c->selected = -1;
        controlRepaint(window);
        if (changed && !(window->style & LIST_MULTIPLE_SELECTION) && !(USHORT)(uintptr_t)second)
            controlNotify(window, LN_SELECT);
        return controlReply(TRUE);
    }
    if (message == LM_QUERYTOPINDEX)
        return controlReply(c->count ? c->top : LIT_NONE);
    if (index < 0 || (size_t)index >= c->count)
        return controlReply(message == LM_SELECTITEM || message == LM_SETTOPINDEX ||
                                    message == LM_SETITEMTEXT || message == LM_SETITEMHANDLE ||
                                    message == LM_QUERYITEMHANDLE || message == LM_QUERYITEMTEXT
                                ? 0
                                : LIT_ERROR);
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
            if (!(window->style & LIST_MULTIPLE_SELECTION) && (uintptr_t)second)
                for (size_t i = 0; i < c->count; i++)
                    c->items[i].selected = FALSE;
            c->items[index].selected = selected;
            c->selected = selected ? index : -1;
            controlRepaint(window);
            if (changed)
                controlNotify(window, LN_SELECT);
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
            return controlReply(LIT_ERROR);
    }
    controlRepaint(window);
    return controlReply(TRUE);
}
