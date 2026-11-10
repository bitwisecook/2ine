/* Public domain. No rights reserved. Wholly AI-authored. */
/* Resource menus and menu messages share the same item and popup lifetime. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/common/pm_text.h"
#include "emulator/abi.h"
#include <ctype.h>

/* Toolkit bsedos.h resource type. */
enum
{
    RT_MENU = 3
};

/* MT and MTI menu template layouts: an MT header, then MTI items whose text and nested
   submenu template follow inline. The MTI pad word carries the item attributes. */
enum
{
    MT_CODEPAGE_OFFSET = 4,
    MT_CMTI_OFFSET = 8,
    MT_HEADER_SIZE = 10,
    MTI_AFSTYLE_OFFSET = 0,
    MTI_AFATTRIBUTE_OFFSET = 2,
    MTI_IDITEM_OFFSET = 4,
    MTI_HEADER_SIZE = 6
};

enum
{
    MENU_TEMPLATE_LIMIT = 1048576, /* bytes; rejects corrupt length words */
    MENU_MAX_ITEMS = 4096,
    MENU_MAX_DEPTH = 16,
    MENU_OWNER_CHAIN_LIMIT = 4096, /* stops a cyclic owner chain */
    /* Item text before this character is the label; the rest is right-aligned shortcut text. */
    MENU_SHORTCUT_SEPARATOR = 8,
    /* Layout in pels; every value except the text inset is display-scaled. */
    MENU_ROW_HEIGHT = 24,
    MENU_PADDING = 4,
    MENU_MIN_POPUP_WIDTH = 80,
    MENU_POPUP_EXTRA_WIDTH = 64,
    MENU_SEPARATOR_HEIGHT = 8,
    MENU_TEXT_INSET = 12,
    MENU_UNSELECTABLE_STYLES = MIS_SEPARATOR | MIS_STATIC | MIS_BUTTONSEPARATOR
};

Window *lx_pm_menu_window(HWND handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    return window && window->menu ? window : NULL;
}

BOOL createSystemMenu(Window *frame, ULONG flags)
{
    ULONG style = MS_ACTIONBAR | MS_TITLEBUTTON | ((flags & FCF_TITLEBAR) ? WS_VISIBLE : 0);
    HWND bar =
        WinCreateWindow(frame->hwnd, (PSZ)WC_MENU, NULL, style, 0,
                        frame->h > lx_pm_display_scale(MENU_ROW_HEIGHT)
                            ? frame->h - lx_pm_display_scale(MENU_ROW_HEIGHT)
                            : 0,
                        lx_pm_display_scale(MENU_ROW_HEIGHT), lx_pm_display_scale(MENU_ROW_HEIGHT),
                        frame->hwnd, HWND_TOP, FID_SYSMENU, NULL, NULL);
    if (!bar)
        return FALSE;
    HWND popup = WinCreateWindow(HWND_DESKTOP, (PSZ)WC_MENU, NULL, 0, 0, 0, 1, 1, bar, HWND_TOP, 0,
                                 NULL, NULL);
    if (!popup)
        goto fail;
    MENUITEM root = {MIT_END, MIS_TEXT | MIS_SUBMENU, 0, SC_SYSMENU, popup, 0};
    if ((intptr_t)WinSendMsg(bar, MM_INSERTITEM, &root, "~System") == MIT_ERROR)
        goto fail;

    static const struct
    {
        USHORT id;
        ULONG flag;
        const char *text;
    } entries[] = {
        {SC_RESTORE, FCF_MINMAX, "~Restore"},      {SC_MOVE, 0, "~Move"},
        {SC_SIZE, FCF_SIZEBORDER, "~Size"},        {SC_MINIMIZE, FCF_MINBUTTON, "Mi~nimize"},
        {SC_MAXIMIZE, FCF_MAXBUTTON, "Ma~ximize"}, {SC_CLOSE, 0, "~Close"}};

    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++)
    {
        if (entries[i].flag && !(flags & entries[i].flag))
            continue;
        MENUITEM item = {MIT_END, MIS_TEXT | MIS_SYSCOMMAND, 0, entries[i].id, 0, 0};
        if ((intptr_t)WinSendMsg(popup, MM_INSERTITEM, &item, (MPARAM)entries[i].text) == MIT_ERROR)
            goto fail;
    }
    return TRUE;
fail:
    WinDestroyWindow(bar);
    return FALSE;
}

BOOL createMinMaxMenu(Window *frame, ULONG flags)
{
    HWND bar =
        WinCreateWindow(frame->hwnd, (PSZ)WC_MENU, NULL, MS_ACTIONBAR | MS_TITLEBUTTON | WS_VISIBLE,
                        0, 0, 1, 1, frame->hwnd, HWND_TOP, FID_MINMAX, NULL, NULL);
    if (!bar)
        return FALSE;
    const USHORT commands[] = {SC_MINIMIZE, SC_MAXIMIZE};
    const ULONG buttons[] = {FCF_MINBUTTON, FCF_MAXBUTTON};
    for (unsigned i = 0; i < 2; i++)
        if (flags & buttons[i])
        {
            MENUITEM item = {MIT_END, MIS_TEXT | MIS_SYSCOMMAND, 0, commands[i], 0, 0};
            if ((intptr_t)WinSendMsg(bar, MM_INSERTITEM, &item, "") == MIT_ERROR)
            {
                WinDestroyWindow(bar);
                return FALSE;
            }
        }
    return TRUE;
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
    for (unsigned depth = 0; depth < MENU_OWNER_CHAIN_LIMIT; depth++)
    {
        Window *parent = window->menu->active_parent
                             ? lx_pm_menu_window(window->menu->active_parent)
                             : window->owner;
        if (!parent || !parent->menu || parent == window)
            break;
        window = parent;
    }
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
            PmMenuItem *sub = menu_find(lx_pm_menu_window(item->item.hwndSubMenu), id, TRUE, found);
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
        if (text[i] == '~' || text[i] == MENU_SHORTCUT_SEPARATOR)
            continue;
        int step = lx_host_font_advance(font, lx_pm_codepoint((uint8)text[i]));
        if (step > 0)
            width += step;
    }
    return width;
}

static BOOL title_menu_item(const Window *window, const PmMenuItem *item)
{
    if (!(window->style & MS_ACTIONBAR))
        return FALSE;
    if (item->item.id == SC_SYSMENU && item->item.hwndSubMenu)
        return !(item->item.afStyle & (MIS_BITMAP | MIS_OWNERDRAW)); /* Keep custom drawing. */
    return (window->style & MS_TITLEBUTTON) && window->id == FID_MINMAX &&
           isFrameWindow(window->parent) && (item->item.afStyle & MIS_SYSCOMMAND) &&
           (item->item.id == SC_MINIMIZE || item->item.id == SC_MAXIMIZE);
}

static void menu_layout(Window *window)
{
    PmMenu *menu = window->menu;
    HPS ps = WinGetPS(window->hwnd);
    LxHostFont *font = lx_pm_font(ps);
    BOOL bar = (window->style & MS_ACTIONBAR) != 0;
    LONG row = (window->style & MS_TITLEBUTTON) && window->h > 0
                   ? window->h
                   : lx_pm_display_scale(MENU_ROW_HEIGHT),
         padding = lx_pm_display_scale(MENU_PADDING);
    LONG width = bar ? window->w : lx_pm_display_scale(MENU_MIN_POPUP_WIDTH),
         height = bar ? row : padding, x = 0;
    if (!bar)
        for (size_t i = 0; i < menu->count; i++)
        {
            LONG item_width =
                text_width(font, menu->items[i].text) + lx_pm_display_scale(MENU_POPUP_EXTRA_WIDTH);
            if (item_width > width)
                width = item_width;
            height += (menu->items[i].item.afStyle & MIS_SEPARATOR)
                          ? lx_pm_display_scale(MENU_SEPARATOR_HEIGHT)
                          : row;
        }
    LONG y = height - padding / 2;
    for (size_t i = 0; i < menu->count; i++)
    {
        PmMenuItem *item = &menu->items[i];
        LONG glyph_width = row;
        if ((window->style & MS_TITLEBUTTON) && isFrameWindow(window->parent))
        {
            if (window->id == FID_MINMAX)
                glyph_width = WinQuerySysValue(HWND_DESKTOP, SV_CXMINMAXBUTTON);
        }
        LONG w =
            bar ? title_menu_item(window, item) ? glyph_width : text_width(font, item->text) + row
                : width;
        LONG h = bar                                    ? row
                 : (item->item.afStyle & MIS_SEPARATOR) ? lx_pm_display_scale(MENU_SEPARATOR_HEIGHT)
                                                        : row;
        item->rect =
            bar ? (RECTL){x, 0, x + w, row} : (RECTL){padding / 2, y - h, width - padding / 2, y};
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
        Window *sub = lx_pm_menu_window(menu->items[i].item.hwndSubMenu);
        if (sub)
            menu_close(sub);
    }
    if (!(window->style & MS_ACTIONBAR))
        WinShowWindow(window->hwnd, FALSE);
    menu->selected = -1;
    menu->active_parent = 0;
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
        Window *previous = lx_pm_menu_window(menu->items[menu->selected].item.hwndSubMenu);
        if (previous)
            menu_close(previous);
    }
    menu->selected = index;
    WinInvalidateRect(window->hwnd, NULL, FALSE);
}

static void menu_open(Window *window, PmMenuItem *item)
{
    Window *root = menu_root(window), *submenu = lx_pm_menu_window(item->item.hwndSubMenu);
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
    window = lx_pm_menu_window(handle);
    item = menu_find(window, id, FALSE, NULL);
    submenu = item ? lx_pm_menu_window(item->item.hwndSubMenu) : NULL;
    if (!submenu)
        return;
    submenu->menu->active_parent = window->hwnd;
    window->menu->selected = item->item.iPosition;
    menu_layout(submenu);
    POINTL point = {(window->style & MS_ACTIONBAR) ? item->rect.xLeft : window->w,
                    (window->style & MS_ACTIONBAR) ? 0 : item->rect.yTop};
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
    if ((item->item.afAttribute & MIA_DISABLED) || (item->item.afStyle & MENU_UNSELECTABLE_STYLES))
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
    ULONG message = (item->item.afStyle & MIS_SYSCOMMAND) ? WM_SYSCOMMAND
                    : (item->item.afStyle & MIS_HELP)     ? WM_HELP
                                                          : WM_COMMAND;
    USHORT id = item->item.id;
    if (message == WM_SYSCOMMAND)
    {
        /* A merged document's system menu still commands its document frame. */
        Window *origin = window;
        while (origin->owner && origin->owner->menu)
            origin = origin->owner;
        owner = origin->owner ? origin->owner->hwnd : 0;
        Window *frame = origin->owner;
        if (frame && id == SC_MAXIMIZE && (frame->style & WS_MAXIMIZED))
            id = SC_RESTORE;
    }
    menu_finish(window);
    if (owner)
        WinPostMsg(owner, message, (MPARAM)(uintptr_t)id, (MPARAM)(uintptr_t)1);
}

static void menu_paint(Window *window)
{
    menu_layout(window);
    HPS ps = WinBeginPaint(window->hwnd, 0, NULL);
    RECTL whole = {0, 0, window->w, window->h};
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    BOOL active = anchor->active == topLevelWindow(anchor, window->hwnd);
    LONG ordinary_background = (window->style & MS_TITLEBUTTON)
                                   ? (active ? SYSCLR_ACTIVETITLE : SYSCLR_INACTIVETITLE)
                                   : CLR_PALEGRAY;
    LONG ordinary_foreground = (window->style & MS_TITLEBUTTON)
                                   ? (active ? SYSCLR_ACTIVETITLETEXT : SYSCLR_INACTIVETITLETEXT)
                                   : CLR_BLACK;
    WinFillRect(ps, &whole, ordinary_background);
    PmMenu *menu = window->menu;
    for (size_t i = 0; i < menu->count; i++)
    {
        PmMenuItem *item = &menu->items[i];
        RECTL rect = item->rect;
        if (item->item.afStyle & MIS_SEPARATOR)
        {
            RECTL line = {rect.xLeft + 4, (rect.yBottom + rect.yTop) / 2, rect.xRight - 4,
                          (rect.yBottom + rect.yTop) / 2 + 1};
            WinFillRect(ps, &line, CLR_DARKGRAY);
            continue;
        }
        BOOL selected = (int)i == menu->selected;
        LONG foreground = (item->item.afAttribute & MIA_DISABLED) ? CLR_DARKGRAY
                          : selected                              ? CLR_WHITE
                                                                  : ordinary_foreground;
        LONG background = selected ? CLR_DARKBLUE : ordinary_background;
        WinFillRect(ps, &rect, background);
        if (title_menu_item(window, item))
        {
            background = selected ? CLR_DARKBLUE : CLR_PALEGRAY;
            foreground = selected ? CLR_WHITE : CLR_BLACK;
            WinFillRect(ps, &rect, background);
            LONG pel = lx_pm_display_scale(1);
            RECTL top = {rect.xLeft, rect.yTop - pel, rect.xRight, rect.yTop},
                  left = {rect.xLeft, rect.yBottom, rect.xLeft + pel, rect.yTop},
                  bottom = {rect.xLeft, rect.yBottom, rect.xRight, rect.yBottom + pel},
                  right = {rect.xRight - pel, rect.yBottom, rect.xRight, rect.yTop};
            WinFillRect(ps, &top, selected ? CLR_DARKGRAY : CLR_WHITE);
            WinFillRect(ps, &left, selected ? CLR_DARKGRAY : CLR_WHITE);
            WinFillRect(ps, &bottom, selected ? CLR_WHITE : CLR_DARKGRAY);
            WinFillRect(ps, &right, selected ? CLR_WHITE : CLR_DARKGRAY);
            LONG x = (rect.xLeft + rect.xRight) / 2, y = (rect.yBottom + rect.yTop) / 2;
            if (item->item.id == SC_MINIMIZE)
            {
                RECTL glyph = {x - pel * 3, y - pel * 3, x + pel * 3, y - pel};
                WinFillRect(ps, &glyph, foreground);
            }
            else if (item->item.id == SC_MAXIMIZE)
            {
                BOOL restore = window->parent && (window->parent->style & WS_MAXIMIZED);
                LONG side = restore ? 3 : 5;
                RECTL outline[] = {{x - side * pel, y - side * pel, x + side * pel, y + side * pel},
                                   {x - (side - 1) * pel, y - (side - 1) * pel,
                                    x + (side - 1) * pel, y + (side - 2) * pel}};
                WinFillRect(ps, &outline[0], foreground);
                WinFillRect(ps, &outline[1], background);
                if (restore)
                {
                    RECTL back = {x - pel, y + 3 * pel, x + 5 * pel, y + 5 * pel};
                    WinFillRect(ps, &back, foreground);
                }
            }
            else
            {
                RECTL glyph = {x - pel * 5, y - pel * 2, x + pel * 5, y + pel * 2};
                WinFillRect(ps, &glyph, foreground);
            }
            continue;
        }
        rect.xLeft += MENU_TEXT_INSET;
        rect.xRight -= MENU_TEXT_INSET;
        const char *text = item->text ? item->text : "";
        const char *shortcut = strchr(text, MENU_SHORTCUT_SEPARATOR);
        LONG count = shortcut ? (LONG)(shortcut - text) : (LONG)strlen(text);
        WinDrawText(ps, count, (PCH)text, &rect, foreground, background, DT_MNEMONIC | DT_VCENTER);
        if (shortcut)
            WinDrawText(ps, -1, (PCH)shortcut + 1, &rect, foreground, background,
                        DT_RIGHT | DT_VCENTER);
        if (item->item.afAttribute & MIA_CHECKED)
        {
            RECTL check = {item->rect.xLeft + 3, item->rect.yBottom + 9, item->rect.xLeft + 7,
                           item->rect.yBottom + 13};
            WinFillRect(ps, &check, foreground);
        }
        if (item->item.hwndSubMenu && !(window->style & MS_ACTIONBAR))
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
    if (depth > MENU_MAX_DEPTH || size < MT_HEADER_SIZE)
        return 0;
    uint32 length = lx_guest_load_u32(bytes);
    unsigned cp = lx_guest_load_u16(bytes + MT_CODEPAGE_OFFSET),
             count = lx_guest_load_u16(bytes + MT_CMTI_OFFSET);
    if (length < MT_HEADER_SIZE || length > size || (cp && cp != 437 && cp != 850) ||
        count > MENU_MAX_ITEMS)
        return 0;
    HWND handle = WinCreateWindow(parent, (PSZ)WC_MENU, NULL, depth ? 0 : WS_VISIBLE | MS_ACTIONBAR,
                                  0, 0, 1, lx_pm_display_scale(MENU_ROW_HEIGHT), owner, HWND_TOP,
                                  depth ? 0 : FID_MENU, NULL, NULL);
    Window *window = lx_pm_menu_window(handle);
    if (!window)
        return 0;
    PmMenu *menu = window->menu;
    menu->codepage = cp ? cp : GProcessState.process_codepage;
    menu->items = calloc(count ? count : 1, sizeof *menu->items);
    if (!menu->items)
        goto fail;
    size_t offset = MT_HEADER_SIZE;
    for (unsigned i = 0; i < count; i++)
    {
        if (offset > length || length - offset < MTI_HEADER_SIZE)
            goto fail;
        PmMenuItem *item = &menu->items[menu->count++];
        item->item.afStyle = lx_guest_load_u16(bytes + offset + MTI_AFSTYLE_OFFSET);
        item->item.afAttribute = lx_guest_load_u16(bytes + offset + MTI_AFATTRIBUTE_OFFSET);
        item->item.id = lx_guest_load_u16(bytes + offset + MTI_IDITEM_OFFSET);
        offset += MTI_HEADER_SIZE;
        if (item->item.afStyle & (MIS_BITMAP | MIS_OWNERDRAW))
            goto fail;
        if (!(item->item.afStyle & MIS_SEPARATOR))
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
        if (item->item.afStyle & MIS_SUBMENU)
        {
            size_t nested = 0;
            item->item.hwndSubMenu = parse_menu(HWND_DESKTOP, handle, bytes + offset,
                                                length - offset, depth + 1, &nested);
            if (!item->item.hwndSubMenu)
                goto fail;
            WinSetWindowUShort(item->item.hwndSubMenu, QWS_ID, item->item.id);
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

BOOL lx_pm_menu_remove(HWND handle, size_t index, BOOL destroy_submenu)
{
    Window *window = lx_pm_menu_window(handle);
    if (!window || index >= window->menu->count)
        return FALSE;
    PmMenuItem *item = &window->menu->items[index];
    if (destroy_submenu && item->item.hwndSubMenu)
        WinDestroyWindow(item->item.hwndSubMenu);
    free(item->text);
    memmove(item, item + 1, (window->menu->count - index - 1) * sizeof *item);
    window->menu->count--;
    menu_layout(window);
    WinInvalidateRect(handle, NULL, FALSE);
    return TRUE;
}

HWND WinCreateMenu(HWND parent, PVOID data)
{
    if (!data)
        return 0;
    uint32 size = lx_guest_load_u32(data);
    if (size > MENU_TEMPLATE_LIMIT)
        return 0;
    size_t consumed = 0;
    return parse_menu(parent, parent, data, size, parent == HWND_DESKTOP ? 1 : 0, &consumed);
}

HWND WinLoadMenu(HWND frame, HMODULE module, ULONG id)
{
    TRACE_NATIVE("WinLoadMenu(%u, %u, %u)", (uint)frame, (uint)module, (uint)id);
    if (!GProcessState.queryResourceSize || !GProcessState.getResource)
        return 0;
    uint32 size = 0;
    void *data = NULL;
    if (GProcessState.queryResourceSize(module, RT_MENU, id, &size) || size < MT_HEADER_SIZE ||
        size > MENU_TEMPLATE_LIMIT || GProcessState.getResource(module, RT_MENU, id, &data))
        return 0;
    size_t consumed = 0;
    HWND result = parse_menu(frame, frame, data, size, 0, &consumed);
    if (GProcessState.freeResource)
        GProcessState.freeResource(data);
    return result;
}

BOOL WinPopupMenu(HWND parent, HWND owner, HWND handle, LONG x, LONG y, LONG id, ULONG flags)
{
    (void)flags;
    Window *window = lx_pm_menu_window(handle);
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
    if (message == WM_BUTTON1DBLCLK && window->id == FID_SYSMENU && isFrameWindow(window->parent))
    {
        menu_finish(window);
        WinPostMsg(window->parent->hwnd, WM_SYSCOMMAND, (MPARAM)(uintptr_t)SC_CLOSE, 0);
        return 0;
    }
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
                /* Clicking the title of an open menu closes it and returns focus. */
                if ((window->style & MS_ACTIONBAR) && menu->selected == (int)i &&
                    menu->items[i].item.hwndSubMenu &&
                    WinIsWindowVisible(menu->items[i].item.hwndSubMenu))
                    menu_finish(window);
                else
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
                if ((menu->items[i].item.afAttribute & MIA_DISABLED) ||
                    (menu->items[i].item.afStyle & MENU_UNSELECTABLE_STYLES))
                {
                    if (!(window->style & MS_ACTIONBAR))
                        menu_select(window, -1);
                    return 0;
                }
                if (menu->items[i].item.hwndSubMenu && menu_root(window)->menu->focus)
                    menu_activate(window, (int)i);
                else if (!(window->style & MS_ACTIONBAR))
                    menu_select(window, (int)i);
                break;
            }
        return 0;
    }
    if (message == WM_CHAR && !(p & KC_KEYUP))
    {
        unsigned key = q >> 16, character = q & 0xffff;
        if ((p & KC_VIRTUALKEY) && key == VK_ESC)
        {
            menu_finish(window);
            return 0;
        }
        if ((p & KC_VIRTUALKEY) && (key == VK_UP || key == VK_DOWN))
        {
            int direction = key == VK_DOWN ? 1 : -1;
            if (menu->count)
                menu_select(window,
                            (menu->selected + direction + (int)menu->count) % (int)menu->count);
            return 0;
        }
        if ((p & KC_VIRTUALKEY) && (key == VK_ENTER || key == VK_NEWLINE))
        {
            menu_activate(window, menu->selected);
            return 0;
        }
        if (p & KC_CHAR)
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
    if (message == MM_QUERYITEMCOUNT)
        return (MRESULT)(uintptr_t)menu->count;
    if (message == MM_INSERTITEM)
    {
        MENUITEM wire;
        if (!controlRead(first, &wire, sizeof wire))
            return (MRESULT)(intptr_t)MIT_ERROR;
        char *text = second ? controlCopyText(second) : strdup("");
        if (!text)
            return (MRESULT)(intptr_t)MIT_MEMERROR;
        int position = wire.iPosition;
        if (position == MIT_END)
            position = (int)menu->count;
        if (position < 0 || (size_t)position > menu->count || menu->count >= MENU_MAX_ITEMS)
        {
            free(text);
            return (MRESULT)(intptr_t)MIT_ERROR;
        }
        PmMenuItem *items = realloc(menu->items, (menu->count + 1) * sizeof *items);
        if (!items)
        {
            free(text);
            return (MRESULT)(intptr_t)MIT_MEMERROR;
        }
        menu->items = items;
        memmove(items + position + 1, items + position,
                (menu->count - (size_t)position) * sizeof *items);
        items[position] = (PmMenuItem){wire, text, {0}};
        menu->count++;
        menu_layout(window);
        return (MRESULT)(intptr_t)position;
    }
    if (message == MM_SETITEM) /* The low word of mp1 is reserved; MENUITEM carries the id. */
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
    if (message == MM_QUERYITEM)
    {
        if (!item)
            return 0;
        TRACE_EVENT("MENU QUERY hwnd=%u id=%u pos=%d style=%x attr=%x submenu=%u data=%x text=%s",
                    handle, item->item.id, item->item.iPosition, item->item.afStyle,
                    item->item.afAttribute, item->item.hwndSubMenu, item->item.hItem,
                    item->text ? item->text : "");
        return (MRESULT)(uintptr_t)controlWrite(second, &item->item, sizeof item->item);
    }
    if (message == MM_QUERYITEMTEXTLENGTH)
        return (MRESULT)(uintptr_t)(item && item->text ? strlen(item->text) : 0);
    if (message == MM_QUERYITEMTEXT)
    {
        SHORT capacity = (SHORT)(p >> 16);
        if (!item || !item->text || !(item->item.afStyle & MIS_TEXT) || capacity <= 0)
            return 0;
        size_t count = strlen(item->text);
        if (count >= (size_t)capacity)
            count = (size_t)capacity - 1;
        return (MRESULT)(uintptr_t)(controlTextOutput(second, item->text, (size_t)capacity) ? count
                                                                                            : 0);
    }
    if (message == MM_SETITEMTEXT)
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
    if (message == MM_QUERYITEMATTR)
        return (MRESULT)(uintptr_t)(item ? item->item.afAttribute & q : 0);
    if (message == MM_ITEMPOSITIONFROMID)
        return (MRESULT)(intptr_t)(item ? item->item.iPosition : MIT_NONE);
    if (message == MM_ITEMIDFROMPOSITION)
        return (MRESULT)(intptr_t)((p & 0xffff) < menu->count ? menu->items[p & 0xffff].item.id
                                                              : MIT_ERROR);
    if (message == MM_ISITEMVALID)
        return (MRESULT)(uintptr_t)(item != NULL);
    if (message == MM_SETITEMATTR)
    {
        if (!item)
            return 0;
        item->item.afAttribute =
            (USHORT)((item->item.afAttribute & ~(q & 0xffff)) | ((q >> 16) & (q & 0xffff)));
        WinInvalidateRect(found->hwnd, NULL, FALSE);
        return (MRESULT)(uintptr_t)TRUE;
    }
    if (message == MM_SETITEMHANDLE)
    {
        if (!item)
            return 0;
        item->item.hItem = q;
        return (MRESULT)(uintptr_t)TRUE;
    }
    if (message == MM_DELETEITEM || message == MM_REMOVEITEM)
    {
        if (!item)
            return 0;
        size_t index = (size_t)(item - found->menu->items);
        return (MRESULT)(uintptr_t)lx_pm_menu_remove(found->hwnd, index, message == MM_DELETEITEM);
    }
    if (message == MM_ENDMENUMODE)
    {
        menu_finish(window);
        return (MRESULT)(uintptr_t)TRUE;
    }
    return WinDefWindowProc(handle, message, first, second);
}

BOOL lx_pm_menu_attach(HWND handle, HWND parent_handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = lx_pm_menu_window(handle);
    Window *parent = anchor ? getWindowFromHWND(anchor, parent_handle) : NULL;
    if (!window || !parent || parent == &desktop_window || window == parent ||
        (window->parent != &desktop_window && window->parent != &anchor->object_window &&
         window->parent != parent))
        return FALSE;
    for (Window *ancestor = parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == window)
            return FALSE;
    if (window->parent != parent)
    {
        Window **link = &window->parent->children;
        while (*link && *link != window)
            link = &(*link)->sibling;
        if (*link)
            *link = window->sibling;
        lx_host_window_destroy(window->heavy.window);
        window->heavy.window = parent->heavy.window;
        window->parent = parent;
        window->sibling = parent->children;
        parent->children = window;
    }
    window->owner = parent;
    window->style |= MS_ACTIONBAR;
    window->id = FID_MENU;
    if (!WinSetWindowPos(handle, HWND_TOP, 0, parent->h - lx_pm_display_scale(MENU_ROW_HEIGHT),
                         parent->w, lx_pm_display_scale(MENU_ROW_HEIGHT),
                         SWP_MOVE | SWP_SIZE | SWP_SHOW | SWP_ZORDER))
        return FALSE;
    return WinInvalidateRect(handle, NULL, FALSE) && WinUpdateWindow(handle);
}

BOOL lx_pm_menu_owner(HWND handle, HWND owner_handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *menu = lx_pm_menu_window(handle);
    Window *owner = anchor ? getWindowFromHWND(anchor, owner_handle) : NULL;
    if (!menu || !owner || menu == owner)
        return FALSE;
    for (Window *ancestor = owner; ancestor; ancestor = ancestor->owner)
        if (ancestor == menu)
            return FALSE;
    menu->owner = owner;
    return TRUE;
}
