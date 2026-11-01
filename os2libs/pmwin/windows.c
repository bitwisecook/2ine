/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
Window *getWindowFromHWND(const AnchorBlock *anchor, const HWND hwnd)
{
    if (hwnd >= FIRST_HWND_VALUE)
    {
        const size_t idx = (size_t)(hwnd - FIRST_HWND_VALUE);
        if (idx >= anchor->windows_array_len)
        {
            return NULL;
        }
        Window *win = anchor->windows[idx];
        return (win && win->hwnd == hwnd) ? win : NULL;
    }
    else if (hwnd == HWND_DESKTOP)
    {
        return &desktop_window;
    }
    return NULL;
}

WindowClass *findRegisteredClass(AnchorBlock *anchor, const char *classname)
{
    const uintptr_t intclass = (uintptr_t)classname;
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
            case WC_BUTTON:
            case WC_MENU:
            case WC_STATIC:
            case WC_ENTRYFIELD:
            case WC_LISTBOX:
            case WC_SCROLLBAR:
            case WC_TITLEBAR:
            case WC_MLE:
            case WC_APPSTAT:
            case WC_KBDSTAT:
            case WC_PECIC:
            case WC_DBE_KKPOPUP:
            case WC_SPINBUTTON:
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
    if (!win || (win->hwnd == NULLHANDLE))
    {
        return;
    }

    for (size_t i = 0; i < anchor->pres_spaces_array_len; i++)
        if (anchor->pres_spaces[i].window == win)
            destroyPresentationSpace(anchor, &anchor->pres_spaces[i]);
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
    // WM_DESTROY is a notification and cannot prevent destruction.
    sendMessage(win, WM_DESTROY, 0, 0);

    Window *next = NULL;
    for (Window *i = win->children; i != NULL; i = next)
    {
        next = i->sibling;
        destroyWindow(anchor, i);
    }

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
    if (!parent)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
    }

    Window *owner = NULL;
    if (hwndOwner != NULLHANDLE)
    {
        owner = getWindowFromHWND(anchor, hwndOwner);
        if (!owner)
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
    size_t window_index = 0;
    while (anchor->windows[window_index] != win)
        window_index++;
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
    win->hwnd = (HWND)(window_index + FIRST_HWND_VALUE);
    win->enabled = TRUE;
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
        lx_host_window_position(win->heavy.window, win->x, (parent->h - win->y) - win->h, win->w,
                                win->h);
        if (win->style & WS_VISIBLE)
        {

            lx_host_window_show(win->heavy.window);
        }
    }

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
        if (phwndClient)
        {
            *phwndClient = clientwin;
        }
    }

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
    if (!win)
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

    switch (msg)
    {
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

        case WM_ACTIVATE:
        case WM_APPTERMINATENOTIFY:
        case WM_ADJUSTWINDOWPOS:
        case WM_CALCFRAMERECT:
        case WM_COMMAND:
        case WM_CONTROL:
        case WM_CREATE:
        case WM_CTLCOLORCHANGE:
        case WM_DESTROY:
        case WM_DRAWITEM:
        case WM_ENABLE:
        case WM_ERASEBACKGROUND:
        case WM_ERROR:
        case WM_FORMATFRAME:
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
        case WM_SYSCOMMAND:
        case WM_SYSVALUECHANGED:
        case WM_TIMER:
        case WM_TRACKFRAME:
        case WM_TRANSLATEACCEL:

        case WM_UPDATEFRAME:
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
        case WM_BUTTON1DBLCLK:
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
            return HT_NORMAL;

        case WM_BUTTON1DOWN:
        case WM_BUTTON2DOWN:
        case WM_BUTTON3DOWN:
        case WM_CALCVALIDRECTS:
        case WM_CONTROLPOINTER:
        case WM_FOCUSCHANGE:
        case WM_HELP:
        case WM_MOUSEMOVE:
        case WM_PAINT:
        case WM_PPAINT:
        case WM_QUERYCONVERTPOS:
        case WM_QUERYWINDOWPARAMS:
        case WM_REALIZEPALETTE:
        case WM_WINDOWPOSCHANGED:
            fprintf(
                stderr,
                "Message that doesn't something other than return 0, %u, in WinDefWindowProc!\n",
                (uint)msg);
            break;

        default:
            fprintf(stderr, "Unhandled message %u in WinDefWindowProc!\n", (uint)msg);
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
    if (!win)
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

    return ((win->style & WS_VISIBLE) == WS_VISIBLE) ? TRUE : FALSE;
}

BOOL WinSetWindowPos(HWND hwnd, HWND behind, LONG x, LONG y, LONG cx, LONG cy, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
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
    if (!(flags & SWP_NOADJUST))
        sendWindowPos(win, WM_ADJUSTWINDOWPOS, &swp);
    LONG oldw = win->w, oldh = win->h;
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
        if (!lx_host_window_position(win->heavy.window, win->x, desktop_window.h - win->y - win->h,
                                     win->w, win->h))
            return FALSE;
        if (win->style & WS_VISIBLE)
            lx_host_window_show(win->heavy.window);
        else
            lx_host_window_hide(win->heavy.window);
    }
    if (was_visible != ((win->style & WS_VISIBLE) != 0))
        sendMessage(win, WM_SHOW, (MPARAM)(uintptr_t)((win->style & WS_VISIBLE) != 0), 0);
    if (oldw != win->w || oldh != win->h)
    {
        sendMessage(win, WM_SIZE, (MPARAM)(uintptr_t)(((uint32)(uint16)oldh << 16) | (uint16)oldw),
                    (MPARAM)(uintptr_t)(((uint32)(uint16)win->h << 16) | (uint16)win->w));
        for (Window *child = win->children; child; child = child->sibling)
            if (child->id == FID_CLIENT)
                WinSetWindowPos(child->hwnd, HWND_TOP, 0, 0, win->w, win->h,
                                SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    if (!(flags & SWP_NOREDRAW) && (flags & (SWP_SIZE | SWP_SHOW)))
        postExposedMessages(anchor, win, currentSystemTicks());
    return TRUE;
}

BOOL WinQueryWindowPos(HWND hwnd, PSWP position)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
    if (!win || !position)
        return FALSE;
    *position = (SWP){(win->style & WS_VISIBLE) ? SWP_SHOW : SWP_HIDE,
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
HPOINTER WinQuerySysPointer(HWND hwnd, LONG id, BOOL copy)
{
    (void)hwnd;
    (void)id;
    (void)copy;
    /* System pointer/icon resources are not provided by this worker. */
    return NULLHANDLE;
}

BOOL WinShowWindow(HWND window, BOOL show)
{
    return WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, show ? SWP_SHOW : SWP_HIDE);
}

BOOL WinEnableWindow(HWND window, BOOL enable)
{
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
    if (was_enabled != w->enabled)
        sendMessage(w, WM_ENABLE, (MPARAM)(uintptr_t)w->enabled, 0);
    return TRUE;
}
