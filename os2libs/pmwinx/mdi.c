/* Public domain. No rights reserved. Wholly AI-authored. */
/* The MDI client owns workbook children and their active-window state. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
static ULONG mdi_create(Open32Window *client, ULONG definition)
{
    uint8 bytes[36];
    if (!lx_execution_read(GLoaderState.execution, definition, bytes, sizeof bytes))
    {
        open32SetError(87);
        return 0;
    }
    ULONG class_pointer = lx_guest_load_u32(bytes), title_pointer = lx_guest_load_u32(bytes + 4);
    char *class_copy = class_pointer > UINT16_MAX
                           ? lx_execution_copy_string(GLoaderState.execution, class_pointer, 256)
                           : NULL;
    char *title = title_pointer
                      ? lx_execution_copy_string(GLoaderState.execution, title_pointer, 65536)
                      : NULL;
    if ((class_pointer > UINT16_MAX && !class_copy) || (title_pointer && !title))
    {
        free(class_copy);
        free(title);
        open32SetError(87);
        return 0;
    }
    LONG x = (LONG)lx_guest_load_u32(bytes + 12), y = (LONG)lx_guest_load_u32(bytes + 16),
         width = (LONG)lx_guest_load_u32(bytes + 20), height = (LONG)lx_guest_load_u32(bytes + 24);
    if (x == INT32_MIN)
        x = 0;
    if (y == INT32_MIN)
        y = 0;
    if (width == INT32_MIN)
        width = client->width;
    if (height == INT32_MIN)
        height = client->height;
    ULONG child = CreateWindowEx(0x40, class_copy ? class_copy : (PSZ)(uintptr_t)class_pointer,
                                 title, lx_guest_load_u32(bytes + 28) | 0x50000000, x, y, width,
                                 height, client->handle, client->mdi_next_id++,
                                 lx_guest_load_u32(bytes + 8), lx_guest_load_u32(bytes + 32));
    free(class_copy);
    free(title);
    if (child)
    {
        ULONG previous = client->mdi_active;
        client->mdi_active = child;
        Open32Window *old = open32FindWindow(previous), *next = open32FindWindow(child);
        if (old)
            open32WindowCallback(old, 0x222, previous, child);
        if (next)
            open32WindowCallback(next, 0x222, previous, child);
    }
    return child;
}
ULONG open32BuiltinWindowProc(ULONG handle, ULONG message, ULONG first, ULONG second)
{
    Open32Window *window = open32FindWindow(handle);
    if (window && window->class->builtin == 2)
        return open32ScrollWindowProc(handle, message, first, second);
    if (!window || window->class->builtin != 1)
        return 0;
    if (message == 0x81)
    {
        uint8 creation[48], client[8];
        if (!lx_execution_read(GLoaderState.execution, second, creation, sizeof creation))
            return 0;
        ULONG parameter = lx_guest_load_u32(creation);
        window->mdi_next_id = 0xFF00;
        if (parameter)
        {
            if (!lx_execution_read(GLoaderState.execution, parameter, client, sizeof client))
                return 0;
            window->mdi_menu = lx_guest_load_u32(client);
            window->mdi_next_id = lx_guest_load_u32(client + 4);
        }
        return 1;
    }
    if (message == 0x220)
        return mdi_create(window, second);
    if (message == 0x222)
    {
        Open32Window *next = open32FindWindow(first);
        if (!next || next->parent != handle)
            return 0;
        ULONG previous = window->mdi_active;
        window->mdi_active = first;
        Open32Window *old = open32FindWindow(previous);
        if (old && previous != first)
            open32WindowCallback(old, 0x222, previous, first);
        next = open32FindWindow(first);
        if (next && previous != first)
            open32WindowCallback(next, 0x222, previous, first);
        if (open32FindWindow(first))
            SetFocus(first);
        return 0;
    }
    if (message == 0x221)
    {
        Open32Window *child = open32FindWindow(first);
        if (!child || child->parent != handle)
            return 0;
        if (window->mdi_active == first)
            window->mdi_active = 0;
        return DestroyWindow(first);
    }
    if (message == 0x230) /* WM_MDISETMENU */
    {
        ULONG previous = GetMenu(window->parent);
        if (first && !SetMenu(window->parent, first))
            return 0;
        if (second)
            window->mdi_menu = second;
        return previous;
    }
    if (message == 0x229)
    {
        if (second)
        {
            Open32Window *child = open32FindWindow(window->mdi_active);
            uint8 result[4];
            lx_guest_store_u32(result, child && (child->style & 0x01000000));
            if (!lx_execution_write(GLoaderState.execution, second, result, 4))
                open32SetError(87);
        }
        return window->mdi_active;
    }
    if (message == 5)
    {
        window->width = second & 0xFFFF;
        window->height = second >> 16;
        Open32Window *child = open32FindWindow(window->mdi_active);
        if (child && (child->style & 0x01000000))
            SetWindowPos(child->handle, 0, 0, 0, window->width, window->height, 4 | 16);
        return 0;
    }
    return (ULONG)DefWindowProc(handle, message, first, second);
}

LONG DefFrameProc(ULONG frame, ULONG client, ULONG message, ULONG first, ULONG second)
{
    Open32Window *mdi = open32FindWindow(client);
    if (message == 7 && mdi)
    {
        SetFocus(mdi->mdi_active ? mdi->mdi_active : client);
        return 0;
    }
    if (message == 5 && mdi)
    {
        uint8 rectangle[16];
        if (GetClientRect(frame, rectangle))
            MoveWindow(client, 0, 0, (LONG)lx_guest_load_u32(rectangle + 8),
                       (LONG)lx_guest_load_u32(rectangle + 12), TRUE);
        return 0;
    }
    if (message == 0x111 && mdi)
    {
        ULONG child_handle = WinWindowFromID(client, first & 0xFFFF);
        Open32Window *child = open32FindWindow(child_handle);
        if (child && child->parent == client)
        {
            SendMessage(client, 0x222, child_handle, 0);
            return 0;
        }
    }
    if (message == 0x86 && mdi && mdi->mdi_active)
        SendMessage(mdi->mdi_active, message, first, second);
    return DefWindowProc(frame, message, first, second);
}
LONG DefMDIChildProc(ULONG child, ULONG message, ULONG first, ULONG second)
{
    return DefWindowProc(child, message, first, second);
}
