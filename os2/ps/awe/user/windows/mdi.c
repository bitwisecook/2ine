/* Public domain. No rights reserved. Wholly AI-authored. */
/* The MDI client owns workbook children and their active-window state. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"

enum
{
    /* Child IDs start here when CLIENTCREATESTRUCT is not supplied. */
    DEFAULT_FIRST_CHILD_ID = 0xFF00,
    /* Longest class name and title copied from MDICREATESTRUCT, including terminators. */
    CLASS_NAME_LIMIT = 256,
    TITLE_LIMIT = 65536,
    /* GetKeyState sets the high bit while the key is down. */
    KEY_DOWN = 0x8000
};

BOOL open32ShowMdiChild(Open32Window *window, LONG command)
{
    Open32Window *parent = open32FindWindow(window->parent);
    if (!(window->extended_style & WIN32_WS_EX_MDICHILD) || !parent ||
        parent->class->builtin != OPEN32_BUILTIN_MDI_CLIENT ||
        (command != WIN32_SW_SHOWNORMAL && command != WIN32_SW_MAXIMIZE &&
         command != WIN32_SW_SHOW && command != WIN32_SW_SHOWNA && command != WIN32_SW_RESTORE))
        return FALSE;
    ULONG handle = window->handle, owner = parent->handle, old_style = window->style;
    LONG x = window->x, y = window->y, width = window->width, height = window->height;
    if (command == WIN32_SW_MAXIMIZE)
    {
        RECTL bounds;
        if (!WinQueryWindowRect(open32ClientWindow(owner), &bounds))
            return TRUE;
        if (!(old_style & WIN32_WS_MAXIMIZE))
        {
            window->mdi_normal_x = x;
            window->mdi_normal_y = y;
            window->mdi_normal_width = width;
            window->mdi_normal_height = height;
            window->mdi_normal_valid = TRUE;
        }
        x = y = 0;
        width = bounds.xRight - bounds.xLeft;
        height = bounds.yTop - bounds.yBottom;
        window->style = (old_style & ~(ULONG)WIN32_WS_MINIMIZE) | WIN32_WS_MAXIMIZE;
    }
    else if (command == WIN32_SW_SHOWNORMAL || command == WIN32_SW_RESTORE)
    {
        if (window->mdi_normal_valid &&
            (old_style & ((ULONG)WIN32_WS_MINIMIZE | WIN32_WS_MAXIMIZE)))
        {
            x = window->mdi_normal_x;
            y = window->mdi_normal_y;
            width = window->mdi_normal_width;
            height = window->mdi_normal_height;
        }
        window->style &= ~((ULONG)WIN32_WS_MINIMIZE | WIN32_WS_MAXIMIZE);
    }
    /* Native PM maximisation accepts WC_FRAME only. An Open32 MDI child is
       positioned within its MDICLIENT instead, with Windows state retained. */
    BOOL moved = SetWindowPos(handle, WIN32_HWND_TOP, x, y, width, height,
                              WIN32_SWP_NOZORDER | WIN32_SWP_SHOWWINDOW |
                                  (command == WIN32_SW_SHOWNA ? WIN32_SWP_NOACTIVATE : 0));
    window = open32FindWindow(handle);
    if (!moved)
    {
        if (window)
            window->style = old_style;
        return TRUE;
    }
    if (window && command != WIN32_SW_SHOWNA)
        SendMessage(owner, WIN32_WM_MDIACTIVATE, handle, 0);
    return TRUE;
}

static ULONG mdi_create(Open32Window *client, ULONG definition)
{
    uint8 bytes[WIN32_MDICREATESTRUCT_SIZE];
    if (!lx_execution_read(GProcessState.execution, definition, bytes, sizeof bytes))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG class_pointer = lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_CLASS),
          title_pointer = lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_TITLE);
    /* A class "name" up to 0xFFFF is a class atom. */
    char *class_copy =
        class_pointer > UINT16_MAX
            ? lx_execution_copy_string(GProcessState.execution, class_pointer, CLASS_NAME_LIMIT)
            : NULL;
    char *title = title_pointer ? lx_execution_copy_string(GProcessState.execution, title_pointer,
                                                           TITLE_LIMIT)
                                : NULL;
    if ((class_pointer > UINT16_MAX && !class_copy) || (title_pointer && !title))
    {
        free(class_copy);
        free(title);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG x = (LONG)lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_X),
         y = (LONG)lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_Y),
         width = (LONG)lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_WIDTH),
         height = (LONG)lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_HEIGHT);
    if (x == (LONG)WIN32_CW_USEDEFAULT)
        x = 0;
    if (y == (LONG)WIN32_CW_USEDEFAULT)
        y = 0;
    if (width == (LONG)WIN32_CW_USEDEFAULT)
        width = client->width;
    if (height == (LONG)WIN32_CW_USEDEFAULT)
        height = client->height;
    ULONG child = CreateWindowEx(
        WIN32_WS_EX_MDICHILD, class_copy ? class_copy : (PSZ)(uintptr_t)class_pointer, title,
        lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_STYLE) | WIN32_WS_CHILD | WIN32_WS_VISIBLE,
        x, y, width, height, client->handle, client->mdi_next_id++,
        lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_OWNER),
        lx_guest_load_u32(bytes + WIN32_MDICREATESTRUCT_PARAMETER));
    free(class_copy);
    free(title);
    if (child)
    {
        ULONG previous = client->mdi_active;
        client->mdi_active = child;
        Open32Window *old = open32FindWindow(previous), *next = open32FindWindow(child);
        if (old)
            open32WindowCallback(old, WIN32_WM_MDIACTIVATE, previous, child);
        if (next)
            open32WindowCallback(next, WIN32_WM_MDIACTIVATE, previous, child);
    }
    return child;
}

ULONG open32BuiltinWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    Open32Window *window = open32FindWindow(handle);
    if (window && window->class->builtin == OPEN32_BUILTIN_SCROLLBAR)
        return open32ScrollWindowProc(handle, message, first, second);
    if (!window || window->class->builtin != OPEN32_BUILTIN_MDI_CLIENT)
        return 0;
    if (message == WIN32_WM_NCCREATE)
    {
        uint8 creation[WIN32_CREATESTRUCT_SIZE], client[WIN32_CLIENTCREATESTRUCT_SIZE];
        if (!lx_execution_read(GProcessState.execution, second, creation, sizeof creation))
            return 0;
        ULONG parameter = lx_guest_load_u32(creation + WIN32_CREATESTRUCT_CREATE_PARAMS);
        window->mdi_next_id = DEFAULT_FIRST_CHILD_ID;
        if (parameter)
        {
            if (!lx_execution_read(GProcessState.execution, parameter, client, sizeof client))
                return 0;
            window->mdi_menu = lx_guest_load_u32(client + WIN32_CLIENTCREATESTRUCT_WINDOW_MENU);
            window->mdi_next_id = lx_guest_load_u32(client + WIN32_CLIENTCREATESTRUCT_FIRST_CHILD);
        }
        return 1;
    }
    if (message == WIN32_WM_MDICREATE)
        return mdi_create(window, second);
    if (message == WIN32_WM_MDIACTIVATE)
    {
        Open32Window *next = open32FindWindow(first);
        if (!next || next->parent != handle)
            return 0;
        ULONG previous = window->mdi_active;
        window->mdi_active = first;
        Open32Window *old = open32FindWindow(previous);
        if (old && previous != first)
            open32WindowCallback(old, WIN32_WM_MDIACTIVATE, previous, first);
        next = open32FindWindow(first);
        if (next && previous != first)
            open32WindowCallback(next, WIN32_WM_MDIACTIVATE, previous, first);
        if (open32FindWindow(first))
            SetFocus(first);
        return 0;
    }
    if (message == WIN32_WM_MDIDESTROY)
    {
        Open32Window *child = open32FindWindow(first);
        if (!child || child->parent != handle)
            return 0;
        if (window->mdi_active == first)
            window->mdi_active = 0;
        DestroyWindow(first);
        return 0; /* WM_MDIDESTROY always returns zero. */
    }
    if (message == WIN32_WM_MDISETMENU)
    {
        ULONG previous = GetMenu(window->parent);
        if (first && !SetMenu(window->parent, first))
            return 0;
        if (second)
            window->mdi_menu = second;
        return previous;
    }
    if (message == WIN32_WM_MDIGETACTIVE)
    {
        /* lParam, when set, receives whether the active child is maximized. */
        if (second)
        {
            Open32Window *child = open32FindWindow(window->mdi_active);
            uint8 result[4];
            lx_guest_store_u32(result, child && (child->style & WIN32_WS_MAXIMIZE));
            if (!lx_execution_write(GProcessState.execution, second, result, 4))
                open32SetError(ERROR_INVALID_PARAMETER);
        }
        return window->mdi_active;
    }
    if (message == WIN32_WM_SIZE)
    {
        window->width = second & 0xFFFF;
        window->height = second >> 16;
        Open32Window *child = open32FindWindow(window->mdi_active);
        if (child && (child->style & WIN32_WS_MAXIMIZE))
            SetWindowPos(child->handle, WIN32_HWND_TOP, 0, 0, window->width, window->height,
                         WIN32_SWP_NOZORDER | WIN32_SWP_NOACTIVATE);
        return 0;
    }
    return (ULONG)DefWindowProc(handle, message, first, second);
}

LONG DefFrameProc(ULONG frame, ULONG client, ULONG message, ULONG first, ULONG second)
{
    Open32Window *mdi = open32FindWindow(client);
    if (message == WIN32_WM_SETFOCUS && mdi)
    {
        SetFocus(mdi->mdi_active ? mdi->mdi_active : client);
        return 0;
    }
    if (message == WIN32_WM_SIZE && mdi)
    {
        uint8 rectangle[WIN32_RECT_SIZE];
        if (GetClientRect(frame, rectangle))
            MoveWindow(client, 0, 0, (LONG)lx_guest_load_u32(rectangle + WIN32_RECT_RIGHT),
                       (LONG)lx_guest_load_u32(rectangle + WIN32_RECT_BOTTOM), TRUE);
        return 0;
    }
    /* A window-menu command whose ID is a child's activates that child. */
    if (message == WIN32_WM_COMMAND && mdi)
    {
        ULONG child_handle = WinWindowFromID(client, first & 0xFFFF);
        Open32Window *child = open32FindWindow(child_handle);
        if (child && child->parent == client)
        {
            SendMessage(client, WIN32_WM_MDIACTIVATE, child_handle, 0);
            return 0;
        }
    }
    if (message == WIN32_WM_NCACTIVATE && mdi && mdi->mdi_active)
        SendMessage(mdi->mdi_active, message, first, second);
    return DefWindowProc(frame, message, first, second);
}

LONG DefMDIChildProc(ULONG child, ULONG message, ULONG first, ULONG second)
{
    return DefWindowProc(child, message, first, second);
}

BOOL TranslateMDISysAccel(ULONG client, PVOID message)
{
    if (!message)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG kind = lx_guest_load_u32((uint8 *)message + WIN32_MSG_MESSAGE),
          key = lx_guest_load_u32((uint8 *)message + WIN32_MSG_WPARAM);
    if ((kind != WIN32_WM_KEYDOWN && kind != WIN32_WM_SYSKEYDOWN) ||
        !(GetKeyState(WIN32_VK_CONTROL) & KEY_DOWN))
        return FALSE;
    ULONG active = (ULONG)SendMessage(client, WIN32_WM_MDIGETACTIVE, 0, 0);
    if (!active)
        return FALSE;
    /* Ctrl+F4 closes the active child; Ctrl+F6 and Ctrl+Tab cycle, backwards with Shift. */
    if (key == WIN32_VK_F4)
        SendMessage(client, WIN32_WM_MDIDESTROY, active, 0);
    else if (key == WIN32_VK_F6 || key == WIN32_VK_TAB)
        SendMessage(client, WIN32_WM_MDINEXT, active,
                    (GetKeyState(WIN32_VK_SHIFT) & KEY_DOWN) != 0);
    else
        return FALSE;
    return TRUE;
}
