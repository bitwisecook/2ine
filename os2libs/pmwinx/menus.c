/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 menus retain PM menu windows and their normal message behavior. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/menus.h"
typedef struct MenuText
{
    char *text;
    struct MenuText *next;
} MenuText;
typedef struct Open32Menu
{
    HWND handle;
    BOOL popup, system;
    MenuText *texts;
    struct Open32Menu *next;
} Open32Menu;
static _Thread_local Open32Menu *menus;
static void free_texts(Open32Menu *menu)
{
    while (menu->texts)
    {
        MenuText *text = menu->texts;
        menu->texts = text->next;
        free(text->text);
        free(text);
    }
}
static Open32Menu *find_menu(ULONG handle)
{
    for (Open32Menu *menu = menus; menu; menu = menu->next)
        if (menu->handle == handle)
            return menu;
    return NULL;
}
static ULONG create_menu(BOOL popup)
{
    if (!open32Presentation())
    {
        open32SetError(8);
        return 0;
    }
    Open32Menu *menu = calloc(1, sizeof *menu);
    if (!menu)
    {
        open32SetError(8);
        return 0;
    }
    uint8 empty[10] = {0};
    lx_guest_store_u32(empty, sizeof empty);
    menu->handle = WinCreateMenu(HWND_DESKTOP, empty);
    if (!menu->handle)
    {
        free(menu);
        open32SetError(8);
        return 0;
    }
    menu->popup = popup;
    menu->next = menus;
    menus = menu;
    return menu->handle;
}
ULONG CreateMenu(void)
{
    return create_menu(FALSE);
}
ULONG CreatePopupMenu(void)
{
    return create_menu(TRUE);
}
BOOL DestroyMenu(ULONG handle)
{
    Open32Menu **link = &menus;
    while (*link && (*link)->handle != handle)
        link = &(*link)->next;
    if (!*link)
    {
        open32SetError(1401);
        return FALSE;
    }
    Open32Menu *menu = *link;
    if (!WinDestroyWindow(handle))
        return FALSE;
    *link = menu->next;
    free_texts(menu);
    free(menu);
    return TRUE;
}
void open32MenusCleanup(void)
{
    while (menus)
    {
        Open32Menu *menu = menus;
        menus = menu->next;
        WinDestroyWindow(menu->handle);
        free_texts(menu);
        free(menu);
    }
}

static Window *menu_window(ULONG handle)
{
    Window *window = find_menu(handle) ? lx_pm_menu_window(handle) : NULL;
    return window && window->menu ? window : NULL;
}
static PmMenuItem *menu_item(Window **target, ULONG item, ULONG flags, unsigned depth)
{
    if (!*target || depth > 32)
        return NULL;
    PmMenu *menu = (*target)->menu;
    if (flags & 0x400)
        return item < menu->count ? &menu->items[item] : NULL;
    for (size_t i = 0; i < menu->count; i++)
    {
        if (menu->items[i].item.id == item)
            return &menu->items[i];
        Window *sub = menu_window(menu->items[i].item.hwndSubMenu);
        PmMenuItem *found = menu_item(&sub, item, flags, depth + 1);
        if (found)
        {
            *target = sub;
            return found;
        }
    }
    return NULL;
}
BOOL InsertMenu(ULONG handle, ULONG position, ULONG flags, ULONG identifier, PSZ text)
{
    TRACE_NATIVE("InsertMenu(%u, %u, %X, %u, %p)", handle, position, flags, identifier, text);
    Window *window = menu_window(handle);
    if (!window)
    {
        open32SetError(1401);
        return FALSE;
    }
    if (flags & (4 | 0x100))
    {
        open32SetError(50);
        return FALSE;
    }
    LONG index;
    if (position == UINT32_MAX)
        index = (LONG)window->menu->count;
    else if (flags & 0x400)
        index = position <= window->menu->count ? (LONG)position : -1;
    else
    {
        PmMenuItem *before = menu_item(&window, position, flags, 0);
        index = before ? before->item.iPosition : -1;
    }
    if (index < 0 || window->menu->count >= 4096)
    {
        open32SetError(87);
        return FALSE;
    }
    Window *submenu = flags & 0x10 ? menu_window(identifier) : NULL;
    if ((flags & 0x10) && !submenu)
    {
        open32SetError(1401);
        return FALSE;
    }
    if (submenu)
    {
        for (Window *owner = window; owner && owner->menu; owner = owner->owner)
            if (owner == submenu)
            {
                open32SetError(87);
                return FALSE;
            }
        if (submenu->owner && submenu->owner->menu)
        {
            open32SetError(87);
            return FALSE;
        }
    }
    MenuText *label = calloc(1, sizeof *label);
    char *copy = strdup(text ? text : "");
    if (label)
        label->text = strdup(text ? text : "");
    if (!copy || !label || !label->text)
    {
        free(copy);
        if (label)
        {
            free(label->text);
            free(label);
        }
        open32SetError(8);
        return FALSE;
    }
    size_t used = 0;
    for (size_t i = 0; copy[i]; i++)
    {
        uint8 c = (uint8)copy[i];
        if (c == '&')
        {
            if (copy[i + 1] == '&')
                i++;
            else
                c = '~';
        }
        else if (c == '\t')
            c = 8;
        copy[used++] = (char)WinCpTranslateChar(0, 1004, c, 437);
    }
    copy[used] = 0;
    MENUITEM record = {(SHORT)index,
                       (USHORT)((flags & 0x800 ? 4 : 0) | (submenu ? 0x10 : 0) |
                                (find_menu(window->hwnd)->system ? 0x40 : 0)),
                       (USHORT)((flags & 3 ? 0x4000 : 0) | (flags & 8 ? 0x2000 : 0)),
                       (USHORT)identifier,
                       submenu ? submenu->hwnd : 0,
                       flags & 0x8FF};
    LONG result = (LONG)(intptr_t)WinSendMsg(window->hwnd, 0x180, &record, copy);
    free(copy);
    if (result < 0)
    {
        free(label->text);
        free(label);
        open32SetError(8);
        return FALSE;
    }
    MenuText **slot = &find_menu(window->hwnd)->texts;
    for (LONG i = 0; i < index; i++)
        slot = &(*slot)->next;
    label->next = *slot;
    *slot = label;
    if (submenu)
        submenu->owner = window;
    WinInvalidateRect(window->hwnd, NULL, FALSE);
    return TRUE;
}
BOOL AppendMenu(ULONG handle, ULONG flags, ULONG identifier, PSZ text)
{
    return InsertMenu(handle, UINT32_MAX, flags, identifier, text);
}
LONG GetMenuItemCount(ULONG handle)
{
    Window *window = menu_window(handle);
    if (!window)
    {
        open32SetError(1401);
        return -1;
    }
    return (LONG)window->menu->count;
}
ULONG GetMenuItemID(ULONG handle, LONG position)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, (ULONG)position, 0x400, 0);
    return item && !item->item.hwndSubMenu ? item->item.id : UINT32_MAX;
}
ULONG GetSubMenu(ULONG handle, LONG position)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, (ULONG)position, 0x400, 0);
    return item ? item->item.hwndSubMenu : 0;
}

ULONG GetMenuState(ULONG handle, ULONG identity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(1456);
        return UINT32_MAX;
    }
    ULONG result = item->item.hItem & 0x8FF;
    if (item->item.hwndSubMenu)
    {
        Window *sub = menu_window(item->item.hwndSubMenu);
        result |= 0x10 | (sub ? (sub->menu->count & 255) << 8 : 0);
    }
    return result;
}
ULONG CheckMenuItem(ULONG handle, ULONG identity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(1456);
        return UINT32_MAX;
    }
    ULONG old = item->item.hItem & 8;
    item->item.hItem = (item->item.hItem & ~8u) | (flags & 8);
    item->item.afAttribute =
        (USHORT)((item->item.afAttribute & ~0x2000u) | (flags & 8 ? 0x2000 : 0));
    WinInvalidateRect(window->hwnd, NULL, FALSE);
    return old;
}
ULONG EnableMenuItem(ULONG handle, ULONG identity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(1456);
        return UINT32_MAX;
    }
    ULONG old = item->item.hItem & 3;
    item->item.hItem = (item->item.hItem & ~3u) | (flags & 3);
    item->item.afAttribute =
        (USHORT)((item->item.afAttribute & ~0x4000u) | (flags & 3 ? 0x4000 : 0));
    WinInvalidateRect(window->hwnd, NULL, FALSE);
    return old;
}

BOOL IsMenu(ULONG handle)
{
    return menu_window(handle) != NULL;
}

static BOOL remove_menu_item(ULONG handle, ULONG identity, ULONG flags, BOOL destroy)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(1456);
        return FALSE;
    }
    Window *sub = menu_window(item->item.hwndSubMenu);
    size_t index = (size_t)(item - window->menu->items);
    if (!lx_pm_menu_remove(window->hwnd, index, destroy))
        return FALSE;
    MenuText **slot = &find_menu(window->hwnd)->texts;
    for (size_t i = 0; i < index; i++)
        slot = &(*slot)->next;
    MenuText *label = *slot;
    if (label)
    {
        *slot = label->next;
        free(label->text);
        free(label);
    }
    if (sub && !destroy)
        sub->owner = NULL;
    return TRUE;
}
BOOL RemoveMenu(ULONG handle, ULONG identity, ULONG flags)
{
    return remove_menu_item(handle, identity, flags, FALSE);
}
BOOL DeleteMenu(ULONG handle, ULONG identity, ULONG flags)
{
    return remove_menu_item(handle, identity, flags, TRUE);
}

LONG GetMenuString(ULONG handle, ULONG identity, PCHAR output, LONG capacity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item || capacity < 0)
    {
        if (output && capacity > 0)
            output[0] = 0;
        open32SetError(item ? 87 : 1456);
        return 0;
    }
    MenuText *label = find_menu(window->hwnd)->texts;
    size_t index = (size_t)(item - window->menu->items);
    for (size_t i = 0; i < index && label; i++)
        label = label->next;
    const char *text = label ? label->text : "";
    size_t length = strlen(text);
    if (!output || !capacity)
        return (LONG)length;
    if (length >= (size_t)capacity)
        length = (size_t)capacity - 1;
    memcpy(output, text, length);
    output[length] = 0;
    return (LONG)length;
}

ULONG GetMenu(ULONG handle)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return 0;
    }
    return window->style & 0x40000000u ? 0 : window->menu;
}
BOOL SetMenu(ULONG handle, ULONG menu)
{
    TRACE_NATIVE("SetMenu(%u, %u)", (uint)handle, (uint)menu);
    Open32Window *window = open32FindWindow(handle);
    if (!window || (window->style & 0x40000000u))
    {
        open32SetError(window ? 87 : 1400);
        return FALSE;
    }
    Window *next = menu ? menu_window(menu) : NULL;
    if (menu && (!next || (next->parent->hwnd != HWND_DESKTOP && next->parent->hwnd != handle)))
    {
        open32SetError(1401);
        return FALSE;
    }
    ULONG old = window->menu;
    Window *previous = old ? menu_window(old) : NULL;
    if (previous && old != menu && previous->parent->hwnd == handle)
    {
        if (!WinShowWindow(old, FALSE))
            return FALSE;
        if (!WinSetParent(old, HWND_DESKTOP, FALSE))
        {
            WinShowWindow(old, TRUE);
            return FALSE;
        }
        WinSetOwner(old, 0);
        WinSetWindowUShort(old, -1, 0); /* QWS_ID */
    }
    if (menu && !lx_pm_menu_attach(menu, handle))
    {
        if (previous && old != menu)
            lx_pm_menu_attach(old, handle);
        open32SetError(8);
        return FALSE;
    }
    window = open32FindWindow(handle);
    if (!window || window->destroyed)
    {
        open32SetError(1400);
        return FALSE;
    }
    window->menu = menu;
    ULONG client = window->client;
    RECTL bounds;
    if (!WinQueryWindowRect(handle, &bounds))
        return FALSE;
    if (client != handle)
    {
        LONG height = bounds.yTop - (menu ? 24 : 0);
        if (height <= 0 || !WinSetWindowPos(client, HWND_BOTTOM, 0, 0, bounds.xRight, height,
                                            SWP_MOVE | SWP_SIZE | SWP_SHOW))
            return FALSE;
    }
    return WinInvalidateRect(handle, NULL, TRUE);
}

BOOL DrawMenuBar(ULONG handle)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return FALSE;
    }
    return SetMenu(handle, window->menu);
}

ULONG GetSystemMenu(ULONG handle, BOOL revert)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return 0;
    }
    if (revert)
    {
        ULONG previous = window->system_menu;
        window->system_menu = 0;
        if (previous)
            DestroyMenu(previous);
        return 0;
    }
    if (!(window->style & 0x00080000u))
        return 0;
    if (window->system_menu && IsMenu(window->system_menu))
        return window->system_menu;
    ULONG menu = CreatePopupMenu();
    if (!menu)
        return 0;
    find_menu(menu)->system = TRUE;
    BOOL minimized = (window->style & 0x20000000u) != 0;
    BOOL maximized = (window->style & 0x01000000u) != 0;
    BOOL ok =
        AppendMenu(menu, minimized || maximized ? 0 : 1, 0xF120, "&Restore") &&
        AppendMenu(menu, minimized || maximized ? 1 : 0, 0xF010, "&Move") &&
        AppendMenu(menu, minimized || maximized || !(window->style & 0x00040000) ? 1 : 0, 0xF000,
                   "&Size") &&
        AppendMenu(menu, minimized || !(window->style & 0x00020000) ? 1 : 0, 0xF020, "Mi&nimize") &&
        AppendMenu(menu, maximized || !(window->style & 0x00010000) ? 1 : 0, 0xF030, "Ma&ximize") &&
        AppendMenu(menu, 0x800, 0, NULL) &&
        AppendMenu(menu, window->class->style & 0x200 ? 1 : 0, 0xF060, "&Close\tAlt+F4") &&
        lx_pm_menu_owner(menu, handle);
    if (!ok)
    {
        DestroyMenu(menu);
        return 0;
    }
    window->system_menu = menu;
    return menu;
}
