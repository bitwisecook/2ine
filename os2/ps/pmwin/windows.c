/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/launch_record.h"
#include "os2/ps/pmwin/internal.h"
#include "os2/ps/pmwin/video_frames.h"
#include "os2/ps/pmwin/graphics.h"
#include "os2/ps/pmwin/controls.h"
#include "os2/ps/pmwin/menus.h"

Window *getWindowFromHWND(const AnchorBlock *anchor, const HWND hwnd)
{
    if (hwnd >= FIRST_HWND_VALUE)
    {
        for (size_t i = 0; i < anchor->windows_array_len; i++)
        {
            Window *window = anchor->windows[i];
            if (window && window->hwnd == hwnd)
                return window;
        }
        return NULL;
    }
    else if (hwnd == HWND_DESKTOP)
    {
        return &desktop_window;
    }
    else if (hwnd == HWND_OBJECT)
    {
        return (Window *)&anchor->object_window;
    }
    return NULL;
}

WindowClass *findRegisteredClass(AnchorBlock *anchor, const char *classname)
{
    uintptr_t intclass = (uintptr_t)classname;
    if (classname && !(intclass >= 0xffff0001 && intclass <= 0xffff0070) && classname[0] == '#')
    {
        char *end;
        unsigned long number = strtoul(classname + 1, &end, 10);
        if (*end == 0 && number >= 1 && number <= 0x70)
            intclass = 0xffff0000u + number;
    }
    if ((intclass >= 0xffff0001) && (intclass <= 0xffff0070))
    {

        switch (intclass)
        {

            case WC_FRAME:
            {
                /* Public WC_FRAME words extend through QWL_PSTATBLK (0x50). */
                static WindowClass frame = {"#1", WinDefWindowProc, CS_SIZEREDRAW, 0x54, NULL};
                return &frame;
            }
            case WC_COMBOBOX:
            {
                static WindowClass combo = {"#2", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &combo;
            }
            case WC_BUTTON:
            {
                static WindowClass button = {"#3", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &button;
            }
            case WC_MENU:
            {
                static WindowClass menu = {"#4", menuProcedure, CS_SIZEREDRAW, 0, NULL};
                return &menu;
            }
            case WC_STATIC:
            {
                static WindowClass text = {"#5", controlProcedure, CS_SIZEREDRAW | CS_HITTEST, 4,
                                           NULL};
                return &text;
            }
            case WC_ENTRYFIELD:
            {
                static WindowClass entry = {"#6", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &entry;
            }
            case WC_LISTBOX:
            {
                static WindowClass list = {"#7", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &list;
            }
            case WC_SCROLLBAR:
            {
                static WindowClass scrollbar = {"#8", controlProcedure, CS_SIZEREDRAW, 0, NULL};
                return &scrollbar;
            }
            case WC_TITLEBAR:
            {
                static WindowClass title = {"#9", frameControlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &title;
            }
            case WC_APPSTAT:
            case WC_KBDSTAT:
            case WC_PECIC:
            case WC_DBE_KKPOPUP:
                return NULL;
            case WC_MLE:
            {
                static WindowClass mle = {"#10", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &mle;
            }
            case WC_SPINBUTTON:
            {
                static WindowClass spin = {"#32", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &spin;
            }
            case WC_CONTAINER:
            case WC_SLIDER:
            case WC_VALUESET:
            case WC_NOTEBOOK:
            case WC_CIRCULARSLIDER:
            default:
                return NULL;
        }
    }
    else
    {
        for (WindowClass *i = anchor->registered_classes; i; i = i->next)
        {
            if (strcmp(i->name, classname) == 0)
            {
                return i;
            }
        }
    }
    return NULL;
}

void destroyWindow(AnchorBlock *anchor, Window *win)
{
    if (!win || (win->hwnd == NULLHANDLE) || win->destroying)
    {
        return;
    }

    unregisterWindowMessages(win->hwnd);
    lx_pm_clipboard_window_destroyed(win->hwnd);
    lx_pm_dive_forget(win);
    lx_pm_video_forget(win);
    win->destroying = TRUE;
    BOOL was_visible = (win->style & WS_VISIBLE) != 0;
    RECTL exposed = {win->x, win->y, win->x + win->w, win->y + win->h};
    if (anchor->focus == win->hwnd)
        WinSetFocus(HWND_DESKTOP, 0);
    if (anchor->active == win->hwnd)
        anchor->active = 0;
    if (anchor->capture == win->hwnd)
        anchor->capture = 0;
    win->style &= ~WS_VISIBLE;
    if (win->parent == &desktop_window)
        lx_host_window_hide(win->heavy.window);
    for (PmTimer **link = &anchor->timers; *link;)
    {
        PmTimer *timer = *link;
        if (timer->hwnd == win->hwnd)
        {
            *link = timer->next;
            free(timer);
        }
        else
            link = &timer->next;
    }
    /* WM_DESTROY precedes destruction of children; their handles remain queryable. */
    sendMessage(win, WM_DESTROY, 0, 0);
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd && anchor->windows[i]->parent == win)
            destroyWindow(anchor, anchor->windows[i]);
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->owner == win)
            destroyWindow(anchor, anchor->windows[i]);
    for (size_t i = 0; i < anchor->pres_spaces_array_len; i++)
    {
        PresentationSpace *ps = &anchor->pres_spaces[i];
        if (ps->window != win)
            continue;
        if (ps->persistent)
            lx_pm_associate_dc(ps->hps, 0);
        else
            destroyPresentationSpace(anchor, ps);
    }
    free(win->damage_rects);
    win->damage_rects = NULL;
    win->damage_count = 0;
    releaseWindowAccelerator(win);
    destroyDialog(win);
    destroyControl(win);
    destroyMenu(win);
    destroyStandardFile(win);
    destroyStandardFont(win);

    if (win->parent == &desktop_window)
    {
        lx_host_window_destroy(win->heavy.window);
    }

    if (win->parent->children == win)
    {
        win->parent->children = win->sibling;
    }
    else
    {
        for (Window *i = win->parent->children; i != NULL; i = i->sibling)
        {
            assert(i != win);
            if (i->sibling == win)
            {
                i->sibling = win->sibling;
                break;
            }
        }
    }
    discardWindowMessages(anchor, win->hwnd);
    if (was_visible && win->parent != &desktop_window && !win->parent->destroying)
        WinInvalidateRect(win->parent->hwnd, &exposed, TRUE);
    destroyPresentationParameters(win);
    free(win->text);
    free(win->data);
    memset(win, '\0', sizeof(*win));
}

BOOL WinRegisterClass(HAB hab, PSZ pszClassName, PFNWP pfnWndProc, ULONG flStyle,
                      ULONG cbWindowData)
{
    TRACE_NATIVE("WinRegisterClass(%u, '%s', %p, %u, %u)", (uint)hab, pszClassName, pfnWndProc,
                 (uint)flStyle, (uint)cbWindowData);

    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return FALSE;
    }

    WindowClass *winclass = findRegisteredClass(anchor, pszClassName);
    if (!winclass)
    { // PM docs say this call can replace existing class.
        winclass = (WindowClass *)malloc(sizeof(WindowClass));
        if (!winclass)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, FALSE);
        }
        else if ((winclass->name = strdup(pszClassName)) == NULL)
        {
            free(winclass);
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, FALSE);
        }
        winclass->next = anchor->registered_classes;
        anchor->registered_classes = winclass;
    }

    winclass->window_proc = pfnWndProc;
    winclass->style = flStyle;
    winclass->data_len = cbWindowData;

    return TRUE;
}

HWND WinCreateWindow(HWND hwndParent, PSZ pszClass, PSZ pszName, ULONG flStyle, LONG x, LONG y,
                     LONG cx, LONG cy, HWND hwndOwner, HWND hwndInsertBehind, ULONG id,
                     PVOID pCtlData, PVOID pPresParams)
{
    TRACE_NATIVE("WinCreateWindow(%u, %p, '%s', %u, %d, %d, %d, %d, %u, %u, %u, %p, %p)",
                 (uint)hwndParent, pszClass, pszName, (uint)flStyle, (int)x, (int)y, (int)cx,
                 (int)cy, (uint)hwndOwner, hwndInsertBehind, (uint)id, pCtlData, pPresParams);

    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }

    if (anchor->message_queue.hmq == NULLHANDLE)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_NO_MSG_QUEUE, NULLHANDLE);
    }

    const WindowClass *winclass = findRegisteredClass(anchor, pszClass);
    if (!winclass)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_PARM, NULLHANDLE);
    }

    Window *parent = getWindowFromHWND(anchor, hwndParent);
    if (!parent || parent->destroying)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
    }

    Window *owner = NULL;
    if (hwndOwner != NULLHANDLE)
    {
        owner = getWindowFromHWND(anchor, hwndOwner);
        if (!owner || owner->destroying)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
        }
    }

    Window *win = NULL;
    FIXME("optimize this");
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        if (!anchor->windows[i] || anchor->windows[i]->hwnd == NULLHANDLE)
        {
            if (!anchor->windows[i])
                anchor->windows[i] = calloc(1, sizeof(Window));
            win = anchor->windows[i];
            break;
        }
    }

    if (!win)
    {
        const size_t len = anchor->windows_array_len;
        void *ptr = realloc(anchor->windows, sizeof *anchor->windows * len * 2);
        if (!ptr)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
        }
        anchor->windows = (Window **)ptr;
        memset(&anchor->windows[len], '\0', sizeof *anchor->windows * len);
        win = anchor->windows[len] = calloc(1, sizeof(Window));
        anchor->windows_array_len *= 2;
    }

    if (!win)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
    }
    char *text = NULL;
    if (pszName)
    {

        text = strdup(pszName);
        if (!text)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
        }
    }

    void *data = NULL;
    if (winclass->data_len > 0)
    {
        data = calloc(1, winclass->data_len);
        if (!data)
        {
            free(text);
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
        }
    }

    if (hwndParent == HWND_DESKTOP)
    {
        win->heavy.window = lx_host_window_create(text, x, (desktop_window.h - y) - cy,
                                                  cx > 0 ? cx : 1, cy > 0 ? cy : 1, win);
        if (!win->heavy.window)
        {
            free(text);
            free(data);
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INTERNAL_ERROR_1, NULLHANDLE);
        }
    }
    else
    {
        win->heavy.window = parent->heavy.window;
    }
    win->parent = parent;
    win->children = NULL;
    win->hwnd = registerWindowMessages(anchor);
    if (win->hwnd)
        bindWindowTarget(win);
    if (!win->hwnd)
    {
        if (parent == &desktop_window)
            lx_host_window_destroy(win->heavy.window);
        free(text);
        free(data);
        return 0;
    }
    win->visible_region_notify = FALSE;
    win->enabled = (flStyle & WS_DISABLED) == 0;
    win->window_class_name = winclass->name; // this pointer lives until Anchor Block terminates.
    win->data = data;
    win->text = text;
    win->data_len = winclass->data_len;
    win->window_proc = winclass->window_proc;
    win->class_style = winclass->style;
    win->style = flStyle;
    win->id = id;
    win->owner = owner;
    win->sibling = NULL;
    win->x = x;
    win->y = y;
    win->w = cx;
    win->h = cy;
    applyHostWindowChrome(win);

    Window *insertBehind = NULL;
    if ((hwndInsertBehind != HWND_TOP) && (hwndInsertBehind != HWND_BOTTOM))
    {
        insertBehind = getWindowFromHWND(anchor, hwndInsertBehind);
        if (!insertBehind || (insertBehind->parent != parent))
        {
            FIXME("should we fail or just insert at top/bottom?");
            hwndInsertBehind = HWND_BOTTOM;
            insertBehind = NULL;
        }
    }

    if (parent->children == NULL)
    {
        parent->children = win;
    }
    else if (hwndInsertBehind == HWND_TOP)
    {
        win->sibling = parent->children;
        parent->children = win;
    }
    else if (hwndInsertBehind == HWND_BOTTOM)
    {
        Window *prev = NULL;
        for (Window *i = parent->children; i != NULL; i = i->sibling)
        {
            prev = i;
        }
        prev->sibling = win;
    }
    else
    {
        win->sibling = insertBehind->sibling;
        insertBehind->sibling = win;
    }

    CREATESTRUCT creation = {pPresParams, pCtlData, id,      hwndInsertBehind, hwndOwner, cy, cx, y,
                             x,           flStyle,  pszName, pszClass,         hwndParent};
    if (sendCreateMessage(win, &creation) != FALSE)
    {
        destroyWindow(anchor, win);
        return NULLHANDLE;
    }

    FIXME("should I set swp.fl here?");
    SWP swp = {0, cy, cx, y, x, hwndInsertBehind, win->hwnd, 0, 0};
    MRESULT adjust = sendWindowPos(win, WM_ADJUSTWINDOWPOS, &swp);

    (void)adjust;
    FIXME("adjust based on WM_ADJUSTWINDOWPOS message result");

    win->x = swp.x;
    win->y = swp.y;
    win->w = swp.cx;
    win->h = swp.cy;
    hwndInsertBehind = swp.hwndInsertBehind;

    if (hwndParent == HWND_DESKTOP && win->w > 0 && win->h > 0)
    {
        updateWindowCyclePolicy(win);
        lx_host_window_position(win->heavy.window, win->x, (parent->h - win->y) - win->h, win->w,
                                win->h);
        if (win->style & WS_VISIBLE)
        {

            lx_host_window_show(win->heavy.window);
        }
    }

    if (win->style & WS_VISIBLE)
        postExposedMessages(anchor, win, currentSystemTicks());
    return win->hwnd;
}

HWND WinCreateStdWindow(HWND hwndParent, ULONG flStyle, PULONG pflCreateFlags, PSZ pszClientClass,
                        PSZ pszTitle, ULONG styleClient, HMODULE hmod, ULONG idResources,
                        PHWND phwndClient)
{
    TRACE_NATIVE("WinCreateStdWindow(%u, %u, %p, '%s', '%s', %u, %u, %u, %p)", (uint)hwndParent,
                 (uint)flStyle, pflCreateFlags, pszClientClass, pszTitle, (uint)styleClient,
                 (uint)hmod, (uint)idResources, phwndClient);

    if (phwndClient)
    {
        *phwndClient = NULLHANDLE;
    }

    FRAMECDATA fcd = {sizeof(FRAMECDATA), *pflCreateFlags, hmod, idResources};
    HWND retval = WinCreateWindow(hwndParent, (PSZ)WC_FRAME, pszTitle, flStyle, 0, 0, 0, 0,
                                  NULLHANDLE, HWND_TOP, idResources, &fcd, NULL);
    if (!retval)
    {
        return NULLHANDLE;
    }

    if (pszClientClass)
    {
        HWND clientwin = WinCreateWindow(retval, pszClientClass, NULL, styleClient | WS_VISIBLE, 0,
                                         0, 0, 0, retval, HWND_BOTTOM, FID_CLIENT, NULL, NULL);
        if (!clientwin)
        {
            WinDestroyWindow(retval);
            return NULLHANDLE;
        }
        AnchorBlock *anchor = getAnchorBlockNoHAB();
        Window *frame = getWindowFromHWND(anchor, retval);
        frame->last_focus = clientwin;
        if (phwndClient)
        {
            *phwndClient = clientwin;
        }
    }

    /* A frame created visible never passes through a show request of its own. */
    if ((flStyle & WS_VISIBLE) && lx_launch_presentation_pending())
        WinSetWindowPos(retval, HWND_TOP, 0, 0, 0, 0, SWP_SHOW);

    return retval;
}

BOOL WinDestroyWindow(HWND hwnd)
{
    TRACE_NATIVE("WinDestroyWindow(%u)", (uint)hwnd);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return FALSE;
    }

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win || win == &desktop_window || win == &anchor->object_window)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    destroyWindow(anchor, win);

    return TRUE;
}

MRESULT WinDefWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    TRACE_NATIVE("WinDefWindowProc(%u, %u, %p, %p)", (uint)hwnd, (uint)msg, mp1, mp2);

    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return (MRESULT)0;
    }
    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win)
    {
        return (MRESULT)0;
    }
    if (frameTrackMessage(win, msg, mp1))
        return 0;

    if (win->window_class_name && !strcmp(win->window_class_name, "#1") &&
        (msg == WM_COMMAND || msg == WM_HELP || msg == WM_INITMENU || msg == WM_MENUSELECT ||
         msg == WM_MENUEND || msg >= WM_USER))
    {
        HWND client = WinWindowFromID(hwnd, FID_CLIENT);
        if (client)
            return WinSendMsg(client, msg, mp1, mp2);
    }
    switch (msg)
    {
        case WM_FLASHWINDOW:
            win->flash = (USHORT)(uintptr_t)mp1 != 0;
            win->flash_count = 0;
            WinInvalidateRect(hwnd, NULL, TRUE);
            return (MRESULT)(uintptr_t)TRUE;
        case WM_ACTIVATE:
            if (isFrameWindow(win))
                for (Window *control = win->children; control; control = control->sibling)
                    if (control->id == FID_TITLEBAR || control->id == FID_SYSMENU ||
                        control->id == FID_MINMAX)
                        WinInvalidateRect(control->hwnd, NULL, FALSE);
            for (Window *child = win->children; child; child = child->sibling)
                if (child->id == FID_CLIENT)
                    return sendMessage(child, msg, mp1, mp2);
            return 0;
        case WM_SYSCOMMAND:
            return frameSystemCommand(win, (USHORT)(uintptr_t)mp1);
        case WM_BUTTON1DBLCLK:
            if (isFrameWindow(win) && (win->style & WS_MINIMIZED))
                return frameSystemCommand(win, 0x8008); /* SC_RESTORE */
            return 0;
        case WM_CREATE:
            if (mp1 && win->window_class_name && !strcmp(win->window_class_name, "#1"))
            {
                FRAMECDATA data;
                if (!controlRead(mp1, &data, sizeof data) || data.cb != sizeof data)
                    return (MRESULT)(uintptr_t)TRUE;
                if (!WinCreateFrameControls(hwnd, &data, win->text))
                    return (MRESULT)(uintptr_t)TRUE;
            }
            return 0;
        case WM_SETICON:
            /* Null removes the application icon; non-null icon resources are unsupported. */
            return (MRESULT)(uintptr_t)(mp1 == NULL);
        case WM_CLOSE:
        {
            AnchorBlock *anchor = getAnchorBlockNoHAB();
            Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
            if (!win)
                return 0;
            for (Window *child = win->children; child; child = child->sibling)
                if (child->id == FID_CLIENT)
                    return sendMessage(child, WM_CLOSE, 0, 0);
            WinPostMsg(hwnd, WM_QUIT, 0, 0);
            return 0;
        }
        case WM_APPTERMINATENOTIFY:
        case WM_ADJUSTWINDOWPOS:
        case WM_COMMAND:
        case WM_CONTROL:
        case WM_CTLCOLORCHANGE:
        case WM_DESTROY:
        case WM_DRAWITEM:
        case WM_ENABLE:
        case WM_ERASEBACKGROUND:
        case WM_ERROR:
        case WM_HSCROLL:
        case WM_INITDLG:
        case WM_INITMENU:
        case WM_JOURNALNOTIFY:
        case WM_MATCHMNEMONIC:
        case WM_MEASUREITEM:
        case WM_MENUEND:
        case WM_MINMAXFRAME:
        case WM_MOUSEMAP:
        case WM_MOVE:
        case WM_NEXTMENU:
        case WM_NULL:
        case WM_PCONTROL:
        case WM_PRESPARAMCHANGED:
        case WM_PSETFOCUS:
        case WM_PSIZE:
        case WM_PSYSCOLORCHANGE:
        case WM_QUERYACCELTABLE:
        case WM_QUIT:
        case WM_SAVEAPPLICATION:
        case WM_SEM1:
        case WM_SEM2:
        case WM_SEM3:
        case WM_SEM4:
        case WM_SETACCELTABLE:
        case WM_SETFOCUS:
        case WM_SETHELPINFO:
        case WM_SETSELECTION:
        case WM_SETWINDOWPARAMS:
        case WM_SHOW:
        case WM_SIZE:
        case WM_SUBSTITUTESTRING:
        case WM_SYSCOLORCHANGE:
        case WM_SYSVALUECHANGED:
        case WM_TIMER:
        case WM_TRACKFRAME:
        case WM_TRANSLATEACCEL:

        case WM_VRNDISABLED:
        case WM_VRNENABLED:
        case WM_VSCROLL:

        case WM_MSGBOXINIT:
        case WM_MSGBOXDISMISS:

            return (MRESULT)0;

        case WM_MENUSELECT:
            return (MRESULT)1;

        case WM_BEGINDRAG:
        case WM_BEGINSELECT:
        case WM_BUTTON1CLICK:
        case WM_BUTTON1MOTIONSTART:
        case WM_BUTTON1MOTIONEND:
        case WM_BUTTON1UP:
        case WM_BUTTON2CLICK:
        case WM_BUTTON2DBLCLK:
        case WM_BUTTON2MOTIONSTART:
        case WM_BUTTON2MOTIONEND:
        case WM_BUTTON2UP:
        case WM_BUTTON3CLICK:
        case WM_BUTTON3DBLCLK:
        case WM_BUTTON3MOTIONSTART:
        case WM_BUTTON3MOTIONEND:
        case WM_BUTTON3UP:
        case WM_CHAR:
        case WM_CHORD:
        case WM_CONTEXTMENU:
        case WM_ENDDRAG:
        case WM_ENDSELECT:
        case WM_OPEN:
        case WM_PACTIVATE:
        case WM_QUERYCTLTYPE:
        case WM_QUERYHELPINFO:
        case WM_QUERYTRACKINFO:
        case WM_SINGLESELECT:
        case WM_TEXTEDIT:
            return win->owner ? sendMessage(win->owner, msg, mp1, mp2) : (MRESULT)0;

        case WM_HITTEST:
            return (MRESULT)(uintptr_t)HT_NORMAL;

        case WM_PAINT:
            if (isFrameWindow(win))
            {
                HPS space = WinBeginPaint(hwnd, 0, NULL);
                if (space)
                {
                    paintFrameBorder(win, space);
                    WinEndPaint(space);
                }
                /* WM_ERASEBACKGROUND: TRUE asks the frame to fill its client. */
                Window *client = getWindowFromHWND(anchor, WinWindowFromID(hwnd, 0x8008));
                HPS client_space = client ? WinGetPS(client->hwnd) : 0;
                if (client_space)
                {
                    RECTL area = {0, 0, client->w, client->h};
                    if (sendEraseBackground(client, client_space, &area))
                        WinFillRect(client_space, &area, -20); /* SYSCLR_WINDOW */
                    WinReleasePS(client_space);
                }
                return 0;
            }
            validateWindowPaint(anchor, win);
            return 0;

        case WM_QUERYBORDERSIZE:
        {
            POINTL border = frameBorderSize(win);
            return (MRESULT)(uintptr_t)(isFrameWindow(win) && !mp2 && mp1 &&
                                        controlWrite(mp1, &border, sizeof border));
        }
        case WM_QUERYFRAMEINFO:
            return (MRESULT)(uintptr_t)(isFrameWindow(win) ? 1u | 4u : 0u);
        case WM_CALCFRAMERECT:
        {
            RECTL rect;
            BOOL valid = isFrameWindow(win) && controlRead(mp1, &rect, sizeof rect) &&
                         frameClientRect(win, &rect, (USHORT)(uintptr_t)mp2 != 0) &&
                         controlWrite(mp1, &rect, sizeof rect);
            return (MRESULT)(uintptr_t)valid;
        }
        case WM_QUERYFRAMECTLCOUNT:
            return (MRESULT)(uintptr_t)(isFrameWindow(win) ? frameControlCount(win) : 0);
        case WM_FORMATFRAME:
            return (MRESULT)(uintptr_t)frameFormatPositions(win, mp1, mp2);
        case WM_UPDATEFRAME:
            if (isFrameWindow(win))
                formatFrame(win);
            return 0;

        case WM_BUTTON1DOWN:
            WinSetActiveWindow(HWND_DESKTOP, topLevelWindow(anchor, hwnd));
            return 0;
        case WM_BUTTON2DOWN:
        case WM_BUTTON3DOWN:
        case WM_CALCVALIDRECTS:
        case WM_FOCUSCHANGE:
        case WM_HELP:
            return 0;
        case WM_CONTROLPOINTER:
            return (MRESULT)mp2;
        case WM_MOUSEMOVE:
        {
            LONG shape = win->control && (win->control->kind == (USHORT)WC_ENTRYFIELD ||
                                          win->control->kind == (USHORT)WC_MLE)
                             ? 2  /* SPTR_TEXT */
                             : 1; /* SPTR_ARROW */
            HPOINTER pointer = WinQuerySysPointer(HWND_DESKTOP, shape, FALSE);
            if (!pointer)
                return 0;
            if (win->control && win->owner)
                pointer = (HPOINTER)(uintptr_t)sendMessage(win->owner, WM_CONTROLPOINTER,
                                                           (MPARAM)(uintptr_t)(USHORT)win->id,
                                                           (MPARAM)(uintptr_t)pointer);
            WinSetPointer(HWND_DESKTOP, pointer);
            return (MRESULT)(uintptr_t)TRUE;
        }
        case WM_PPAINT:
        case WM_QUERYCONVERTPOS:
        case WM_QUERYWINDOWPARAMS:
        case WM_REALIZEPALETTE:
        case WM_WINDOWPOSCHANGED:
            TRACE_EVENT("Default window message %u for %u", (uint)msg, (uint)hwnd);
            break;

        default:
            TRACE_EVENT("Default window message %u for %u", (uint)msg, (uint)hwnd);
            break;
    }

    return (MRESULT)0;
}

BOOL WinIsWindow(HAB hab, HWND hwnd)
{
    TRACE_NATIVE("WinIsWindow(%u, %u)", (uint)hab, (uint)hwnd);
    FIXME("this 'get the anchor, get the window, set errors' code is repeated a lot");
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return FALSE;

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win && !windowMessageTargetExists(hwnd))
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    return TRUE;
}

BOOL WinIsWindowEnabled(HWND hwnd)
{
    TRACE_NATIVE("WinIsWindowEnabled(%u)", (uint)hwnd);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return FALSE;

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    return win->enabled;
}

BOOL WinIsWindowVisible(HWND hwnd)
{
    TRACE_NATIVE("WinIsWindowVisible(%u)", (uint)hwnd);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return FALSE;

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    if (win == &desktop_window)
        return TRUE;
    return ((win->style & WS_VISIBLE) == WS_VISIBLE) ? TRUE : FALSE;
}

static BOOL set_window_order(AnchorBlock *anchor, Window *window, HWND behind)
{
    if (!window->parent)
        return FALSE;
    if (behind == window->hwnd)
        return TRUE;
    Window *previous = NULL;
    if (behind != HWND_TOP && behind != HWND_BOTTOM)
    {
        previous = getWindowFromHWND(anchor, behind);
        if (!previous || previous->parent != window->parent || previous->destroying)
            return FALSE;
    }
    Window **slot = &window->parent->children;
    while (*slot && *slot != window)
        slot = &(*slot)->sibling;
    if (!*slot)
        return FALSE;
    *slot = window->sibling;
    if (behind == HWND_BOTTOM)
    {
        slot = &window->parent->children;
        while (*slot)
            slot = &(*slot)->sibling;
    }
    else
        slot = previous ? &previous->sibling : &window->parent->children;
    window->sibling = *slot;
    *slot = window;
    return TRUE;
}

/* A launcher's show state and placement apply to the first frame shown. */
static void applyLaunchPresentation(const Window *win, LONG *x, LONG *y, LONG *cx, LONG *cy,
                                    ULONG *flags)
{
    LxLaunchPresentation launch;
    if (!(*flags & SWP_SHOW) || win->parent != &desktop_window || !isFrameWindow(win) ||
        !lx_launch_take_presentation(&launch))
        return;
    if (launch.control & LX_LAUNCH_INVISIBLE)
    {
        *flags = (*flags & ~(ULONG)(SWP_SHOW | SWP_ACTIVATE)) | SWP_HIDE;
        return;
    }
    if (launch.control & LX_LAUNCH_SET_POSITION)
    {
        *x = launch.x;
        *y = launch.y;
        *flags |= SWP_MOVE;
    }
    if ((launch.control & LX_LAUNCH_SET_SIZE) && launch.cx > 0 && launch.cy > 0)
    {
        *cx = launch.cx;
        *cy = launch.cy;
        *flags |= SWP_SIZE;
    }
    if (launch.control & LX_LAUNCH_MINIMIZE)
        *flags |= SWP_MINIMIZE;
    else if (launch.control & LX_LAUNCH_MAXIMIZE)
        *flags |= SWP_MAXIMIZE;
    if (launch.control & LX_LAUNCH_BACKGROUND)
        *flags &= ~(ULONG)SWP_ACTIVATE;
}

BOOL WinSetWindowPos(HWND hwnd, HWND behind, LONG x, LONG y, LONG cx, LONG cy, ULONG flags)
{
    TRACE_NATIVE("WinSetWindowPos(%u, %u, %d, %d, %d, %d, %u)", (uint)hwnd, (uint)behind, (int)x,
                 (int)y, (int)cx, (int)cy, (uint)flags);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
    if (win)
        applyLaunchPresentation(win, &x, &y, &cx, &cy, &flags);
    if (!win || ((flags & SWP_SIZE) && (cx <= 0 || cy <= 0)))
        return FALSE;
    SWP swp = {flags,
               (flags & SWP_SIZE) ? cy : win->h,
               (flags & SWP_SIZE) ? cx : win->w,
               (flags & SWP_MOVE) ? y : win->y,
               (flags & SWP_MOVE) ? x : win->x,
               behind,
               hwnd,
               0,
               0};
    if (!prepareFrameState(win, &swp))
        return FALSE;
    flags = swp.fl;
    if (frameControlSuppressed(win))
    {
        if (!win->frame_chrome_hidden)
            win->frame_chrome_visible = (win->style & WS_VISIBLE) != 0;
        if ((flags & SWP_SHOW) && !win->parent->formatting_frame)
            win->frame_chrome_visible = TRUE;
        if ((flags & SWP_HIDE) && !win->parent->formatting_frame)
            win->frame_chrome_visible = FALSE;
        win->frame_chrome_hidden = TRUE;
        flags = (flags & ~SWP_SHOW) | SWP_HIDE;
    }
    else if (win->frame_chrome_hidden)
    {
        if (!(flags & (SWP_SHOW | SWP_HIDE)))
            flags |= win->frame_chrome_visible ? SWP_SHOW : SWP_HIDE;
        win->frame_chrome_hidden = FALSE;
    }
    swp.fl = flags;
    if (!(flags & SWP_NOADJUST))
        sendWindowPos(win, WM_ADJUSTWINDOWPOS, &swp);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    flags = swp.fl;
    if ((flags & SWP_ZORDER) && !set_window_order(anchor, win, swp.hwndInsertBehind))
        return FALSE;
    LONG oldw = win->w, oldh = win->h;
    RECTL exposed = {win->x, win->y, win->x + oldw, win->y + oldh};
    win->x = swp.x;
    win->y = swp.y;
    win->w = swp.cx;
    win->h = swp.cy;
    BOOL was_visible = (win->style & WS_VISIBLE) != 0;
    if (flags & SWP_SHOW)
        win->style |= WS_VISIBLE;
    if (flags & SWP_HIDE)
        win->style &= ~WS_VISIBLE;
    if (win->parent == &desktop_window)
    {
        updateWindowCyclePolicy(win);
        /* The host refuses positions above the usable display area, for instance a placement
           saved on a taller screen. PM must record where the window actually lands. */
        int host_y = desktop_window.h - win->y - win->h;
        lx_host_window_constrain(win->heavy.window, win->x, &host_y, win->w, win->h);
        win->y = desktop_window.h - host_y - win->h;
        if (!lx_host_window_position(win->heavy.window, win->x, host_y, win->w, win->h))
            return FALSE;
        if ((win->style & WS_VISIBLE) && !(win->style & WS_MINIMIZED))
            lx_host_window_show(win->heavy.window);
        else if (!(win->style & WS_VISIBLE))
            lx_host_window_hide(win->heavy.window);
    }
    if (was_visible != ((win->style & WS_VISIBLE) != 0))
        sendMessage(win, WM_SHOW, (MPARAM)(uintptr_t)((win->style & WS_VISIBLE) != 0), 0);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    if (was_visible && win->parent != &desktop_window && !(flags & SWP_NOREDRAW) &&
        ((flags & SWP_HIDE) || win->x != exposed.xLeft || win->y != exposed.yBottom ||
         win->w != oldw || win->h != oldh))
        WinInvalidateRect(win->parent->hwnd, &exposed, TRUE);
    if (oldw != win->w || oldh != win->h)
    {
        sendMessage(win, WM_SIZE, (MPARAM)(uintptr_t)(((uint32)(uint16)oldh << 16) | (uint16)oldw),
                    (MPARAM)(uintptr_t)(((uint32)(uint16)win->h << 16) | (uint16)win->w));
        win = getWindowFromHWND(anchor, hwnd);
        if (!win || win->destroying)
            return FALSE;
        if (isFrameWindow(win))
            formatFrame(win);
        HWND menu = isFrameWindow(win) ? 0 : WinWindowFromID(win->hwnd, FID_MENU);
        LONG menu_height = menu ? lx_pm_display_scale(24) : 0;
        if (menu && win->h > menu_height)
            WinSetWindowPos(menu, HWND_TOP, 0, win->h - menu_height, win->w, menu_height,
                            SWP_MOVE | SWP_SIZE | SWP_SHOW);
        win = getWindowFromHWND(anchor, hwnd);
        if (!win || win->destroying)
            return FALSE;
        for (Window *child = win->children; child; child = child->sibling)
            if (!isFrameWindow(win) && child->id == FID_CLIENT && win->h > menu_height)
                WinSetWindowPos(child->hwnd, HWND_BOTTOM, 0, 0, win->w, win->h - menu_height,
                                SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    else if (isFrameWindow(win) && (flags & (SWP_MINIMIZE | SWP_MAXIMIZE | SWP_RESTORE)))
        formatFrame(win);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    if (oldw != win->w || oldh != win->h)
        for (Window *child = win->children; child; child = child->sibling)
            if (child->style & WS_MAXIMIZED)
                WinSetWindowPos(child->hwnd, HWND_TOP, 0, 0, win->w, win->h,
                                SWP_MOVE | SWP_SIZE | SWP_NOADJUST);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    if (flags & (SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_HIDE))
        notifyVisibleRegions(anchor, win);
    if (!(flags & SWP_NOREDRAW) && (flags & (SWP_SIZE | SWP_SHOW | SWP_MOVE)))
        postExposedMessages(anchor, win, currentSystemTicks());
    if ((flags & SWP_ZORDER) && !(flags & SWP_NOREDRAW))
        WinInvalidateRect(win->parent->hwnd, NULL, TRUE);
    if ((flags & (SWP_ZORDER | SWP_ACTIVATE)) == (SWP_ZORDER | SWP_ACTIVATE) &&
        swp.hwndInsertBehind == HWND_TOP && win->parent == &desktop_window)
        lx_host_window_focus(win->heavy.window);
    if (flags & SWP_ACTIVATE)
        WinSetActiveWindow(HWND_DESKTOP, topLevelWindow(anchor, hwnd));
    return TRUE;
}

BOOL WinQueryWindowPos(HWND hwnd, PSWP position)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
    if (!win || !position)
        return FALSE;
    *position = (SWP){((win->style & WS_VISIBLE) ? SWP_SHOW : SWP_HIDE) |
                          ((win->style & WS_MAXIMIZED) ? SWP_MAXIMIZE : 0) |
                          ((win->style & WS_MINIMIZED) ? SWP_MINIMIZE : 0),
                      win->h,
                      win->w,
                      win->y,
                      win->x,
                      HWND_TOP,
                      hwnd,
                      0,
                      0};
    return TRUE;
}

BOOL WinQueryWindowRect(HWND hwnd, PRECTL rect)
{
    SWP position;
    if (!rect || !WinQueryWindowPos(hwnd, &position))
        return FALSE;
    *rect = (RECTL){0, 0, position.cx, position.cy};
    return TRUE;
}

BOOL WinQueryPointerPos(HWND hwnd, PPOINTL point)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
    if (!win || !point)
        return FALSE;
    int x, y;
    lx_host_mouse_position(&x, &y);
    y = desktop_window.h - 1 - y;
    for (Window *w = win; w && w != &desktop_window; w = w->parent)
    {
        x -= w->x;
        y -= w->y;
    }
    point->x = x;
    point->y = y;
    return TRUE;
}

BOOL WinShowWindow(HWND window, BOOL show)
{
    return WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, show ? SWP_SHOW : SWP_HIDE);
}

static void setDrawable(Window *window, LxHostWindow *drawable)
{
    window->heavy.window = drawable;
    for (Window *child = window->children; child; child = child->sibling)
        setDrawable(child, drawable);
}

BOOL WinSetParent(HWND handle, HWND parent_handle, BOOL redraw)
{
    TRACE_NATIVE("WinSetParent(%u, %u, %u)", (uint)handle, (uint)parent_handle, (uint)redraw);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    Window *parent = anchor ? getWindowFromHWND(anchor, parent_handle) : NULL;
    if (!window || !parent || !window->parent || window->destroying || parent->destroying)
        return FALSE;
    for (Window *ancestor = parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == window)
            return FALSE;
    Window *old_parent = window->parent;
    if (old_parent == parent)
        return TRUE;
    LxHostWindow *drawable = parent->heavy.window;
    if (parent == &desktop_window)
    {
        drawable = lx_host_window_create(
            window->text, window->x, desktop_window.h - window->y - window->h,
            window->w > 0 ? window->w : 1, window->h > 0 ? window->h : 1, window);
        if (!drawable)
            return FALSE;
    }
    Window **link = &old_parent->children;
    while (*link && *link != window)
        link = &(*link)->sibling;
    if (!*link)
    {
        if (parent == &desktop_window)
            lx_host_window_destroy(drawable);
        return FALSE;
    }
    *link = window->sibling;
    window->sibling = parent->children;
    parent->children = window;
    LxHostWindow *old_drawable = old_parent == &desktop_window ? window->heavy.window : NULL;
    window->parent = parent;
    setDrawable(window, drawable);
    updateWindowCyclePolicy(old_parent);
    applyHostWindowChrome(window);
    updateWindowCyclePolicy(window);
    if (isFrameWindow(window))
        formatFrame(window);
    if (old_drawable)
        lx_host_window_destroy(old_drawable);
    if (parent == &desktop_window && (window->style & WS_VISIBLE))
        lx_host_window_show(drawable);
    notifyVisibleRegions(anchor, window);
    if (redraw && (window->style & WS_VISIBLE))
    {
        if (old_parent != &desktop_window && old_parent != &anchor->object_window)
            WinInvalidateRect(old_parent->hwnd, NULL, TRUE);
        WinInvalidateRect(handle, NULL, TRUE);
    }
    return TRUE;
}

BOOL WinEnableWindow(HWND window, BOOL enable)
{
    TRACE_NATIVE("WinEnableWindow(%u, %u)", (uint)window, (uint)enable);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w)
        return FALSE;
    BOOL was_enabled = w->enabled;
    w->enabled = enable != 0;
    if (enable)
        w->style &= ~WS_DISABLED;
    else
        w->style |= WS_DISABLED;
    updateWindowCyclePolicy(w);
    if (was_enabled != w->enabled)
        sendMessage(w, WM_ENABLE, (MPARAM)(uintptr_t)w->enabled, 0);
    return TRUE;
}

BOOL WinCalcFrameRect(HWND handle, PRECTL rect, BOOL client)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || !rect)
        return FALSE;
    return frameClientRect(window, rect, client);
}
