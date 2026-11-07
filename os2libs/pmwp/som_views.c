/* Public domain. No rights reserved. Wholly AI-authored. */
/* WPObject views, menus, drag items, copies, shadows, settings and help. A settings
   view is a frame whose client lists its pages and shows the selected page. Help opens
   the object's help library in the system information viewer. */
#include "os2libs/pmwp/internal.h"
#include "os2libs/doscalls.h"
#include "os2libs/pmdrag.h"
#include "os2libs/pmshapi.h"
#include "os2libs/pmwin.h"
#include "os2libs/sesmgr.h"
#include "guest/abi.h"
#include <stdio.h>
#include <strings.h>

#define ARG(a, n) lx_guest_load_u32((a) + 4 * (n))
enum
{
    TITLE_LIMIT = 4097,
    MENU_QUERY_ITEM = 0x182,
    MENU_INSERT_ITEM = 0x180,
    MENU_REMOVE_ITEM = 0x181,
    MENU_ITEM_COUNT = 0x184,
    MENU_ITEM_TEXT = 0x18b,
    MENU_ITEM_ID = 0x190,
    MENU_END = -1,
    MENU_ID_PRIMARY = 0,
    MENU_ID_DELETE = 109,
    MENU_ID_PROPERTIES = 112,
    MENU_ID_CLOSE = 120,
    CONFIRM_DELETE_FLAG = 1,
    OR_FROMCOPY = 0x10000008,
    OR_SHADOW = 0x10000010,
    /* USEITEM followed by VIEWITEM {view, handle, state, cnr, record}. */
    VIEW_ITEM_BYTES = 28,
    VIEW_VIEW = 8,
    VIEW_HANDLE = 12,
    /* DRAGITEM and DRAGINFO wire offsets. */
    ITEM_ID = 0x04,
    ITEM_TYPE = 0x08,
    ITEM_RMF = 0x0c,
    ITEM_CONTAINER = 0x10,
    ITEM_SOURCE = 0x14,
    ITEM_TARGET = 0x18,
    ITEM_CONTROL = 0x20,
    ITEM_OPERATIONS = 0x22,
    INFO_OPERATION = 0x06,
    INFO_SOURCE = 0x08,
    INFO_ITEMS = 0x10,
    DC_OPEN_FLAG = 1,
    DO_COPYABLE_FLAG = 1,
    DO_MOVEABLE_FLAG = 2,
    DO_LINKABLE_FLAG = 4,
    DO_DEFAULT_OPERATION = 0xbffe,
    DO_CREATE_OPERATION = 0xc002,
    DO_COPY_OPERATION = 0x10,
    DO_LINK_OPERATION = 0x18,
    DO_MOVE_OPERATION = 0x20,
    DOR_DROP_REPLY = 1,
    DOR_NODROPOP_REPLY = 2,
    DOR_NEVERDROP_REPLY = 3,
    SWITCH_VISIBLE = 4,
    SWITCH_JUMPABLE = 2,
    /* PAGEINFO wire offsets. */
    PAGE_WINDOW = 0x04,
    PAGE_PROCEDURE = 0x08,
    PAGE_MODULE = 0x0c,
    PAGE_PARAMETERS = 0x10,
    PAGE_DIALOG = 0x14,
    PAGE_NAME = 0x1c,
    BOOK_LIST_ID = 1,
    BOOK_LIST_WIDTH = 140,
    BOOK_WIDTH = 560,
    BOOK_HEIGHT = 380,
    LIST_INSERT_ITEM = 0x161,
    LIST_SELECT_ITEM = 0x164,
    LIST_QUERY_SELECTION = 0x165,
    LIST_SET_HANDLE = 0x169,
    LIST_QUERY_HANDLE = 0x16a,
    LIST_QUERY_COUNT = 0x160,
    LIST_END = -1,
    LIST_NOTIFY_SELECT = 1,
    GENERAL_TITLE_ID = 2,
    FRAME_FLAGS = 0x3b,
    STYLE_BORDER_ENTRY = 0x20,
    QSV_BOOT_DRIVE_INDEX = 5,
    HELP_LIBRARY_LIMIT = 260
};

static uint32 load16(uint32 address)
{
    uint8 bytes[2] = {0};
    lx_execution_read(GLoaderState.execution, address, bytes, sizeof bytes);
    return lx_guest_load_u16(bytes);
}
static void store16(uint32 address, uint32 value)
{
    uint8 bytes[2];
    lx_guest_store_u16(bytes, (uint16)value);
    lx_execution_write(GLoaderState.execution, address, bytes, sizeof bytes);
}
static uint32 query_style(uint32 self)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryStyle, self, 0, 0, 0);
}

static uint32 wpFilterPopupMenu(uint8 *a)
{
    return ARG(a, 1);
}
static uint32 wpModifyPopupMenu(uint8 *a)
{
    (void)a;
    return TRUE;
}
/* These base methods have no behaviour of their own in PMWP.DLL: subclasses override
   them, and an override calling its parent receives this result. */
static uint32 returns_nothing(uint8 *a)
{
    (void)a;
    return 0;
}

static uint32 wpQueryDefaultView(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 view = pmwpObjectField(self, PMWP_OBJ_DEFAULT_VIEW);
    if (view)
        return view;
    uint32 object_class = pmwpInvokeObject(PMWP_SOMObject_somGetClass, self, 0, 0, 0);
    view =
        pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryDefaultView, object_class, 0, 0, 0);
    if (!view)
        view = PMWP_OPEN_SETTINGS;
    pmwpSetObjectField(self, PMWP_OBJ_DEFAULT_VIEW, view);
    return view;
}
static uint32 wpSetDefaultView(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_DEFAULT_VIEW, ARG(a, 1));
    return TRUE;
}
static uint32 wpQueryConcurrentView(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_CONCURRENT_VIEW);
}
static uint32 wpSetConcurrentView(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_CONCURRENT_VIEW, ARG(a, 1));
    return 0;
}

static uint32 wpQueryDetailsData(uint8 *a)
{
    uint32 self = ARG(a, 0), cursor = ARG(a, 1);
    if (!cursor)
        return pmwpStore32(ARG(a, 2), 12);
    uint32 output = pmwpLoad32(cursor);
    uint32 object_class = pmwpInvokeObject(PMWP_SOMObject_somGetClass, self, 0, 0, 0);
    pmwpStore32(output, pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryTitle, self, 0, 0, 0));
    pmwpStore32(output + 4, pmwpObjectField(self, PMWP_OBJ_STYLE));
    pmwpStore32(output + 8, pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryTitle,
                                       object_class, 0, 0, 0));
    pmwpStore32(cursor, output + 12);
    return TRUE;
}

/* The caller keeps any type, RMF or name it already placed in the item. */
uint32 pmwpFormatDragItem(uint32 self, uint32 item)
{
    if (!item)
        return FALSE;
    char rmf[320] = "<DRM_OBJECT,DRF_OBJECT>";
    uint32 existing = pmwpLoad32(item + ITEM_RMF);
    if (existing)
    {
        DrgQueryStrName(existing, 260, rmf);
        DrgDeleteStrHandle(existing);
    }
    strcat(rmf, ",<DRM_SHAREDMEM,DRF_POINTERDATA>");
    pmwpStore32(item + ITEM_RMF, DrgAddStrHandle(rmf));
    uint32 operations = load16(item + ITEM_OPERATIONS);
    if (!operations)
        operations = DO_COPYABLE_FLAG | DO_MOVEABLE_FLAG | DO_LINKABLE_FLAG;
    uint32 style = query_style(self);
    if (style & PMWP_OBJSTYLE_NOMOVE)
        operations &= ~(uint32)DO_MOVEABLE_FLAG;
    if (style & PMWP_OBJSTYLE_NOCOPY)
        operations &= ~(uint32)DO_COPYABLE_FLAG;
    if (style & PMWP_OBJSTYLE_NOLINK)
        operations &= ~(uint32)DO_LINKABLE_FLAG;
    store16(item + ITEM_OPERATIONS, operations);
    if (!pmwpLoad32(item + ITEM_TYPE))
        pmwpStore32(item + ITEM_TYPE, DrgAddStrHandle("Unknown"));
    if (!load16(item + ITEM_CONTROL) &&
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFindUseItem, self, PMWP_USAGE_OPENVIEW, 0, 0))
        store16(item + ITEM_CONTROL, DC_OPEN_FLAG);
    if (!pmwpLoad32(item + ITEM_ID))
        pmwpStore32(item + ITEM_ID,
                    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryHandle, self, 0, 0, 0));
    uint32 folder = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryFolder, self, 0, 0, 0);
    ULONG parent = folder ? pmwpObjectField(folder, PMWP_OBJ_HANDLE) : 0;
    char path[1024];
    if (!pmwpLoad32(item + ITEM_CONTAINER) && parent && pmwpStorePath(parent, path, sizeof path))
        pmwpStore32(item + ITEM_CONTAINER, DrgAddStrHandle(path));
    char *title = lx_execution_copy_string(
        GLoaderState.execution,
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryTitle, self, 0, 0, 0), TITLE_LIMIT);
    if (title)
    {
        if (!pmwpLoad32(item + ITEM_SOURCE))
            pmwpStore32(item + ITEM_SOURCE, DrgAddStrHandle(title));
        if (!pmwpLoad32(item + ITEM_TARGET))
            pmwpStore32(item + ITEM_TARGET, DrgAddStrHandle(title));
        free(title);
    }
    return TRUE;
}

static uint32 wpFormatDragItem(uint8 *a)
{
    return pmwpFormatDragItem(ARG(a, 0), ARG(a, 1));
}
/* The reply is MRFROM2SHORT(indicator, operation). Styles of each dragged object remove
   the operations it refuses; an object cannot be dropped on itself. */
static uint32 wpDragOver(uint8 *a)
{
    uint32 self = ARG(a, 0), target = ARG(a, 1), info = ARG(a, 2);
    const uint32 never = DOR_NEVERDROP_REPLY | (uint32)DO_DEFAULT_OPERATION << 16;
    if (!info || query_style(self) & (PMWP_OBJSTYLE_NODROP | PMWP_OBJSTYLE_TEMPLATE))
        return never;
    uint32 requested = load16(info + INFO_OPERATION), count = load16(info + INFO_ITEMS);
    uint32 allowed = DO_COPYABLE_FLAG | DO_MOVEABLE_FLAG | DO_LINKABLE_FLAG;
    for (uint32 i = 0; i < count; i++)
    {
        uint32 item = DrgQueryDragitemPtr(info, i);
        if (!item)
            return never;
        uint32 source = pmwpAwakeObject(pmwpLoad32(item + ITEM_ID));
        if (!source)
            continue;
        if (source == self && requested != DO_CREATE_OPERATION)
            return never;
        uint32 style = query_style(source);
        if (style & PMWP_OBJSTYLE_NODRAG)
            return never;
        if (style & PMWP_OBJSTYLE_NOMOVE ||
            (style & PMWP_OBJSTYLE_LOCKEDINPLACE && pmwpLoad32(info + INFO_SOURCE) == target))
            allowed &= ~(uint32)DO_MOVEABLE_FLAG;
        if (style & PMWP_OBJSTYLE_NOCOPY)
            allowed &= ~(uint32)DO_COPYABLE_FLAG;
        if (style & PMWP_OBJSTYLE_NOLINK)
            allowed &= ~(uint32)DO_LINKABLE_FLAG;
    }
    uint32 operation = requested;
    if (requested == DO_DEFAULT_OPERATION)
        operation = allowed & DO_MOVEABLE_FLAG   ? DO_MOVE_OPERATION
                    : allowed & DO_COPYABLE_FLAG ? DO_COPY_OPERATION
                    : allowed & DO_LINKABLE_FLAG ? DO_LINK_OPERATION
                                                 : DO_DEFAULT_OPERATION;
    BOOL permitted = (operation == DO_MOVE_OPERATION && allowed & DO_MOVEABLE_FLAG) ||
                     (operation == DO_COPY_OPERATION && allowed & DO_COPYABLE_FLAG) ||
                     (operation == DO_LINK_OPERATION && allowed & DO_LINKABLE_FLAG);
    return (permitted ? DOR_DROP_REPLY : DOR_NODROPOP_REPLY) | operation << 16;
}

/* Moves the items of a resource menu into a live popup. Each item leaves the loaded
   menu first, so destroying that menu leaves transferred submenus intact. */
static uint32 wpInsertPopupMenuItems(uint8 *a)
{
    HWND menu = ARG(a, 1);
    LONG position = (LONG)ARG(a, 2);
    ULONG submenu = ARG(a, 5);
    if (submenu)
    {
        MENUITEM parent;
        if (!WinSendMsg(menu, MENU_QUERY_ITEM, (MPARAM)(uintptr_t)(submenu | 1u << 16), &parent) ||
            !parent.hwndSubMenu)
            return FALSE;
        menu = parent.hwndSubMenu;
        position = MENU_END;
    }
    HWND loaded = WinLoadMenu(HWND_OBJECT, ARG(a, 3), ARG(a, 4));
    if (!loaded)
        return FALSE;
    BOOL result = TRUE;
    while (result && (uintptr_t)WinSendMsg(loaded, MENU_ITEM_COUNT, 0, 0))
    {
        LONG id = (SHORT)(uintptr_t)WinSendMsg(loaded, MENU_ITEM_ID, 0, 0);
        MENUITEM item;
        char text[256] = "";
        if (id == -1 || !WinSendMsg(loaded, MENU_QUERY_ITEM, (MPARAM)(uintptr_t)(uint16)id, &item))
        {
            result = FALSE;
            break;
        }
        WinSendMsg(loaded, MENU_ITEM_TEXT, (MPARAM)(uintptr_t)((uint16)id | sizeof text << 16),
                   text);
        WinSendMsg(loaded, MENU_REMOVE_ITEM, (MPARAM)(uintptr_t)(uint16)id, 0);
        item.iPosition = (SHORT)position;
        if ((intptr_t)WinSendMsg(menu, MENU_INSERT_ITEM, &item, text) == -1)
            result = FALSE;
        if (position != MENU_END)
            position++;
    }
    WinDestroyWindow(loaded);
    return result;
}

static uint32 wpMenuItemSelected(uint8 *a)
{
    uint32 self = ARG(a, 0);
    switch (ARG(a, 2))
    {
        case MENU_ID_PRIMARY:
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpViewObject, self, 0, PMWP_OPEN_DEFAULT, 0);
            return TRUE;
        case PMWP_OPEN_SETTINGS:
        case MENU_ID_PROPERTIES:
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpViewObject, self, 0, PMWP_OPEN_SETTINGS, 0);
            return TRUE;
        case MENU_ID_DELETE:
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpDelete, self, CONFIRM_DELETE_FLAG, 0, 0);
            return TRUE;
        case MENU_ID_CLOSE:
            return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpClose, self, 0, 0, 0);
        default:
            return FALSE;
    }
}

static void show_page(HWND client, HWND list)
{
    LONG selected =
        (SHORT)(uintptr_t)WinSendMsg(list, LIST_QUERY_SELECTION, (MPARAM)(intptr_t)-1, 0);
    LONG count = (SHORT)(uintptr_t)WinSendMsg(list, LIST_QUERY_COUNT, 0, 0);
    SWP area;
    if (!WinQueryWindowPos(client, &area))
        return;
    for (LONG i = 0; i < count; i++)
    {
        HWND page = (HWND)(uintptr_t)WinSendMsg(list, LIST_QUERY_HANDLE, (MPARAM)(intptr_t)i, 0);
        WinSetWindowPos(page, HWND_TOP, BOOK_LIST_WIDTH, 0, area.cx - BOOK_LIST_WIDTH, area.cy,
                        SWP_MOVE | SWP_SIZE | (i == selected ? SWP_SHOW : SWP_HIDE));
    }
}
static MRESULT OS2API book_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    HWND list = WinWindowFromID(window, BOOK_LIST_ID);
    if (message == WM_SIZE)
    {
        ULONG size = (ULONG)(uintptr_t)second;
        if (list)
            WinSetWindowPos(list, HWND_TOP, 0, 0, BOOK_LIST_WIDTH, (USHORT)(size >> 16),
                            SWP_MOVE | SWP_SIZE);
        show_page(window, list);
        return 0;
    }
    if (message == WM_CONTROL && (USHORT)(uintptr_t)first == BOOK_LIST_ID &&
        ((ULONG)(uintptr_t)first >> 16) == LIST_NOTIFY_SELECT)
    {
        show_page(window, list);
        return 0;
    }
    if (message == WM_CLOSE)
    {
        HWND frame = WinQueryWindow(window, 5);
        return (MRESULT)(uintptr_t)WinDestroyWindow(frame ? frame : window);
    }
    return WinDefWindowProc(window, message, first, second);
}
/* The page identifier is its one-based position in the book. */
static uint32 add_page(HWND book, HWND page, const char *name)
{
    HWND list = WinWindowFromID(book, BOOK_LIST_ID);
    if (!list || !page)
        return 0;
    LONG at = (LONG)(intptr_t)WinSendMsg(list, LIST_INSERT_ITEM, (MPARAM)(intptr_t)LIST_END,
                                         (MPARAM)name);
    if (at < 0)
        return 0;
    WinSendMsg(list, LIST_SET_HANDLE, (MPARAM)(intptr_t)at, (MPARAM)(uintptr_t)page);
    if (!at)
        WinSendMsg(list, LIST_SELECT_ITEM, 0, (MPARAM)(uintptr_t)TRUE);
    show_page(book, list);
    return (uint32)at + 1;
}
static uint32 wpInsertSettingsPage(uint8 *a)
{
    HWND book = ARG(a, 1);
    uint32 info = ARG(a, 2);
    if (!info || !WinWindowFromID(book, BOOK_LIST_ID))
        return 0;
    HWND page = pmwpLoad32(info + PAGE_WINDOW);
    uint32 dialog = load16(info + PAGE_DIALOG);
    if (!page && dialog)
        page = WinLoadDlg(book, book, (PFNWP)(uintptr_t)pmwpLoad32(info + PAGE_PROCEDURE),
                          pmwpLoad32(info + PAGE_MODULE), dialog,
                          (PVOID)(uintptr_t)pmwpLoad32(info + PAGE_PARAMETERS));
    if (!page)
        return 0;
    uint32 name_address = pmwpLoad32(info + PAGE_NAME);
    char *name = name_address
                     ? lx_execution_copy_string(GLoaderState.execution, name_address, TITLE_LIMIT)
                     : strdup("");
    uint32 result = name ? add_page(book, page, name) : 0;
    free(name);
    return result;
}
/* The General page edits the title. An edit takes effect when the page closes, through
   wpSetTitle so that overrides see it. */
static MRESULT OS2API general_page_procedure(HWND window, ULONG message, MPARAM first,
                                             MPARAM second)
{
    if (message == WM_DESTROY)
    {
        uint32 self = WinQueryWindowULong(window, 0);
        HWND entry = WinWindowFromID(window, GENERAL_TITLE_ID);
        char title[TITLE_LIMIT] = "";
        if (self && entry && WinQueryWindowText(entry, sizeof title, title) > 0)
        {
            uint32 guest = pmwpGuestString(title);
            if (guest)
                pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetTitle, self, guest, 0, 0);
            pmwpFree(guest);
        }
    }
    return WinDefWindowProc(window, message, first, second);
}
static uint32 wpAddObjectGeneralPage(uint8 *a)
{
    uint32 self = ARG(a, 0);
    HWND book = ARG(a, 1);
    WinRegisterClass(WinQueryAnchorBlock(HWND_DESKTOP), "2ine.GeneralPage", general_page_procedure,
                     0, 4);
    HWND page =
        WinCreateWindow(book, "2ine.GeneralPage", "", 0, 0, 0, 1, 1, book, HWND_TOP, 0, NULL, NULL);
    if (!page)
        return 0;
    char *title = lx_execution_copy_string(
        GLoaderState.execution,
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryTitle, self, 0, 0, 0), TITLE_LIMIT);
    WinCreateWindow(page, (PSZ)(uintptr_t)WC_STATIC, "Title:", WS_VISIBLE, 16, 300, 80, 20, page,
                    HWND_TOP, 0, NULL, NULL);
    WinCreateWindow(page, (PSZ)(uintptr_t)WC_ENTRYFIELD, title ? title : "",
                    WS_VISIBLE | STYLE_BORDER_ENTRY, 16, 272, 360, 24, page, HWND_TOP,
                    GENERAL_TITLE_ID, NULL, NULL);
    free(title);
    WinSetWindowULong(page, 0, self);
    uint32 result = add_page(book, page, "General");
    if (!result)
        WinDestroyWindow(page);
    return result;
}
static uint32 wpAddSettingsPages(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAddObjectGeneralPage, ARG(a, 0), ARG(a, 1), 0,
                      0) != 0;
}
/* A read-only page of text, for settings that have no editing dialog here. */
uint32 pmwpAddInfoPage(HWND book, const char *name, const char *text)
{
    HWND page = WinCreateWindow(book, (PSZ)(uintptr_t)WC_STATIC, (PSZ)text, 0, 0, 0, 1, 1, book,
                                HWND_TOP, 0, NULL, NULL);
    if (!page)
        return 0;
    uint32 result = add_page(book, page, name);
    if (!result)
        WinDestroyWindow(page);
    return result;
}
uint32 pmwpOpenSettings(uint32 self)
{
    if (query_style(self) & PMWP_OBJSTYLE_NOSETTINGS)
        return 0;
    WinRegisterClass(WinQueryAnchorBlock(HWND_DESKTOP), "2ine.SettingsBook", book_procedure, 0, 0);
    ULONG flags = FRAME_FLAGS;
    HWND client = 0;
    HWND frame = WinCreateStdWindow(HWND_DESKTOP, 0, &flags, "2ine.SettingsBook", "Settings", 0, 0,
                                    0, &client);
    if (!frame)
        return 0;
    if (!WinCreateWindow(client, (PSZ)(uintptr_t)WC_LISTBOX, "", WS_VISIBLE, 0, 0, BOOK_LIST_WIDTH,
                         BOOK_HEIGHT, client, HWND_TOP, BOOK_LIST_ID, NULL, NULL) ||
        !pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAddSettingsPages, self, client, 0, 0))
    {
        WinDestroyWindow(frame);
        return 0;
    }
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpRegisterView, self, frame, 0, 0);
    WinSetWindowPos(frame, HWND_TOP, 60, 60, BOOK_WIDTH, BOOK_HEIGHT,
                    SWP_MOVE | SWP_SIZE | SWP_SHOW | SWP_ACTIVATE);
    pmwpRememberView(self, PMWP_OPEN_SETTINGS, frame);
    return frame;
}

/* The default view is resolved here; views other than settings belong to subclasses. */
static uint32 wpOpen(uint8 *a)
{
    uint32 self = ARG(a, 0), view = ARG(a, 2);
    if (view == PMWP_OPEN_SETTINGS)
        return pmwpOpenSettings(self);
    if (view != PMWP_OPEN_DEFAULT)
        return 0;
    uint32 resolved = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryDefaultView, self, 0, 0, 0);
    return resolved == PMWP_OPEN_DEFAULT
               ? 0
               : pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpOpen, self, ARG(a, 1), resolved, 0);
}
void pmwpRememberView(uint32 self, uint32 view, HWND frame)
{
    uint32 block = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAllocMem, self, VIEW_ITEM_BYTES, 0, 0);
    if (!block)
        return;
    pmwpStore32(block, PMWP_USAGE_OPENVIEW);
    pmwpStore32(block + VIEW_VIEW, view);
    pmwpStore32(block + VIEW_HANDLE, frame);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAddToObjUseList, self, block, 0, 0);
}
/* WPFolder and WPProgram open through the object store. A program has no frame, so a
   successful start returns the nonzero completion value instead. */
static uint32 open_store_view(uint8 *a)
{
    uint32 self = ARG(a, 0), view = ARG(a, 2);
    if (view == PMWP_OPEN_SETTINGS)
        return pmwpOpenSettings(self);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    HWND frame = 0;
    if (!handle || !pmwpStoreOpen(handle, view, TRUE, &frame))
        return 0;
    if (!frame)
        return TRUE;
    pmwpRememberView(self, view, frame);
    return frame;
}
static uint32 wpClose(uint8 *a)
{
    uint32 self = ARG(a, 0);
    HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
    for (uint32 item = pmwpObjectField(self, PMWP_OBJ_USE_LIST); item; item = pmwpLoad32(item + 4))
        if (pmwpLoad32(item) == PMWP_USAGE_OPENVIEW)
        {
            HWND frame = pmwpLoad32(item + VIEW_HANDLE);
            if (WinIsWindow(hab, frame))
                WinPostMsg(frame, WM_CLOSE, 0, 0);
        }
    return TRUE;
}
static uint32 wpSwitchTo(uint8 *a)
{
    uint32 self = ARG(a, 0), view = ARG(a, 1);
    HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
    for (uint32 item = pmwpObjectField(self, PMWP_OBJ_USE_LIST); item; item = pmwpLoad32(item + 4))
    {
        HWND frame = pmwpLoad32(item + VIEW_HANDLE);
        if (pmwpLoad32(item) == PMWP_USAGE_OPENVIEW && pmwpLoad32(item + VIEW_VIEW) == view &&
            WinIsWindow(hab, frame))
        {
            WinSetWindowPos(frame, HWND_TOP, 0, 0, 0, 0, SWP_SHOW | SWP_ZORDER | SWP_ACTIVATE);
            return frame;
        }
    }
    return 0;
}
/* An object without a concurrent-view setting follows PM_Workplace CCVIEW. */
static uint32 wpViewObject(uint8 *a)
{
    uint32 self = ARG(a, 0), cnr = ARG(a, 1), view = ARG(a, 2), parameter = ARG(a, 3);
    if (view == PMWP_OPEN_DEFAULT)
        view = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryDefaultView, self, 0, 0, 0);
    uint32 concurrent =
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryConcurrentView, self, 0, 0, 0);
    if (!concurrent)
    {
        char value[8];
        PrfQueryProfileString(HINI_USERPROFILE, "PM_Workplace", "CCVIEW", "OFF", value,
                              sizeof value);
        concurrent = strcasecmp(value, "ON") ? PMWP_CCVIEW_OFF : PMWP_CCVIEW_ON;
    }
    if (concurrent != PMWP_CCVIEW_ON)
    {
        uint32 existing = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSwitchTo, self, view, 0, 0);
        if (existing)
            return existing;
    }
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpOpen, self, cnr, view, parameter);
}
static uint32 wpRegisterView(uint8 *a)
{
    uint32 self = ARG(a, 0), title_address = ARG(a, 2);
    HWND frame = ARG(a, 1);
    if (!WinIsWindow(WinQueryAnchorBlock(HWND_DESKTOP), frame))
        return FALSE;
    if (!title_address)
        title_address = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryTitle, self, 0, 0, 0);
    char *title = lx_execution_copy_string(GLoaderState.execution, title_address, TITLE_LIMIT);
    if (!title)
        return FALSE;
    WinSetWindowText(frame, title);
    SWCNTRL entry = {0};
    entry.hwnd = frame;
    entry.uchVisibility = SWITCH_VISIBLE;
    entry.fbJump = SWITCH_JUMPABLE;
    snprintf(entry.szSwtitle, sizeof entry.szSwtitle, "%s", title);
    WinAddSwitchEntry(&entry);
    free(title);
    return TRUE;
}

/* The information viewer accepts a library but no numeric panel, so the library opens at
   its first panel. The result reports whether the viewer session started. */
static BOOL start_help_viewer(const char *library)
{
    ULONG boot = 3;
    DosQuerySysInfo(QSV_BOOT_DRIVE_INDEX, QSV_BOOT_DRIVE_INDEX, &boot, sizeof boot);
    char viewer[32], parameters[HELP_LIBRARY_LIMIT];
    snprintf(viewer, sizeof viewer, "%c:\\OS2\\VIEW.EXE", (char)('A' + boot - 1));
    snprintf(parameters, sizeof parameters, "%s", library);
    STARTDATA start = {0};
    start.Length = sizeof start;
    start.PgmName = viewer;
    start.PgmTitle = "Help";
    start.PgmInputs = (PBYTE)parameters;
    start.FgBg = 0;
    ULONG session;
    PID process;
    return DosStartSession(&start, &session, &process) == NO_ERROR;
}
/* An empty library name selects the shell's own help, as class defaults report it. */
static uint32 wpDisplayHelp(uint8 *a)
{
    char *library =
        ARG(a, 2) ? lx_execution_copy_string(GLoaderState.execution, ARG(a, 2), HELP_LIBRARY_LIMIT)
                  : strdup("");
    BOOL result = library && start_help_viewer(*library ? library : "WPHELP.HLP");
    free(library);
    return result;
}
static uint32 wpSetDefaultHelp(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char *library =
        ARG(a, 2) ? lx_execution_copy_string(GLoaderState.execution, ARG(a, 2), HELP_LIBRARY_LIMIT)
                  : NULL;
    if (ARG(a, 2) && !library)
        return FALSE;
    pmwpFree(pmwpObjectField(self, PMWP_OBJ_HELP_LIBRARY));
    pmwpSetObjectField(self, PMWP_OBJ_HELP_LIBRARY, library ? pmwpGuestString(library) : 0);
    pmwpSetObjectField(self, PMWP_OBJ_HELP_PANEL, ARG(a, 1));
    free(library);
    return TRUE;
}
/* An object without its own help reports its class default. */
static uint32 wpQueryDefaultHelp(uint8 *a)
{
    uint32 self = ARG(a, 0), panel = ARG(a, 1), library = ARG(a, 2);
    uint32 own = pmwpObjectField(self, PMWP_OBJ_HELP_LIBRARY);
    uint32 own_panel = pmwpObjectField(self, PMWP_OBJ_HELP_PANEL);
    if (!own && !own_panel)
        return pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryDefaultHelp,
                          pmwpInvokeObject(PMWP_SOMObject_somGetClass, self, 0, 0, 0), panel,
                          library, 0);
    char *text = own ? lx_execution_copy_string(GLoaderState.execution, own, HELP_LIBRARY_LIMIT)
                     : strdup("");
    BOOL result = text && pmwpStore32(panel, own_panel) &&
                  lx_execution_write(GLoaderState.execution, library, text, strlen(text) + 1);
    free(text);
    return result;
}
static uint32 wpMenuItemHelpSelected(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 buffer = pmwpMalloc(HELP_LIBRARY_LIMIT + 4);
    if (!buffer)
        return FALSE;
    BOOL result =
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryDefaultHelp, self, buffer, buffer + 4, 0) &&
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpDisplayHelp, self, pmwpLoad32(buffer), buffer + 4,
                   0);
    pmwpFree(buffer);
    return result;
}

/* Copies, moves and shadows act on the store record. File contents are not copied. */
static uint32 awaken_result(uint32 self, ULONG handle, BOOL lock, uint32 reason)
{
    uint32 object = handle ? pmwpAwaken(handle) : 0;
    if (!object)
        return 0;
    if (lock)
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpLockObject, object, 0, 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpObjectReady, object, reason, self, 0);
    return object;
}
static uint32 copy_stored_object(uint8 *a)
{
    uint32 self = ARG(a, 0), folder = ARG(a, 1);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    ULONG target = folder ? pmwpObjectField(folder, PMWP_OBJ_HANDLE) : 0;
    if (!handle || !target ||
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryStyle, self, 0, 0, 0) & PMWP_OBJSTYLE_NOCOPY)
        return 0;
    return awaken_result(self, WinCopyObject(handle, target, 0), ARG(a, 2), OR_FROMCOPY);
}
static uint32 shadow_with_class(uint32 self, uint32 folder, BOOL lock, uint32 shadow_class)
{
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    ULONG target = folder ? pmwpObjectField(folder, PMWP_OBJ_HANDLE) : 0;
    ULONG shadow = handle && target ? WinCreateShadow(handle, target, 0) : 0;
    if (shadow && shadow_class)
    {
        char *name = lx_execution_copy_string(
            GLoaderState.execution,
            pmwpInvokeClass(PMWP_SOMClass_somGetName, shadow_class, 0, 0, 0), 256);
        BOOL named = name && pmwpStoreSetClass(shadow, name);
        free(name);
        if (!named)
            return 0;
    }
    return awaken_result(self, shadow, lock, OR_SHADOW);
}
static uint32 shadow_object(uint8 *a)
{
    return shadow_with_class(ARG(a, 0), ARG(a, 1), ARG(a, 2), 0);
}
static uint32 wpCreateShadowObjectExt(uint8 *a)
{
    uint32 shadow = shadow_with_class(ARG(a, 0), ARG(a, 1), ARG(a, 2), ARG(a, 4));
    if (shadow && ARG(a, 3))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetup, shadow, ARG(a, 3), 0, 0);
    return shadow;
}
static uint32 wpMoveObject(uint8 *a)
{
    uint32 self = ARG(a, 0), folder = ARG(a, 1);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    ULONG target = folder ? pmwpObjectField(folder, PMWP_OBJ_HANDLE) : 0;
    if (!handle || !target || query_style(self) & PMWP_OBJSTYLE_NOMOVE ||
        !WinMoveObject(handle, target, 0))
        return FALSE;
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetFolder, self, folder, 0, 0);
    return TRUE;
}

const PmwpBody pmwpViewBodies[] = {
    {"WPObject", "wpFilterPopupMenu", wpFilterPopupMenu, 16},
    {"WPObject", "wpModifyPopupMenu", wpModifyPopupMenu, 16},
    {"WPObject", "wpCopiedFromTemplate", returns_nothing, 4},
    {"WPObject", "wpObjectReady", returns_nothing, 12},
    {"WPObject", "wpDroppedOnObject", returns_nothing, 8},
    {"WPObject", "wpPrintObject", returns_nothing, 12},
    {"WPObject", "wpCopyObject", returns_nothing, 12},
    {"WPAbstract", "wpCopyObject", copy_stored_object, 12},
    {"WPObject", "wpCreateShadowObject", shadow_object, 12},
    {"WPObject", "wpCreateShadowObjectExt", wpCreateShadowObjectExt, 20},
    {"WPObject", "wpMoveObject", wpMoveObject, 8},
    {"WPObject", "wpQueryDefaultView", wpQueryDefaultView, 4},
    {"WPObject", "wpSetDefaultView", wpSetDefaultView, 8},
    {"WPObject", "wpQueryConcurrentView", wpQueryConcurrentView, 4},
    {"WPObject", "wpSetConcurrentView", wpSetConcurrentView, 8},
    {"WPObject", "wpQueryDetailsData", wpQueryDetailsData, 12},
    {"WPObject", "wpFormatDragItem", wpFormatDragItem, 8},
    {"WPObject", "wpDragOver", wpDragOver, 12},
    {"WPObject", "wpInsertPopupMenuItems", wpInsertPopupMenuItems, 24},
    {"WPObject", "wpMenuItemSelected", wpMenuItemSelected, 12},
    {"WPObject", "wpOpen", wpOpen, 16},
    {"WPFolder", "wpOpen", open_store_view, 16},
    {"WPProgram", "wpOpen", open_store_view, 16},
    {"WPObject", "wpClose", wpClose, 4},
    {"WPObject", "wpSwitchTo", wpSwitchTo, 8},
    {"WPObject", "wpViewObject", wpViewObject, 16},
    {"WPObject", "wpRegisterView", wpRegisterView, 12},
    {"WPObject", "wpInsertSettingsPage", wpInsertSettingsPage, 12},
    {"WPObject", "wpAddSettingsPages", wpAddSettingsPages, 8},
    {"WPObject", "wpAddObjectGeneralPage", wpAddObjectGeneralPage, 8},
    {"WPObject", "wpAddObjectGeneralPage2", wpAddObjectGeneralPage, 8},
    {"WPObject", "wpDisplayHelp", wpDisplayHelp, 12},
    {"WPObject", "wpSetDefaultHelp", wpSetDefaultHelp, 12},
    {"WPObject", "wpQueryDefaultHelp", wpQueryDefaultHelp, 12},
    {"WPObject", "wpMenuItemHelpSelected", wpMenuItemHelpSelected, 8},
};
const size_t pmwpViewBodyCount = sizeof pmwpViewBodies / sizeof *pmwpViewBodies;
