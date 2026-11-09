/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* WinDefWindowProc: the default processing of every window message. */
#include "emulator/launch_record.h"
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/video_frames.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/ps/pmmerge/pmwin/controls.h"
#include "os2/ps/pmmerge/pmwin/menus.h"
#include "os2/include/private/pmp.h"

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
