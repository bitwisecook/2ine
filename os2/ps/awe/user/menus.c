/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 menus retain PM menu windows and their normal message behavior. */
#include "os2/include/private/export.h"
#include "os2/include/bsedos.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/include/private/pmwinp.h"
#include "emulator/abi.h"

enum
{
    /* The MF_* bits a Windows menu item keeps as its state, stored in the PM item's hItem. */
    MENU_STATE_FLAGS = WIN32_MF_GRAYED | WIN32_MF_DISABLED | WIN32_MF_BITMAP | WIN32_MF_CHECKED |
                       WIN32_MF_POPUP | WIN32_MF_MENUBARBREAK | WIN32_MF_MENUBREAK |
                       WIN32_MF_HILITE | WIN32_MF_SEPARATOR,
    MENU_DISABLED_FLAGS = WIN32_MF_GRAYED | WIN32_MF_DISABLED,
    /* GetMenuState reports a popup's item count in bits 8-15. */
    MENU_STATE_COUNT_SHIFT = 8,
    /* Bounds on item counts and nesting, which also stop submenu cycles. */
    MENU_ITEMS_MAX = 4096,
    MENU_DEPTH_MAX = 32,
    LOADED_MENU_DEPTH_MAX = 16,
    LOADED_MENU_ITEMS_MAX = 65536,
    /* PM menu text separates the accelerator column with a backspace, Windows with a tab. */
    PM_MENU_ACCELERATOR_SEPARATOR = '\b',
    /* PM menu resources and native menu text without a code page use 437. */
    PM_MENU_CODEPAGE = 437,
    /* Pixels the client loses to a menu bar. */
    MENU_BAR_HEIGHT = 24,
    /* Pixels added to the Windows popup y before flipping it to PM coordinates. */
    POPUP_Y_OFFSET = 15,
    /* Keep the popup on screen, and let every mouse button and the keyboard drive it. */
    POPUP_MENU_FLAGS = PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 | PU_MOUSEBUTTON2 |
                       PU_MOUSEBUTTON3 | PU_KEYBOARD
};

/* An MT menu template header with no items: ULONG cb, code page, a reserved word and the item
   count. */
enum
{
    MT_HEADER_SIZE = 10
};

/* RT_RESNAMES: a ULONG count, that many ULONG identities, then the names in the same order. */
enum
{
    RESNAMES_COUNT_SIZE = 4,
    RESNAMES_ID_SIZE = 4,
    RESNAMES_SIZE_MAX = 1048576
};

typedef struct MenuText
{
    char *text;
    struct MenuText *next;
} MenuText;

typedef struct Open32Menu
{
    HWND handle;
    BOOL popup, system;
    ULONG context_help;
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
    for (Open32Menu **slot = &menus; *slot; slot = &(*slot)->next)
        if ((*slot)->handle == handle)
        {
            Open32Menu *menu = *slot;
            if (lx_pm_menu_window(handle))
                return menu;
            *slot = menu->next;
            free_texts(menu);
            free(menu);
            break;
        }
    return NULL;
}

static void prune_menus(void)
{
    for (Open32Menu **slot = &menus; *slot;)
    {
        Open32Menu *menu = *slot;
        if (lx_pm_menu_window(menu->handle))
            slot = &menu->next;
        else
        {
            *slot = menu->next;
            free_texts(menu);
            free(menu);
        }
    }
}

static ULONG create_menu(BOOL popup)
{
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    prune_menus();
    Open32Menu *menu = calloc(1, sizeof *menu);
    if (!menu)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    uint8 empty[MT_HEADER_SIZE] = {0};
    lx_guest_store_u32(empty, sizeof empty);
    menu->handle = WinCreateMenu(HWND_DESKTOP, empty);
    if (!menu->handle)
    {
        free(menu);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    Open32Menu *menu = *link;
    if (!WinDestroyWindow(handle))
        return FALSE;
    *link = menu->next;
    free_texts(menu);
    free(menu);
    prune_menus();
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
    if (!*target || depth > MENU_DEPTH_MAX)
        return NULL;
    PmMenu *menu = (*target)->menu;
    if (flags & WIN32_MF_BYPOSITION)
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

static BOOL menu_contains(Window *menu, Window *target, unsigned depth)
{
    if (menu == target || depth >= MENU_DEPTH_MAX)
        return TRUE;
    for (size_t i = 0; i < menu->menu->count; i++)
    {
        Window *sub = menu_window(menu->menu->items[i].item.hwndSubMenu);
        if (sub && menu_contains(sub, target, depth + 1))
            return TRUE;
    }
    return FALSE;
}

BOOL InsertMenu(ULONG handle, ULONG position, ULONG flags, ULONG identifier, PSZ text)
{
    TRACE_NATIVE("InsertMenu(%u, %u, %X, %u, %p)", handle, position, flags, identifier, text);
    Window *window = menu_window(handle);
    if (!window)
    {
        open32SetError(ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    if (flags & (WIN32_MF_BITMAP | WIN32_MF_OWNERDRAW))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    LONG index;
    if (position == UINT32_MAX)
        index = (LONG)window->menu->count;
    else if (flags & WIN32_MF_BYPOSITION)
        index = position <= window->menu->count ? (LONG)position : -1;
    else
    {
        PmMenuItem *before = menu_item(&window, position, flags, 0);
        index = before ? before->item.iPosition : -1;
    }
    if (index < 0 || window->menu->count >= MENU_ITEMS_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Window *submenu = flags & WIN32_MF_POPUP ? menu_window(identifier) : NULL;
    if ((flags & WIN32_MF_POPUP) && !submenu)
    {
        open32SetError(ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    if (submenu)
    {
        if (menu_contains(submenu, window, 0))
        {
            open32SetError(ERROR_INVALID_PARAMETER);
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
            c = PM_MENU_ACCELERATOR_SEPARATOR;
        copy[used++] = (char)WinCpTranslateChar(0, WIN32_CP_WINANSI, c, PM_MENU_CODEPAGE);
    }
    copy[used] = 0;
    MENUITEM record = {(SHORT)index,
                       (USHORT)((flags & WIN32_MF_SEPARATOR ? MIS_SEPARATOR : 0) |
                                (submenu ? MIS_SUBMENU : 0) |
                                (find_menu(window->hwnd)->system ? MIS_SYSCOMMAND : 0)),
                       (USHORT)((flags & MENU_DISABLED_FLAGS ? MIA_DISABLED : 0) |
                                (flags & WIN32_MF_CHECKED ? MIA_CHECKED : 0)),
                       (USHORT)identifier,
                       submenu ? submenu->hwnd : 0,
                       flags & MENU_STATE_FLAGS};
    LONG result = (LONG)(intptr_t)WinSendMsg(window->hwnd, MM_INSERTITEM, &record, copy);
    free(copy);
    if (result < 0)
    {
        free(label->text);
        free(label);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(ERROR_INVALID_MENU_HANDLE);
        return -1;
    }
    return (LONG)window->menu->count;
}

ULONG GetMenuItemID(ULONG handle, LONG position)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, (ULONG)position, WIN32_MF_BYPOSITION, 0);
    return item && !item->item.hwndSubMenu ? item->item.id : UINT32_MAX;
}

ULONG GetSubMenu(ULONG handle, LONG position)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, (ULONG)position, WIN32_MF_BYPOSITION, 0);
    return item ? item->item.hwndSubMenu : 0;
}

ULONG GetMenuState(ULONG handle, ULONG identity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(ERROR_MENU_ITEM_NOT_FOUND);
        return UINT32_MAX;
    }
    ULONG result = item->item.hItem & MENU_STATE_FLAGS;
    if (item->item.hwndSubMenu)
    {
        Window *sub = menu_window(item->item.hwndSubMenu);
        result |= WIN32_MF_POPUP | (sub ? (sub->menu->count & 255) << MENU_STATE_COUNT_SHIFT : 0);
    }
    return result;
}

ULONG CheckMenuItem(ULONG handle, ULONG identity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(ERROR_MENU_ITEM_NOT_FOUND);
        return UINT32_MAX;
    }
    ULONG old = item->item.hItem & WIN32_MF_CHECKED;
    item->item.hItem = (item->item.hItem & ~(ULONG)WIN32_MF_CHECKED) | (flags & WIN32_MF_CHECKED);
    item->item.afAttribute = (USHORT)((item->item.afAttribute & ~(unsigned)MIA_CHECKED) |
                                      (flags & WIN32_MF_CHECKED ? MIA_CHECKED : 0));
    WinInvalidateRect(window->hwnd, NULL, FALSE);
    return old;
}

ULONG EnableMenuItem(ULONG handle, ULONG identity, ULONG flags)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(ERROR_MENU_ITEM_NOT_FOUND);
        return UINT32_MAX;
    }
    ULONG old = item->item.hItem & MENU_DISABLED_FLAGS;
    item->item.hItem =
        (item->item.hItem & ~(ULONG)MENU_DISABLED_FLAGS) | (flags & MENU_DISABLED_FLAGS);
    item->item.afAttribute = (USHORT)((item->item.afAttribute & ~(unsigned)MIA_DISABLED) |
                                      (flags & MENU_DISABLED_FLAGS ? MIA_DISABLED : 0));
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
        open32SetError(ERROR_MENU_ITEM_NOT_FOUND);
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
    if (destroy)
        prune_menus();
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
        open32SetError(item ? ERROR_INVALID_PARAMETER : ERROR_MENU_ITEM_NOT_FOUND);
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
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    return window->style & WIN32_WS_CHILD ? 0 : window->menu;
}

BOOL SetMenu(ULONG handle, ULONG menu)
{
    TRACE_NATIVE("SetMenu(%u, %u)", (uint)handle, (uint)menu);
    Open32Window *window = open32FindWindow(handle);
    if (!window || (window->style & WIN32_WS_CHILD))
    {
        open32SetError(window ? ERROR_INVALID_PARAMETER : ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    Window *next = menu ? menu_window(menu) : NULL;
    if (menu && (!next || (next->parent->hwnd != HWND_DESKTOP &&
                           next->parent->hwnd != HWND_OBJECT && next->parent->hwnd != handle)))
    {
        open32SetError(ERROR_INVALID_MENU_HANDLE);
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
        WinSetWindowUShort(old, QWS_ID, 0);
    }
    if (menu && !lx_pm_menu_attach(menu, handle))
    {
        if (previous && old != menu)
            lx_pm_menu_attach(old, handle);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    window = open32FindWindow(handle);
    if (!window || window->destroyed)
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    window->menu = menu;
    ULONG client = window->client;
    RECTL bounds;
    if (!WinQueryWindowRect(handle, &bounds))
        return FALSE;
    if (client != handle)
    {
        LONG height = bounds.yTop - (menu ? MENU_BAR_HEIGHT : 0);
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
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    return SetMenu(handle, window->menu);
}

ULONG GetSystemMenu(ULONG handle, BOOL revert)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
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
    if (!(window->style & WIN32_WS_SYSMENU))
        return 0;
    if (window->system_menu && IsMenu(window->system_menu))
        return window->system_menu;
    ULONG menu = CreatePopupMenu();
    if (!menu)
        return 0;
    find_menu(menu)->system = TRUE;
    BOOL minimized = (window->style & WIN32_WS_MINIMIZE) != 0;
    BOOL maximized = (window->style & WIN32_WS_MAXIMIZE) != 0;
    BOOL ok =
        AppendMenu(menu, minimized || maximized ? WIN32_MF_ENABLED : WIN32_MF_GRAYED,
                   WIN32_SC_RESTORE, "&Restore") &&
        AppendMenu(menu, minimized || maximized ? WIN32_MF_GRAYED : WIN32_MF_ENABLED, WIN32_SC_MOVE,
                   "&Move") &&
        AppendMenu(menu,
                   minimized || maximized || !(window->style & WIN32_WS_THICKFRAME)
                       ? WIN32_MF_GRAYED
                       : WIN32_MF_ENABLED,
                   WIN32_SC_SIZE, "&Size") &&
        AppendMenu(menu,
                   minimized || !(window->style & WIN32_WS_MINIMIZEBOX) ? WIN32_MF_GRAYED
                                                                        : WIN32_MF_ENABLED,
                   WIN32_SC_MINIMIZE, "Mi&nimize") &&
        AppendMenu(menu,
                   maximized || !(window->style & WIN32_WS_MAXIMIZEBOX) ? WIN32_MF_GRAYED
                                                                        : WIN32_MF_ENABLED,
                   WIN32_SC_MAXIMIZE, "Ma&ximize") &&
        AppendMenu(menu, WIN32_MF_SEPARATOR, 0, NULL) &&
        AppendMenu(menu,
                   window->class->style & WIN32_CS_NOCLOSE ? WIN32_MF_GRAYED : WIN32_MF_ENABLED,
                   WIN32_SC_CLOSE, "&Close\tAlt+F4") &&
        lx_pm_menu_owner(menu, handle);
    if (!ok)
    {
        DestroyMenu(menu);
        return 0;
    }
    window->system_menu = menu;
    return menu;
}

static BOOL resource_name_equal(const char *first, const char *second)
{
    while (*first && *second)
    {
        unsigned a = (unsigned char)*first++, b = (unsigned char)*second++;
        if (a >= 'a' && a <= 'z')
            a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z')
            b -= 'a' - 'A';
        if (a != b)
            return FALSE;
    }
    return *first == *second;
}

ULONG open32ResourceIdentity(ULONG module, PSZ name, ULONG type)
{
    uintptr_t id = (uintptr_t)name;
    if (id <= UINT16_MAX)
        return (ULONG)id;
    if (*name == '#')
    {
        char *end;
        unsigned long value = strtoul(name + 1, &end, 10);
        return end == name + 1 || *end || value > UINT16_MAX ? 0 : (ULONG)value;
    }
    ULONG size = 0, result = 0;
    void *data = NULL;
    if (!GProcessState.queryResourceSize || !GProcessState.getResource ||
        GProcessState.queryResourceSize(module, RT_RESNAMES, type, &size) ||
        size < RESNAMES_COUNT_SIZE || size > RESNAMES_SIZE_MAX ||
        GProcessState.getResource(module, RT_RESNAMES, type, &data) || !data)
        return 0;
    const uint8 *bytes = data;
    ULONG count = lx_guest_load_u32(bytes);
    BOOL valid = count <= (size - RESNAMES_COUNT_SIZE) / RESNAMES_ID_SIZE;
    size_t offset = valid ? RESNAMES_COUNT_SIZE + (size_t)count * RESNAMES_ID_SIZE : size;
    for (ULONG i = 0; valid && i < count; i++)
    {
        const uint8 *end = memchr(bytes + offset, 0, size - offset);
        if (!end)
        {
            valid = FALSE;
            break;
        }
        if (!result && resource_name_equal(name, (const char *)bytes + offset))
        {
            /* RT_RESNAMES uses DWORD identities; RF_ORDINALID marks an ordinal. */
            ULONG value =
                lx_guest_load_u32(bytes + RESNAMES_COUNT_SIZE + (size_t)i * RESNAMES_ID_SIZE) &
                ~UINT32_C(0x80000000);
            if (value > UINT16_MAX)
                valid = FALSE;
            else
                result = value;
        }
        offset = (size_t)(end - bytes) + 1;
    }
    if (GProcessState.freeResource)
        GProcessState.freeResource(data);
    if (!valid)
    {
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    return result;
}

static ULONG menu_resource_id(ULONG module, PSZ name)
{
    return open32ResourceIdentity(module, name, RT_MENU);
}

static char *loaded_menu_text(const char *source, ULONG codepage)
{
    size_t length = strlen(source);
    if (length > (SIZE_MAX - 1) / 2)
        return NULL;
    char *text = malloc(length * 2 + 1);
    if (!text)
        return NULL;
    size_t used = 0;
    for (size_t i = 0; i < length; i++)
    {
        UCHAR c = (UCHAR)source[i];
        if (c == '~')
        {
            if (source[i + 1] == '~')
                i++;
            else
                c = '&';
        }
        else if (c == '&')
            text[used++] = '&';
        else if (c == PM_MENU_ACCELERATOR_SEPARATOR || c == '\t')
            c = '\t';
        else
            c = WinCpTranslateChar(0, codepage, c, WIN32_CP_WINANSI);
        text[used++] = (char)c;
    }
    text[used] = 0;
    return text;
}

static BOOL adopt_loaded_menu(HWND handle, Open32Menu **pending, unsigned depth, size_t *nodes)
{
    Window *window = lx_pm_menu_window(handle);
    if (!window || depth > LOADED_MENU_DEPTH_MAX || ++*nodes > LOADED_MENU_ITEMS_MAX)
        return FALSE;
    Open32Menu *menu = calloc(1, sizeof *menu);
    if (!menu)
        return FALSE;
    menu->handle = handle;
    menu->popup = depth != 0;
    menu->next = *pending;
    *pending = menu;
    ULONG codepage = window->menu->codepage ? window->menu->codepage : PM_MENU_CODEPAGE;
    ULONG display_codepage =
        GProcessState.process_codepage ? GProcessState.process_codepage : PM_MENU_CODEPAGE;
    MenuText **link = &menu->texts;
    for (size_t i = 0; i < window->menu->count; i++)
    {
        PmMenuItem *item = &window->menu->items[i];
        MenuText *label = calloc(1, sizeof *label);
        if (!label)
            return FALSE;
        *link = label;
        link = &label->next;
        label->text = loaded_menu_text(item->text ? item->text : "", codepage);
        if (!label->text)
            return FALSE;
        /* Normalize resource shortcuts to the same native representation as InsertMenu. */
        for (size_t n = 0; item->text && item->text[n]; n++)
        {
            UCHAR c = (UCHAR)item->text[n];
            item->text[n] = c == '\t' ? PM_MENU_ACCELERATOR_SEPARATOR
                                      : (char)WinCpTranslateChar(0, codepage, c, display_codepage);
        }
        item->item.hItem = (item->item.afStyle & MIS_SEPARATOR ? (ULONG)WIN32_MF_SEPARATOR : 0) |
                           (item->item.afAttribute & MIA_CHECKED ? (ULONG)WIN32_MF_CHECKED : 0) |
                           (item->item.afAttribute & MIA_DISABLED ? (ULONG)WIN32_MF_GRAYED : 0) |
                           (item->item.afAttribute & MIA_HILITED ? (ULONG)WIN32_MF_HILITE : 0);
        if (item->item.hwndSubMenu &&
            !adopt_loaded_menu(item->item.hwndSubMenu, pending, depth + 1, nodes))
            return FALSE;
    }
    return TRUE;
}

ULONG LoadMenu(ULONG module, PSZ resource)
{
    if (!resource)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!open32Presentation())
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    prune_menus();
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG identity = menu_resource_id(module, resource);
    if (!identity)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    ULONG handle = WinLoadMenu(HWND_OBJECT, module, identity);
    if (!handle)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    Open32Menu *pending = NULL;
    size_t nodes = 0;
    if (!adopt_loaded_menu(handle, &pending, 0, &nodes))
    {
        WinDestroyWindow(handle);
        while (pending)
        {
            Open32Menu *menu = pending;
            pending = menu->next;
            free_texts(menu);
            free(menu);
        }
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    Open32Menu **tail = &pending;
    while (*tail)
        tail = &(*tail)->next;
    *tail = menus;
    menus = pending;
    return handle;
}

BOOL ModifyMenu(ULONG handle, ULONG identity, ULONG flags, ULONG identifier, PSZ text)
{
    Window *window = menu_window(handle);
    PmMenuItem *item = menu_item(&window, identity, flags, 0);
    if (!item)
    {
        open32SetError(ERROR_MENU_ITEM_NOT_FOUND);
        return FALSE;
    }
    ULONG position = item->item.iPosition, target = window->hwnd;
    /* Insert first so allocation failure retains the original item. */
    if (!InsertMenu(target, position, flags | WIN32_MF_BYPOSITION, identifier, text))
        return FALSE;
    return RemoveMenu(target, position + 1, WIN32_MF_BYPOSITION);
}

BOOL TrackPopupMenu(ULONG handle, ULONG flags, LONG x, LONG y, LONG reserved, ULONG owner,
                    PVOID exclude)
{
    (void)reserved;
    (void)exclude;
    Window *popup = menu_window(handle);
    if (!owner || !popup || !IsWindow(owner))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG width = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
         height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    if (flags & WIN32_TPM_CENTERALIGN)
        x -= popup->w / 2;
    else
    {
        if ((flags & WIN32_TPM_RIGHTALIGN) && popup->w < x)
            x -= popup->w - 1;
        else if (width <= popup->w + x)
            x -= popup->w - 1;
    }
    find_menu(handle)->popup = TRUE;
    if (!WinPopupMenu(HWND_DESKTOP, owner, handle, x, height - (y + POPUP_Y_OFFSET), 0,
                      POPUP_MENU_FLAGS))
        return FALSE;
    /* Windows returns after the menu closes, with its command already queued. */
    while (WinIsWindowVisible(handle))
    {
        uint8 message[WIN32_MSG_SIZE];
        if (!GetMessage(message, 0, 0, 0))
        {
            PostQuitMessage((LONG)lx_guest_load_u32(message + WIN32_MSG_WPARAM));
            break;
        }
        TranslateMessage(message);
        DispatchMessage(message);
    }
    return TRUE;
}

ULONG GetMenuContextHelpId(ULONG handle)
{
    Open32Menu *menu = find_menu(handle);
    if (!menu)
    {
        open32SetError(ERROR_INVALID_MENU_HANDLE);
        return 0;
    }
    return menu->context_help;
}

BOOL SetMenuContextHelpId(ULONG handle, ULONG help)
{
    Open32Menu *menu = find_menu(handle);
    if (!menu)
    {
        open32SetError(ERROR_INVALID_MENU_HANDLE);
        return FALSE;
    }
    menu->context_help = help;
    return TRUE;
}
