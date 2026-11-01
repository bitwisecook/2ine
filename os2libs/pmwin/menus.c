/* Public domain. No rights reserved. Wholly AI-authored. */
/* Resource menus and menu messages share the same item and popup lifetime. */
#include "os2libs/pmwin/menus.h"
#include "os2libs/pmwin/controls.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pm_text.h"
#include "guest/abi.h"
#include <ctype.h>
static Window *menu_window(HWND handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    return window && window->menu ? window : NULL;
}
BOOL createSystemMenu(Window *frame, ULONG flags)
{
    HWND bar = WinCreateWindow(frame->hwnd, (PSZ)WC_MENU, NULL, WS_VISIBLE | 1, 0,
                               frame->h > 24 ? frame->h - 24 : 0, 24, 24, frame->hwnd, HWND_TOP,
                               FID_SYSMENU, NULL, NULL);
    if (!bar)
        return FALSE;
    HWND popup = WinCreateWindow(HWND_DESKTOP, (PSZ)WC_MENU, NULL, 0, 0, 0, 1, 1, bar, HWND_TOP, 0,
                                 NULL, NULL);
    if (!popup)
        goto fail;
    MENUITEM root = {-1, 0x11, 0, 0x8007, popup, 0}; /* MIS_TEXT | MIS_SUBMENU, SC_SYSMENU */
    if ((intptr_t)WinSendMsg(bar, 0x180, &root, "~System") == -1)
        goto fail;
    static const struct
    {
        USHORT id;
        ULONG flag;
        const char *text;
    } entries[] = {{0x8008, 0x30, "~Restore"},  {0x8001, 0, "~Move"},        {0x8000, 8, "~Size"},
                   {0x8002, 0x10, "Mi~nimize"}, {0x8003, 0x20, "Ma~ximize"}, {0x8004, 0, "~Close"}};
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++)
    {
        if (entries[i].flag && !(flags & entries[i].flag))
            continue;
        MENUITEM item = {-1, 0x41, 0, entries[i].id, 0, 0}; /* MIS_TEXT | MIS_SYSCOMMAND */
        if ((intptr_t)WinSendMsg(popup, 0x180, &item, (MPARAM)entries[i].text) == -1)
            goto fail;
    }
    return TRUE;
fail:
    WinDestroyWindow(bar);
    return FALSE;
}
void destroyMenu(Window *window)
{
    PmMenu *menu = window->menu;
    if (!menu)
        return;
    for (size_t i = 0; i < menu->count; i++)
        free(menu->items[i].text);
    free(menu->items);
    free(menu);
    window->menu = NULL;
}
static Window *menu_root(Window *window)
{
    while (window->owner && window->owner->menu)
        window = window->owner;
    return window;
}
static PmMenuItem *menu_find(Window *window, unsigned id, BOOL recursive, Window **found)
{
    if (!window || !window->menu)
        return NULL;
    PmMenu *menu = window->menu;
    for (size_t i = 0; i < menu->count; i++)
    {
        PmMenuItem *item = &menu->items[i];
        if (item->item.id == id)
        {
            if (found)
                *found = window;
            return item;
        }
        if (recursive && item->item.hwndSubMenu)
        {
            PmMenuItem *sub = menu_find(menu_window(item->item.hwndSubMenu), id, TRUE, found);
            if (sub)
                return sub;
        }
    }
    return NULL;
}
static LONG text_width(LxHostFont *font, const char *text)
{
    LONG width = 0;
    for (size_t i = 0; text && text[i]; i++)
    {
        if (text[i] == '~' || text[i] == 8)
            continue;
        int step = lx_host_font_advance(font, lx_pm_codepoint((uint8)text[i]));
        if (step > 0)
            width += step;
    }
    return width;
}
static void menu_layout(Window *window)
{
    PmMenu *menu = window->menu;
    HPS ps = WinGetPS(window->hwnd);
    LxHostFont *font = lx_pm_font(ps);
    BOOL bar = (window->style & 1) != 0;
    LONG width = bar ? window->w : 80, height = bar ? 24 : 4, x = 0;
    if (!bar)
        for (size_t i = 0; i < menu->count; i++)
        {
            LONG item_width = text_width(font, menu->items[i].text) + 64;
            if (item_width > width)
                width = item_width;
            height += (menu->items[i].item.afStyle & 4) ? 8 : 24;
        }
    LONG y = height - 2;
    for (size_t i = 0; i < menu->count; i++)
    {
        PmMenuItem *item = &menu->items[i];
        LONG w = bar ? text_width(font, item->text) + 24 : width;
        LONG h = bar ? 24 : (item->item.afStyle & 4) ? 8 : 24;
        item->rect = bar ? (RECTL){x, 0, x + w, 24} : (RECTL){2, y - h, width - 2, y};
        x += w;
        y -= h;
        item->item.iPosition = (SHORT)i;
    }
    WinReleasePS(ps);
    if (!bar && (window->w != width || window->h != height))
        WinSetWindowPos(window->hwnd, HWND_TOP, 0, 0, width, height, SWP_SIZE | SWP_NOADJUST);
}
static void menu_close(Window *window)
{
    PmMenu *menu = window->menu;
    for (size_t i = 0; menu && i < menu->count; i++)
    {
        Window *sub = menu_window(menu->items[i].item.hwndSubMenu);
        if (sub)
            menu_close(sub);
    }
    if (!(window->style & 1))
        WinShowWindow(window->hwnd, FALSE);
    menu->selected = -1;
    WinInvalidateRect(window->hwnd, NULL, FALSE);
}
static void menu_finish(Window *window)
{
    Window *root = menu_root(window);
    HWND previous = root->menu->focus;
    root->menu->focus = 0;
    menu_close(root);
    if (previous)
        WinSetFocus(HWND_DESKTOP, previous);
    if (root->owner)
        WinPostMsg(root->owner->hwnd, WM_MENUEND, (MPARAM)(uintptr_t)root->id,
                   (MPARAM)(uintptr_t)root->hwnd);
}
void dismissMenusForClick(AnchorBlock *anchor, Window *target)
{
    Window *target_menu = target;
    while (target_menu && !target_menu->menu)
        target_menu = target_menu->parent;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *window = anchor->windows[i];
        if (!window || !window->hwnd || !window->menu || !window->menu->focus ||
            menu_root(window) != window)
            continue;
        if (!target_menu || menu_root(target_menu) != window)
            menu_finish(window);
    }
}
static void menu_select(Window *window, int index)
{
    PmMenu *menu = window->menu;
    if (menu->selected == index)
        return;
    if (menu->selected >= 0 && (size_t)menu->selected < menu->count)
    {
        Window *previous = menu_window(menu->items[menu->selected].item.hwndSubMenu);
        if (previous)
            menu_close(previous);
    }
    menu->selected = index;
    WinInvalidateRect(window->hwnd, NULL, FALSE);
}
static void menu_open(Window *window, PmMenuItem *item)
{
    Window *root = menu_root(window), *submenu = menu_window(item->item.hwndSubMenu);
    if (!submenu)
        return;
    HWND handle = window->hwnd;
    unsigned id = item->item.id;
    if (!root->menu->focus)
        root->menu->focus = WinQueryFocus(HWND_DESKTOP);
    if (root->owner)
        WinSendMsg(root->owner->hwnd, WM_INITMENU, (MPARAM)(uintptr_t)item->item.id,
                   (MPARAM)(uintptr_t)submenu->hwnd);
    /* WM_INITMENU may replace items or destroy the menu while the owner runs. */
    window = menu_window(handle);
    item = menu_find(window, id, FALSE, NULL);
    submenu = item ? menu_window(item->item.hwndSubMenu) : NULL;
    if (!submenu)
        return;
    window->menu->selected = item->item.iPosition;
    menu_layout(submenu);
    POINTL point = {(window->style & 1) ? item->rect.xLeft : window->w,
                    (window->style & 1) ? 0 : item->rect.yTop};
    WinMapWindowPoints(window->hwnd, HWND_DESKTOP, &point, 1);
    LONG x = point.x, y = point.y - submenu->h;
    if (x + submenu->w > desktop_window.w)
        x = desktop_window.w - submenu->w;
    if (y < 0)
        y = 0;
    WinSetWindowPos(submenu->hwnd, HWND_TOP, x, y, submenu->w, submenu->h,
                    SWP_MOVE | SWP_SIZE | SWP_SHOW);
    WinSetFocus(HWND_DESKTOP, submenu->hwnd);
}
static void menu_activate(Window *window, int index)
{
    PmMenu *menu = window->menu;
    if (index < 0 || (size_t)index >= menu->count)
        return;
    PmMenuItem *item = &menu->items[index];
    if ((item->item.afAttribute & 0x4000) || (item->item.afStyle & (4 | 0x100 | 0x200)))
        return;
    if (menu->selected == index && item->item.hwndSubMenu &&
        WinIsWindowVisible(item->item.hwndSubMenu))
        return;
    menu_select(window, index);
    if (item->item.hwndSubMenu)
    {
        menu_open(window, item);
        return;
    }
    Window *root = menu_root(window);
    HWND owner = root->owner ? root->owner->hwnd : 0;
    ULONG message = (item->item.afStyle & 0x40)   ? WM_SYSCOMMAND
                    : (item->item.afStyle & 0x80) ? WM_HELP
                                                  : WM_COMMAND;
    USHORT id = item->item.id;
    menu_finish(window);
    if (owner)
        WinPostMsg(owner, message, (MPARAM)(uintptr_t)id, (MPARAM)(uintptr_t)1); /* CMDSRC_MENU */
}
static void menu_paint(Window *window)
{
    menu_layout(window);
    HPS ps = WinBeginPaint(window->hwnd, 0, NULL);
    RECTL whole = {0, 0, window->w, window->h};
    WinFillRect(ps, &whole, CLR_PALEGRAY);
    PmMenu *menu = window->menu;
    for (size_t i = 0; i < menu->count; i++)
    {
        PmMenuItem *item = &menu->items[i];
        RECTL rect = item->rect;
        if (item->item.afStyle & 4)
        {
            RECTL line = {rect.xLeft + 4, (rect.yBottom + rect.yTop) / 2, rect.xRight - 4,
                          (rect.yBottom + rect.yTop) / 2 + 1};
            WinFillRect(ps, &line, CLR_DARKGRAY);
            continue;
        }
        BOOL selected = (int)i == menu->selected;
        LONG foreground = (item->item.afAttribute & 0x4000) ? CLR_DARKGRAY
                          : selected                        ? CLR_WHITE
                                                            : CLR_BLACK;
        LONG background = selected ? CLR_DARKBLUE : CLR_PALEGRAY;
        WinFillRect(ps, &rect, background);
        rect.xLeft += 12;
        rect.xRight -= 12;
        const char *text = item->text ? item->text : "";
        const char *shortcut = strchr(text, 8);
        LONG count = shortcut ? (LONG)(shortcut - text) : (LONG)strlen(text);
        WinDrawText(ps, count, (PCH)text, &rect, foreground, background, 0x2400);
        if (shortcut)
            WinDrawText(ps, -1, (PCH)shortcut + 1, &rect, foreground, background, 0x600);
        if (item->item.afAttribute & 0x2000)
        {
            RECTL check = {item->rect.xLeft + 3, item->rect.yBottom + 9, item->rect.xLeft + 7,
                           item->rect.yBottom + 13};
            WinFillRect(ps, &check, foreground);
        }
        if (item->item.hwndSubMenu && !(window->style & 1))
        {
            RECTL arrow = {rect.xRight - 1, rect.yBottom + 9, rect.xRight + 3, rect.yBottom + 13};
            WinFillRect(ps, &arrow, foreground);
        }
    }
    WinEndPaint(ps);
}
static HWND parse_menu(HWND parent, HWND owner, const uint8 *bytes, size_t size, unsigned depth,
                       size_t *consumed)
{
    if (depth > 16 || size < 10)
        return 0;
    uint32 length = lx_guest_load_u32(bytes);
    unsigned cp = lx_guest_load_u16(bytes + 4), count = lx_guest_load_u16(bytes + 8);
    if (length < 10 || length > size || (cp && cp != 437 && cp != 850) || count > 4096)
        return 0;
    HWND handle = WinCreateWindow(parent, (PSZ)WC_MENU, NULL, depth ? 0 : WS_VISIBLE | 1, 0, 0, 1,
                                  24, owner, HWND_TOP, depth ? 0 : FID_MENU, NULL, NULL);
    Window *window = menu_window(handle);
    if (!window)
        return 0;
    PmMenu *menu = window->menu;
    menu->items = calloc(count ? count : 1, sizeof *menu->items);
    if (!menu->items)
        goto fail;
    size_t offset = 10;
    for (unsigned i = 0; i < count; i++)
    {
        if (offset > length || length - offset < 6)
            goto fail;
        PmMenuItem *item = &menu->items[menu->count++];
        item->item.afStyle = lx_guest_load_u16(bytes + offset);
        item->item.afAttribute = lx_guest_load_u16(bytes + offset + 2);
        item->item.id = lx_guest_load_u16(bytes + offset + 4);
        offset += 6;
        if (item->item.afStyle & (2 | 8))
            goto fail;
        if (!(item->item.afStyle & 4))
        {
            const uint8 *end = memchr(bytes + offset, 0, length - offset);
            if (!end)
                goto fail;
            size_t text_size = (size_t)(end - bytes - offset) + 1;
            item->text = malloc(text_size);
            if (!item->text)
                goto fail;
            memcpy(item->text, bytes + offset, text_size);
            offset += text_size;
        }
        if (item->item.afStyle & 0x10)
        {
            size_t nested = 0;
            item->item.hwndSubMenu = parse_menu(HWND_DESKTOP, handle, bytes + offset,
                                                length - offset, depth + 1, &nested);
            if (!item->item.hwndSubMenu)
                goto fail;
            WinSetWindowUShort(item->item.hwndSubMenu, -1, item->item.id);
            offset += nested;
        }
    }
    if (offset != length)
        goto fail;
    *consumed = length;
    menu_layout(window);
    if (depth)
        lx_host_window_border(window->heavy.window, 0);
    return handle;
fail:
    WinDestroyWindow(handle);
    return 0;
}
HWND WinCreateMenu(HWND parent, PVOID data)
{
    if (!data)
        return 0;
    uint32 size = lx_guest_load_u32(data);
    if (size > 1048576)
        return 0;
    size_t consumed = 0;
    return parse_menu(parent, parent, data, size, parent == HWND_DESKTOP ? 1 : 0, &consumed);
}
HWND WinLoadMenu(HWND frame, HMODULE module, ULONG id)
{
    TRACE_NATIVE("WinLoadMenu(%u, %u, %u)", (uint)frame, (uint)module, (uint)id);
    if (!GLoaderState.queryResourceSize || !GLoaderState.getResource)
        return 0;
    uint32 size = 0;
    void *data = NULL;
    if (GLoaderState.queryResourceSize(module, 3, id, &size) || size < 10 || size > 1048576 ||
        GLoaderState.getResource(module, 3, id, &data))
        return 0;
    size_t consumed = 0;
    HWND result = parse_menu(frame, frame, data, size, 0, &consumed);
    if (GLoaderState.freeResource)
        GLoaderState.freeResource(data);
    return result;
}
BOOL WinPopupMenu(HWND parent, HWND owner, HWND handle, LONG x, LONG y, LONG id, ULONG flags)
{
    (void)flags;
    Window *window = menu_window(handle);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *target = anchor ? getWindowFromHWND(anchor, owner) : NULL;
    if (!window || !target)
        return FALSE;
    window->owner = target;
    POINTL point = {x, y};
    if (!WinMapWindowPoints(parent, HWND_DESKTOP, &point, 1))
        return FALSE;
    menu_layout(window);
    if (id)
    {
        PmMenuItem *item = menu_find(window, (unsigned)id, FALSE, NULL);
        if (item)
            window->menu->selected = item->item.iPosition;
    }
    window->menu->focus = WinQueryFocus(HWND_DESKTOP);
    WinSetWindowPos(handle, HWND_TOP, point.x, point.y, window->w, window->h,
                    SWP_MOVE | SWP_SIZE | SWP_SHOW);
    return WinSetFocus(HWND_DESKTOP, handle);
}
MRESULT OS2API menuProcedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return 0;
    if (message == WM_CREATE)
    {
        window->menu = calloc(1, sizeof(PmMenu));
        if (!window->menu)
            return (MRESULT)(uintptr_t)TRUE;
        window->menu->selected = -1;
        return 0;
    }
    PmMenu *menu = window->menu;
    if (!menu)
        return 0;
    ULONG p = (ULONG)(uintptr_t)first, q = (ULONG)(uintptr_t)second;
    if (message == WM_PAINT)
    {
        menu_paint(window);
        return 0;
    }
    if (message == WM_SIZE)
    {
        menu_layout(window);
        return 0;
    }
    if (message == WM_BUTTON1DOWN)
    {
        POINTL point = {(SHORT)p, (SHORT)(p >> 16)};
        for (size_t i = 0; i < menu->count; i++)
            if (WinPtInRect(0, &menu->items[i].rect, &point))
            {
                menu_activate(window, (int)i);
                break;
            }
        return 0;
    }
    if (message == WM_MOUSEMOVE)
    {
        POINTL point = {(SHORT)p, (SHORT)(p >> 16)};
        for (size_t i = 0; i < menu->count; i++)
            if (WinPtInRect(0, &menu->items[i].rect, &point))
            {
                if (menu->selected == (int)i)
                    return 0;
                if ((menu->items[i].item.afAttribute & 0x4000) ||
                    (menu->items[i].item.afStyle & (4 | 0x100 | 0x200)))
                {
                    if (!(window->style & 1))
                        menu_select(window, -1);
                    return 0;
                }
                if (menu->items[i].item.hwndSubMenu && menu_root(window)->menu->focus)
                    menu_activate(window, (int)i);
                else if (!(window->style & 1))
                    menu_select(window, (int)i);
                break;
            }
        return 0;
    }
    if (message == WM_CHAR && !(p & 0x40))
    {
        unsigned key = q >> 16, character = q & 0xffff;
        if ((p & 2) && key == 0x0f)
        {
            menu_finish(window);
            return 0;
        }
        if ((p & 2) && (key == 0x16 || key == 0x18))
        {
            int direction = key == 0x18 ? 1 : -1;
            if (menu->count)
                menu_select(window,
                            (menu->selected + direction + (int)menu->count) % (int)menu->count);
            return 0;
        }
        if ((p & 2) && (key == 0x1e || key == 8))
        {
            menu_activate(window, menu->selected);
            return 0;
        }
        if (p & 1)
            for (size_t i = 0; i < menu->count; i++)
            {
                const char *mnemonic =
                    menu->items[i].text ? strchr(menu->items[i].text, '~') : NULL;
                if (mnemonic && mnemonic[1] && character < 128 &&
                    toupper((unsigned char)mnemonic[1]) == toupper((int)character))
                {
                    menu_activate(window, (int)i);
                    break;
                }
            }
        return 0;
    }
    if (message == 0x184)
        return (MRESULT)(uintptr_t)menu->count;
    if (message == 0x180)
    {
        MENUITEM wire;
        if (!controlRead(first, &wire, sizeof wire))
            return (MRESULT)(intptr_t)-1;
        char *text = second ? controlCopyText(second) : strdup("");
        if (!text)
            return (MRESULT)(intptr_t)-1;
        int position = wire.iPosition;
        if (position == -1)
            position = (int)menu->count;
        if (position < 0 || (size_t)position > menu->count || menu->count >= 4096)
        {
            free(text);
            return (MRESULT)(intptr_t)-1;
        }
        PmMenuItem *items = realloc(menu->items, (menu->count + 1) * sizeof *items);
        if (!items)
        {
            free(text);
            return (MRESULT)(intptr_t)-1;
        }
        menu->items = items;
        memmove(items + position + 1, items + position,
                (menu->count - (size_t)position) * sizeof *items);
        items[position] = (PmMenuItem){wire, text, {0}};
        menu->count++;
        menu_layout(window);
        return (MRESULT)(intptr_t)position;
    }
    if (message == 0x183) /* MM_SETITEM: low param1 is reserved, id comes from MENUITEM. */
    {
        MENUITEM definition;
        if ((p & 0xffff) || !controlRead(second, &definition, sizeof definition))
            return 0;
        Window *target = NULL;
        PmMenuItem *existing = menu_find(window, definition.id, p >> 16, &target);
        if (!existing)
            return 0;
        SHORT position = existing->item.iPosition;
        existing->item = definition;
        existing->item.iPosition = position;
        menu_layout(target);
        WinInvalidateRect(target->hwnd, NULL, FALSE);
        return (MRESULT)(uintptr_t)TRUE;
    }
    Window *found = NULL;
    PmMenuItem *item = menu_find(window, p & 0xffff, p >> 16, &found);
    if (message == 0x182)
    {
        if (!item)
            return 0;
        TRACE_EVENT("MENU QUERY hwnd=%u id=%u pos=%d style=%x attr=%x submenu=%u data=%x text=%s",
                    handle, item->item.id, item->item.iPosition, item->item.afStyle,
                    item->item.afAttribute, item->item.hwndSubMenu, item->item.hItem,
                    item->text ? item->text : "");
        return (MRESULT)(uintptr_t)controlWrite(second, &item->item, sizeof item->item);
    }
    if (message == 0x18c)
        return (MRESULT)(uintptr_t)(item && item->text ? strlen(item->text) : 0);
    if (message == 0x18b)
    {
        SHORT capacity = (SHORT)(p >> 16);
        if (!item || !item->text || !(item->item.afStyle & 1) || capacity <= 0)
            return 0;
        size_t count = strlen(item->text);
        if (count >= (size_t)capacity)
            count = (size_t)capacity - 1;
        return (MRESULT)(uintptr_t)(controlTextOutput(second, item->text, (size_t)capacity) ? count
                                                                                            : 0);
    }
    if (message == 0x18e)
    {
        if (!item)
            return 0;
        char *text = controlCopyText(second);
        if (!text)
            return 0;
        free(item->text);
        item->text = text;
        menu_layout(found);
        return (MRESULT)(uintptr_t)TRUE;
    }
    if (message == 0x191)
        return (MRESULT)(uintptr_t)(item ? item->item.afAttribute & q : 0);
    if (message == 0x18f)
        return (MRESULT)(intptr_t)(item ? item->item.iPosition : -1);
    if (message == 0x190)
        return (MRESULT)(intptr_t)((p & 0xffff) < menu->count ? menu->items[p & 0xffff].item.id
                                                              : -1);
    if (message == 0x193)
        return (MRESULT)(uintptr_t)(item != NULL);
    if (message == 0x192)
    {
        if (!item)
            return 0;
        item->item.afAttribute =
            (USHORT)((item->item.afAttribute & ~(q & 0xffff)) | ((q >> 16) & (q & 0xffff)));
        WinInvalidateRect(found->hwnd, NULL, FALSE);
        return (MRESULT)(uintptr_t)TRUE;
    }
    if (message == 0x18d)
    {
        if (!item)
            return 0;
        item->item.hItem = q;
        return (MRESULT)(uintptr_t)TRUE;
    }
    if (message == 0x181 || message == 0x188)
    {
        if (!item)
            return 0;
        size_t index = (size_t)(item - found->menu->items);
        if (message == 0x181 && item->item.hwndSubMenu)
            WinDestroyWindow(item->item.hwndSubMenu);
        free(item->text);
        memmove(item, item + 1, (found->menu->count - index - 1) * sizeof *item);
        found->menu->count--;
        menu_layout(found);
        return (MRESULT)(uintptr_t)TRUE;
    }
    if (message == 0x186)
    {
        menu_finish(window);
        return (MRESULT)(uintptr_t)TRUE;
    }
    return WinDefWindowProc(handle, message, first, second);
}
