/* Public domain. No rights reserved. Wholly AI-authored. */
/* Combo edit/list children retain ordinary PM identities and a real popup list. */
#include "os2libs/pmwin/controls.h"
#include "os2libs/pmwin/graphics.h"
enum
{
    CBID_LIST = 0x29a,
    CBID_EDIT = 0x29b,
    CBM_SHOWLIST = 0x170,
    CBM_HILITE = 0x171,
    CBM_ISLISTSHOWING = 0x172,
    LM_SELECTITEM = 0x164
};
BOOL initializeCombo(Window *window)
{
    PmControl *c = window->control;
    ULONG style = window->style & 7;
    if (style != 1 && style != 2 && style != 4)
        return FALSE;
    HPS space = WinGetPS(window->hwnd);
    LxHostFontMetrics metrics;
    BOOL valid = space && lx_host_font_metrics(lx_pm_font(space), &metrics);
    if (space)
        WinReleasePS(space);
    if (!valid)
        return FALSE;
    LONG height = metrics.ascent + metrics.descent + 4;
    c->popup_height = window->h > height * 2 ? window->h - height : height * 6;
    c->edit = WinCreateWindow(window->hwnd, (PSZ)WC_ENTRYFIELD, window->text,
                              WS_VISIBLE | (style == 4 ? 0x20 : 0), 0, 0,
                              window->w > height ? window->w - height : 1, height, window->hwnd,
                              HWND_BOTTOM, CBID_EDIT, NULL, NULL);
    if (!c->edit)
        return FALSE;
    c->list = WinCreateWindow(style == 1 ? window->hwnd : HWND_DESKTOP, (PSZ)WC_LISTBOX, NULL,
                              style == 1 ? WS_VISIBLE : 0, 0, 0, window->w > 0 ? window->w : 1,
                              c->popup_height, window->hwnd, HWND_BOTTOM, CBID_LIST, NULL, NULL);
    if (!c->list)
        return FALSE;
    if (style != 1)
    {
        Window *list = getWindowFromHWND(getAnchorBlockNoHAB(), c->list);
        lx_host_window_border(list->heavy.window, FALSE);
    }
    return TRUE;
}
static void select_item(Window *window, BOOL notify)
{
    PmControl *c = window->control;
    Window *list = getWindowFromHWND(getAnchorBlockNoHAB(), c->list);
    if (!list || !list->control)
        return;
    PmControl *items = list->control;
    for (size_t i = 0; i < items->count; i++)
        if (items->items[i].selected)
        {
            c->updating = TRUE;
            WinSetWindowText(window->hwnd, items->items[i].text);
            WinSetWindowText(c->edit, items->items[i].text);
            c->updating = FALSE;
            if (notify)
                controlNotify(window, 4); /* CBN_LBSELECT */
            return;
        }
}
MRESULT comboMessage(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    if (message >= 0x160 && message <= 0x16f)
    {
        MRESULT result = WinSendMsg(c->list, message, first, second);
        if (message == LM_SELECTITEM && result)
            select_item(window, FALSE);
        return result;
    }
    if (message == WM_ADJUSTWINDOWPOS)
    {
        SWP position;
        if (controlRead(first, &position, sizeof position) && (window->style & 7) != 1)
        {
            SWP edit;
            if (WinQueryWindowPos(c->edit, &edit) && position.cy > edit.cy)
            {
                position.y += position.cy - edit.cy;
                position.cy = edit.cy;
                controlWrite(first, &position, sizeof position);
            }
        }
        return 0;
    }
    if (message == WM_SIZE)
    {
        LONG width = window->w > window->h ? window->w - window->h : 1;
        WinSetWindowPos(c->edit, HWND_TOP, 0, 0, width, window->h,
                        SWP_SIZE | SWP_MOVE | SWP_NOADJUST);
        return 0;
    }
    if (message == CBM_ISLISTSHOWING)
        return controlReply(c->dropped);
    if (message == CBM_HILITE)
    {
        controlRepaint(window);
        return controlReply(TRUE);
    }
    if (message == WM_BUTTON1CLICK || message == CBM_SHOWLIST)
    {
        BOOL show = message == CBM_SHOWLIST ? (USHORT)(uintptr_t)first != 0 : !c->dropped;
        if ((window->style & 7) == 1)
            return controlReply(TRUE);
        c->dropped = show;
        if (show)
        {
            LONG x = 0, y = 0;
            for (Window *w = window; w && w != &desktop_window; w = w->parent)
            {
                x += w->x;
                y += w->y;
            }
            WinSetWindowPos(c->list, HWND_TOP, x, y - c->popup_height, window->w, c->popup_height,
                            SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_NOADJUST);
        }
        else
            WinShowWindow(c->list, FALSE);
        controlNotify(window, 6);
        return controlReply(TRUE); /* CBN_SHOWLIST */
    }
    if (message == WM_CONTROL)
    {
        if (c->updating)
            return 0;
        USHORT id = (USHORT)(uintptr_t)first, code = (USHORT)((uintptr_t)first >> 16);
        if (id == CBID_EDIT && code == 4)
        {
            Window *edit = getWindowFromHWND(getAnchorBlockNoHAB(), c->edit);
            c->updating = TRUE;
            if (edit)
                WinSetWindowText(window->hwnd, edit->text ? edit->text : "");
            c->updating = FALSE;
            controlNotify(window, 1); /* CBN_EFCHANGE */
        }
        else if (id == CBID_LIST && (code == 1 || code == 5))
        {
            c->dropped = FALSE;
            if ((window->style & 7) != 1)
                WinShowWindow(c->list, FALSE);
            WinSetFocus(HWND_DESKTOP, c->edit);
            select_item(window, TRUE);
        }
        return 0;
    }
    return WinDefWindowProc(window->hwnd, message, first, second);
}
