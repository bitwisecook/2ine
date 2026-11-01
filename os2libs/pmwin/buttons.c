/* Public domain. No rights reserved. Wholly AI-authored. */
/* Button check state, automatic transitions and owner notifications. */
#include "os2libs/pmwin/controls.h"
enum
{
    BS_PUSH = 0,
    BS_CHECK = 1,
    BS_AUTOCHECK = 2,
    BS_RADIO = 3,
    BS_AUTORADIO = 4,
    BS_THREE = 5,
    BS_AUTOTHREE = 6,
    BM_CLICK = 0x120,
    BM_QUERYINDEX = 0x121,
    BM_QUERYHILITE = 0x122,
    BM_SETHILITE = 0x123,
    BM_QUERYCHECK = 0x124,
    BM_SETCHECK = 0x125,
    BM_SETDEFAULT = 0x126
};
static Window *group_start(Window *window)
{
    Window *first = window->parent ? window->parent->children : window;
    Window *start = first;
    for (Window *item = first; item; item = item->sibling)
    {
        if (item->style & 0x10000) /* WS_GROUP */
            start = item;
        if (item == window)
            break;
    }
    return start;
}
MRESULT buttonMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    unsigned type = window->style & 15;
    switch (message)
    {
        case BM_QUERYCHECK:
            return controlReply((LONG)c->check);
        case BM_QUERYHILITE:
            return controlReply(c->pressed);
        case BM_SETHILITE:
        {
            BOOL old = c->pressed;
            c->pressed = (USHORT)(uintptr_t)first != 0;
            controlRepaint(window);
            return controlReply(old);
        }
        case BM_SETCHECK:
        {
            unsigned old = c->check, value = (USHORT)(uintptr_t)first;
            if (type != BS_PUSH && value <= (type >= BS_THREE ? 2u : 1u))
                c->check = value;
            controlRepaint(window);
            return controlReply((LONG)old);
        }
        case BM_SETDEFAULT:
            if ((USHORT)(uintptr_t)first)
                window->style |= 0x400;
            else
                window->style &= ~0x400u;
            controlRepaint(window);
            return controlReply(TRUE);
        case BM_QUERYINDEX:
        {
            int index = 0;
            for (Window *item = group_start(window); item; item = item->sibling)
            {
                if (index && (item->style & 0x10000))
                    break;
                if (item->control && item->control->kind == 3)
                {
                    if (item->control->check)
                        return controlReply(index);
                    index++;
                }
            }
            return controlReply(-1);
        }
        case WM_QUERYDLGCODE:
            return controlReply(2 |
                                (type == BS_PUSH                            ? 0x20
                                 : type == BS_RADIO || type == BS_AUTORADIO ? 4
                                                                            : 0x40) |
                                (window->style & 0x400 ? 0x10 : 0));
        case WM_BUTTON1DOWN:
            c->pressed = TRUE;
            controlRepaint(window);
            return 0;
        case WM_BUTTON1UP:
            c->pressed = FALSE;
            controlRepaint(window);
            return 0;
        case WM_SETFOCUS:
            controlRepaint(window);
            return 0;
        case WM_CHAR:
            if (((uintptr_t)first & 0x40) ||
                !(((uintptr_t)first & 1) && (USHORT)(uintptr_t)second == ' '))
                return WinDefWindowProc(window->hwnd, message, first, second);
            break;
        case WM_BUTTON1CLICK:
        case BM_CLICK:
            break;
        default:
            return WinDefWindowProc(window->hwnd, message, first, second);
    }
    if (!window->enabled || !window->owner)
        return 0;
    if (type == BS_AUTOCHECK)
        c->check = !c->check;
    else if (type == BS_AUTOTHREE)
        c->check = (c->check + 1) % 3;
    else if (type == BS_AUTORADIO)
    {
        Window *start = group_start(window);
        for (Window *item = start; item; item = item->sibling)
        {
            if (item != start && (item->style & 0x10000))
                break;
            if (item->control && item->control->kind == 3 &&
                ((item->style & 15) == BS_RADIO || (item->style & 15) == BS_AUTORADIO))
            {
                item->control->check = item == window;
                controlRepaint(item);
            }
        }
    }
    controlRepaint(window);
    HWND owner = window->owner->hwnd;
    ULONG id = window->id, style = window->style;
    if (style & 0x100)
        WinPostMsg(owner, WM_HELP, (MPARAM)(uintptr_t)id, 0);
    else if (type == BS_PUSH)
        WinPostMsg(owner, style & 0x200 ? WM_SYSCOMMAND : WM_COMMAND, (MPARAM)(uintptr_t)id,
                   (MPARAM)(uintptr_t)1);
    else
        controlNotify(window, 1); /* BN_CLICKED */
    return 0;
}
