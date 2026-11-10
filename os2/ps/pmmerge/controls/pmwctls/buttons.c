/* Public domain. No rights reserved. Wholly AI-authored. */
/* Button check state, automatic transitions and owner notifications. */
#include "os2/include/private/pmwctlp.h"

enum
{
    /* BM_SETCHECK / BM_QUERYCHECK states; the indeterminate one exists only for 3-state buttons. */
    CHECK_STATE_CHECKED = 1,
    CHECK_STATE_INDETERMINATE = 2,
    /* lx_execution_map allocates whole guest pages. */
    GUEST_PAGE_SIZE = 4096
};

void paintUserButton(Window *window, HPS space)
{
    if (!window->owner)
        return;
    ULONG state = (window->control->pressed ? BDS_HILITED : 0) |
                  (!window->enabled ? BDS_DISABLED : 0) | (window->style & BS_DEFAULT);
    uint32 data[] = {window->hwnd, space, state, window->control->button_draw_state};
    window->control->button_draw_state = state;
    Window *owner = window->owner;
    ULONG packed = (USHORT)window->id | (ULONG)BN_PAINT << 16;
    LxExecution *execution = GProcessState.execution;
    BOOL guest =
        execution && (uintptr_t)owner->window_proc <= UINT32_MAX &&
        lx_execution_memory(execution, (uint32)(uintptr_t)owner->window_proc, 1, LX_GUEST_EXECUTE);
    LxGuestMapping *mapping =
        guest ? lx_execution_map(execution, 0, GUEST_PAGE_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0)
              : NULL;
    if (guest && !mapping)
        return;
    MPARAM parameter = data;
    if (mapping)
    {
        uint8 *wire = lx_execution_mapping_host(mapping);
        for (unsigned i = 0; i < 4; i++)
            lx_guest_store_u32(wire + i * 4, data[i]);
        parameter = (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping);
    }
    WinSendMsg(owner->hwnd, WM_CONTROL, (MPARAM)(uintptr_t)packed, parameter);
    if (mapping)
        lx_execution_unmap(execution, mapping);
}

static Window *group_start(Window *window)
{
    Window *first = window->parent ? window->parent->children : window;
    Window *start = first;
    for (Window *item = first; item; item = item->sibling)
    {
        if (item->style & WS_GROUP)
            start = item;
        if (item == window)
            break;
    }
    return start;
}

MRESULT buttonMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    unsigned type = window->style & BS_PRIMARYSTYLES;
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
            if (type != BS_PUSHBUTTON &&
                value <= (type >= BS_3STATE ? (unsigned)CHECK_STATE_INDETERMINATE
                                            : (unsigned)CHECK_STATE_CHECKED))
                c->check = value;
            controlRepaint(window);
            return controlReply((LONG)old);
        }
        case BM_SETDEFAULT:
            if ((USHORT)(uintptr_t)first)
                window->style |= BS_DEFAULT;
            else
                window->style &= ~(unsigned)BS_DEFAULT;
            controlRepaint(window);
            return controlReply(TRUE);
        case BM_QUERYCHECKINDEX:
        {
            int index = 0;
            for (Window *item = group_start(window); item; item = item->sibling)
            {
                if (index && (item->style & WS_GROUP))
                    break;
                if (item->control && item->control->kind == PM_CLASS_BUTTON)
                {
                    if (item->control->check)
                        return controlReply(index);
                    index++;
                }
            }
            return controlReply(-1);
        }
        case WM_QUERYDLGCODE:
            return controlReply(DLGC_BUTTON |
                                (type == BS_PUSHBUTTON || type == BS_USERBUTTON ? DLGC_PUSHBUTTON
                                 : type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON
                                     ? DLGC_RADIOBUTTON
                                     : DLGC_CHECKBOX) |
                                (window->style & BS_DEFAULT ? DLGC_DEFAULT : 0));
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
            if (((uintptr_t)first & KC_KEYUP) ||
                !(((uintptr_t)first & KC_CHAR) && (USHORT)(uintptr_t)second == ' '))
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
    if (type == BS_AUTOCHECKBOX)
        c->check = !c->check;
    else if (type == BS_AUTO3STATE)
        c->check = (c->check + 1) % (CHECK_STATE_INDETERMINATE + 1);
    else if (type == BS_AUTORADIOBUTTON)
    {
        Window *start = group_start(window);
        for (Window *item = start; item; item = item->sibling)
        {
            if (item != start && (item->style & WS_GROUP))
                break;
            if (item->control && item->control->kind == PM_CLASS_BUTTON &&
                ((item->style & BS_PRIMARYSTYLES) == BS_RADIOBUTTON ||
                 (item->style & BS_PRIMARYSTYLES) == BS_AUTORADIOBUTTON))
            {
                item->control->check = item == window;
                controlRepaint(item);
            }
        }
    }
    controlRepaint(window);
    HWND owner = window->owner->hwnd;
    ULONG id = window->id, style = window->style;
    if (style & BS_HELP)
        WinPostMsg(owner, WM_HELP, (MPARAM)(uintptr_t)id, 0);
    else if (type == BS_PUSHBUTTON)
        WinPostMsg(owner, style & BS_SYSCOMMAND ? WM_SYSCOMMAND : WM_COMMAND, (MPARAM)(uintptr_t)id,
                   (MPARAM)(uintptr_t)CMDSRC_PUSHBUTTON);
    else
        controlNotify(window, BN_CLICKED);
    return 0;
}
