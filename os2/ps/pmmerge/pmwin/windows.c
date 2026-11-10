/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Window creation and destruction, handle lookup and the enabled and visible state queries. */
#include "emulator/launch_record.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmp.h"

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

enum
{
    /* One hook each for the file and font dialogs, with room to spare. */
    MAX_WINDOW_DESTROY_HOOKS = 4
};

static PmWindowDestroyHook window_destroy_hooks[MAX_WINDOW_DESTROY_HOOKS];
static size_t window_destroy_hook_count;

/* Hooks register while their library loads, before any window exists. */
BOOL lx_pm_add_window_destroy_hook(PmWindowDestroyHook hook)
{
    for (size_t i = 0; i < window_destroy_hook_count; i++)
        if (window_destroy_hooks[i] == hook)
            return TRUE;
    if (!hook || window_destroy_hook_count == MAX_WINDOW_DESTROY_HOOKS)
        return FALSE;
    window_destroy_hooks[window_destroy_hook_count++] = hook;
    return TRUE;
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
    for (size_t i = 0; i < window_destroy_hook_count; i++)
        window_destroy_hooks[i](win);

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
    win->window_class_name = winclass->name; /* Class names live as long as the anchor block. */
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
