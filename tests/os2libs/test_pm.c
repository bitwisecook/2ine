/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame/client lifetime, queue filtering, retained drawing and timer contracts. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi.h"
#include "guest/handles.h"
#include "guest/abi.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/menus.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static unsigned creates, sizes, changes;
static unsigned hook_calls;
static unsigned selections;
static LONG expected_selection;
static uint8_t icon_resource[88];
static uint32 icon_size = sizeof icon_resource, icon_releases;
static uint32 icon_query(uint32 module, uint32 type, uint32 id, uint32 *size)
{
    if (module || type != 1 || id != 1)
        return 2;
    *size = icon_size;
    return 0;
}
static uint32 icon_get(uint32 module, uint32 type, uint32 id, void **output)
{
    uint32 size;
    uint32 result = icon_query(module, type, id, &size);
    if (!result)
        *output = icon_resource;
    return result;
}
static uint32 icon_release(void *resource)
{
    CHECK(resource == icon_resource);
    icon_releases++;
    return 0;
}
static void pointer_contracts(HPS ps)
{
    for (unsigned offset = 0; offset <= 32; offset += 32)
    {
        uint8_t *header = icon_resource + offset;
        lx_guest_store_u16(header, 0x4943);
        lx_guest_store_u32(header + 2, 26);
        lx_guest_store_u32(header + 10, offset ? 80 : 64);
        lx_guest_store_u32(header + 14, 12);
        lx_guest_store_u16(header + 18, 2);
        lx_guest_store_u16(header + 20, offset ? 2 : 4);
        lx_guest_store_u16(header + 22, 1);
        lx_guest_store_u16(header + 24, 1);
        memset(header + 29, 0xff, 3);
    }
    icon_resource[72] = 0x40; /* Bottom-right AND mask keeps the destination. */
    icon_resource[80] = 0;    /* Transparent pixels have a zero XOR image. */
    icon_resource[84] = 0x80;
    uint32 (*old_query)(uint32, uint32, uint32, uint32 *) = GLoaderState.queryResourceSize;
    uint32 (*old_get)(uint32, uint32, uint32, void **) = GLoaderState.getResource;
    uint32 (*old_free)(void *) = GLoaderState.freeResource;
    GLoaderState.queryResourceSize = icon_query;
    GLoaderState.getResource = icon_get;
    GLoaderState.freeResource = icon_release;
    HPOINTER pointer = WinLoadPointer(HWND_DESKTOP, 0, 1);
    CHECK(pointer && icon_releases == 1);
    RECTL background = {0, 0, 5, 5};
    CHECK(WinFillRect(ps, &background, CLR_RED));
    CHECK(WinDrawPointer(ps, 1, 1, pointer, 0));
    uint32_t pixel;
    CHECK(lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel == 0);
    CHECK(lx_pm_raster_read(ps, 2, 1, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 1, 2, 1, 1, &pixel) && pixel == 0xffffff);
    CHECK(!WinDrawPointer(ps, 1, 1, pointer, 4));
    CHECK(WinDestroyPointer(pointer) && !WinDestroyPointer(pointer));
    icon_size = 30;
    CHECK(!WinLoadPointer(HWND_DESKTOP, 0, 1) && icon_releases == 2);
    CHECK(!WinLoadPointer(HWND_DESKTOP, 0, 2));
    GLoaderState.queryResourceSize = old_query;
    GLoaderState.getResource = old_get;
    GLoaderState.freeResource = old_free;
}
static HWND changing_menu;
static unsigned menu_initializations;
static BOOL OS2API input_hook(HAB hab, PQMSG message, ULONG flags)
{
    CHECK(flags == 1);
    if (message->msg == WM_USER + 100)
    {
        hook_calls++;
        return TRUE;
    }
    if (message->msg == WM_USER + 101)
    {
        hook_calls++;
        message->msg++;
        message->mp1 = (MPARAM)(uintptr_t)1234;
        CHECK(WinReleaseHook(hab, 1, 1, (PFN)input_hook, 0));
    }
    return FALSE;
}
static void hook_contracts(HAB hab, HWND window)
{
    QMSG message;
    while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
        WinDispatchMsg(hab, &message);
    CHECK(WinSetHook(hab, 1, 1, (PFN)input_hook, 0));
    CHECK(WinPostMsg(window, WM_USER + 100, 0, 0));
    CHECK(WinPostMsg(window, WM_USER + 101, 0, 0));
    CHECK(WinGetMsg(hab, &message, 0, 0, 0));
    CHECK(hook_calls == 2 && message.msg == WM_USER + 102 && (uintptr_t)message.mp1 == 1234);
    CHECK(!WinReleaseHook(hab, 1, 1, (PFN)input_hook, 0));
}
static void busy_queue_host_events(HAB hab, HWND window)
{
    QMSG message;
    while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
        WinDispatchMsg(hab, &message);
    CHECK(WinPostMsg(window, WM_USER + 150, 0, 0));
    LxHostEvent quit = {.type = LX_HOST_EVENT_QUIT};
    CHECK(lx_host_video_push(&quit));
    CHECK(WinGetMsg(hab, &message, 0, 0, 0) && message.msg == WM_USER + 150);
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    MessageQueueItem *queued = anchor->message_queue.head;
    while (queued && queued->qmsg.msg != WM_QUIT)
        queued = queued->next;
    CHECK(queued); /* Host pumping cannot wait for the guest queue to become empty. */
    CHECK(!WinGetMsg(hab, &message, 0, 0, 0) && message.msg == WM_QUIT);
}
static MRESULT OS2API procedure(HWND hwnd, ULONG message, MPARAM a, MPARAM b)
{
    if (message == WM_CONTROL && (ULONG)(uintptr_t)a == (902 | 1u << 16))
    {
        CHECK((LONG)(intptr_t)WinSendMsg((HWND)(uintptr_t)b, 0x165, (MPARAM)(intptr_t)-1, 0) ==
              expected_selection);
        selections++;
    }
    if (message == WM_INITMENU && changing_menu)
    {
        if (!menu_initializations++)
        {
            MENUITEM extra = {0, 1, 0, 99, 0, 0};
            CHECK((intptr_t)WinSendMsg(changing_menu, 0x180, &extra, "~Extra") == 0);
        }
        return 0;
    }
    if (message == WM_CREATE)
    {
        CREATESTRUCT *creation = b;
        CHECK(creation && creation->pCtlData == a);
        CHECK(creation->hwndParent == WinQueryWindow(hwnd, 5));
        CHECK(WinWindowFromID(creation->hwndParent, creation->id) == hwnd);
        creates++;
    }
    if (message == WM_PRESPARAMCHANGED)
        changes++;
    if (message == WM_SIZE)
        sizes++;
    return WinDefWindowProc(hwnd, message, a, b);
}
static void accelerator_contracts(HAB hab, HWND frame, HWND client)
{
    const uint8_t table[] = {1, 0, 181, 1, 17, 0, 'S', 0, 42, 0};
    ULONG handle = WinCreateAccelTable(hab, (PVOID)table);
    CHECK(handle && WinSetAccelTable(hab, handle, frame));
    CHECK(WinQueryAccelTable(hab, frame) == handle && !WinDestroyAccelTable(handle));
    uint8_t copy[10];
    CHECK(WinCopyAccelTable(handle, copy, sizeof copy) == sizeof copy &&
          !memcmp(copy, table, sizeof table));
    QMSG message = {client, WM_CHAR, (MPARAM)(uintptr_t)0x10001, (MPARAM)(uintptr_t)'s', 0,
                    {0, 0}, 0};
    CHECK(!WinTranslateAccel(hab, frame, 0, &message) && message.hwnd == client &&
          message.msg == WM_CHAR);
    message.mp1 = (MPARAM)(uintptr_t)0x10011;
    CHECK(WinTranslateAccel(hab, frame, 0, &message));
    CHECK(message.hwnd == frame && message.msg == WM_COMMAND && (uintptr_t)message.mp1 == 42 &&
          (uintptr_t)message.mp2 == 3);
    CHECK(WinSetAccelTable(hab, 0, frame) && WinDestroyAccelTable(handle));
    CHECK(!WinQueryAccelTable(hab, frame) && !WinDestroyAccelTable(handle));
}
static void keyboard_contracts(HAB hab, HWND window)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    CHECK(anchor && WinSetFocus(HWND_DESKTOP, window));
    LxHostEvent event = {0};
    event.type = LX_HOST_EVENT_KEY_DOWN;
    event.key = LX_HOST_KEY_CTRL;
    event.scancode = 0x1d;
    event.modifiers = LX_HOST_MOD_CTRL;
    CHECK(processKeyboardEvent(anchor, &event));
    event.key = LX_HOST_KEY_END;
    event.scancode = 0x4f;
    CHECK(processKeyboardEvent(anchor, &event));
    event.type = LX_HOST_EVENT_KEY_UP;
    CHECK(processKeyboardEvent(anchor, &event));
    event.key = LX_HOST_KEY_CTRL;
    event.scancode = 0x1d;
    event.modifiers = 0;
    CHECK(processKeyboardEvent(anchor, &event));
    CHECK(WinGetPhysKeyState(HWND_DESKTOP, 0x1d) == 1);
    CHECK(WinGetPhysKeyState(HWND_DESKTOP, 0x1d) == 0);
    QMSG message;
    CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
    CHECK((WinGetKeyState(HWND_DESKTOP, 10) & 0x8000) != 0);
    CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
    CHECK((uintptr_t)message.mp2 == 0x001375e0 && ((uintptr_t)message.mp1 >> 24) == 0x65 &&
          (WinGetKeyState(HWND_DESKTOP, 10) & 0x8000) != 0);
    CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
    CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
    CHECK((WinGetKeyState(HWND_DESKTOP, 10) & 0x8000) == 0);
    for (unsigned keypad = 0; keypad < 2; keypad++)
    {
        event.type = LX_HOST_EVENT_KEY_DOWN;
        event.key = keypad ? LX_HOST_KEY_KP_ENTER : LX_HOST_KEY_RETURN;
        event.scancode = 0x1c;
        CHECK(processKeyboardEvent(anchor, &event));
        CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
        CHECK(((uintptr_t)message.mp1 & 7) == 7);
        CHECK((uintptr_t)message.mp2 == ((keypad ? 0x1eu : 8u) << 16 | 13));
        CHECK(((uintptr_t)message.mp1 >> 24) == (keypad ? 0x5au : 0x1cu));
        event.type = LX_HOST_EVENT_KEY_UP;
        CHECK(processKeyboardEvent(anchor, &event));
        CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
        CHECK(((uintptr_t)message.mp1 & 0x47) == 0x46);
        CHECK((uintptr_t)message.mp2 == (keypad ? 0x001ee00du : 0x00081c0du));
    }
    const struct
    {
        unsigned key, scan, plain, control;
    } navigation[] = {
        {LX_HOST_KEY_HOME, 0x60, 0x001447e0, 0x001477e0},
        {LX_HOST_KEY_END, 0x65, 0x00134fe0, 0x001375e0},
        {LX_HOST_KEY_LEFT, 0x63, 0x00154be0, 0x001573e0},
        {LX_HOST_KEY_RIGHT, 0x64, 0x00174de0, 0x001774e0},
        {LX_HOST_KEY_UP, 0x61, 0x001648e0, 0x00168de0},
        {LX_HOST_KEY_DOWN, 0x66, 0x001850e0, 0x001891e0},
        {LX_HOST_KEY_PAGEUP, 0x62, 0x001149e0, 0x001184e0},
        {LX_HOST_KEY_PAGEDOWN, 0x67, 0x001251e0, 0x001276e0},
        {LX_HOST_KEY_INSERT, 0x68, 0x001a52e0, 0x001a92e0},
        {LX_HOST_KEY_DELETE, 0x69, 0x001b53e0, 0x001b93e0},
    };
    for (unsigned i = 0; i < sizeof navigation / sizeof navigation[0]; i++)
        for (unsigned control = 0; control < 2; control++)
            for (unsigned up = 0; up < 2; up++)
            {
                event.type = up ? LX_HOST_EVENT_KEY_UP : LX_HOST_EVENT_KEY_DOWN;
                event.key = navigation[i].key;
                event.modifiers = control ? LX_HOST_MOD_CTRL : 0;
                CHECK(processKeyboardEvent(anchor, &event));
                CHECK(WinGetMsg(hab, &message, window, WM_CHAR, WM_CHAR));
                CHECK(((uintptr_t)message.mp1 >> 24) == navigation[i].scan);
                CHECK(((uintptr_t)message.mp1 & 0x57) == (6u | control << 4 | up << 6));
                CHECK((uintptr_t)message.mp2 ==
                      (control ? navigation[i].control : navigation[i].plain));
            }
}
static void selection_contracts(HWND parent)
{
    HWND list = WinCreateWindow(parent, (PSZ)WC_LISTBOX, "", 0, 0, 0, 120, 80, parent, HWND_TOP,
                                902, NULL, NULL);
    CHECK(list);
    CHECK((intptr_t)WinSendMsg(list, 0x161, (MPARAM)(intptr_t)-1, "first") == 0);
    CHECK((intptr_t)WinSendMsg(list, 0x161, (MPARAM)(intptr_t)-1, "second") == 1);
    expected_selection = 0;
    CHECK(WinSendMsg(list, 0x164, 0, (MPARAM)(uintptr_t)TRUE) && selections == 1);
    CHECK((intptr_t)WinSendMsg(list, 0x165, 0, 0) == 0);
    CHECK((intptr_t)WinSendMsg(list, 0x165, (MPARAM)(uintptr_t)100, 0) == 0);
    CHECK(WinSendMsg(list, 0x164, 0, (MPARAM)(uintptr_t)TRUE) && selections == 1);
    expected_selection = 1;
    CHECK(WinSendMsg(list, 0x164, (MPARAM)(uintptr_t)1, (MPARAM)(uintptr_t)TRUE) &&
          selections == 2);
    expected_selection = -1;
    CHECK(WinSendMsg(list, 0x164, (MPARAM)(intptr_t)-1, 0) && selections == 3);
    CHECK(WinDestroyWindow(list));
}
static void scrollbar_contracts(HAB hab, HWND parent)
{
    uint8_t data[14] = {14, 0, 0, 0, 1, 0, 51, 0, 25, 0, 50, 0, 100, 0};
    HWND scroll = WinCreateWindow(parent, (PSZ)WC_SCROLLBAR, "", WS_VISIBLE | 3, 0, 0, 20, 200,
                                  parent, HWND_TOP, 903, data, NULL);
    CHECK(scroll);
    CHECK((uintptr_t)WinSendMsg(scroll, 0x1a3, 0, 0) == 0x00330001);
    CHECK((uintptr_t)WinSendMsg(scroll, 0x1a2, 0, 0) == 25);
    CHECK(WinSendMsg(scroll, 0x1a1, (MPARAM)(uintptr_t)100, 0));
    CHECK((uintptr_t)WinSendMsg(scroll, 0x1a2, 0, 0) == 51);
    CHECK(WinSendMsg(scroll, 0x1a0, (MPARAM)(intptr_t)-10, (MPARAM)(uintptr_t)0x0005fffe));
    CHECK((uintptr_t)WinSendMsg(scroll, 0x1a3, 0, 0) == 0x00050000);
    CHECK((uintptr_t)WinSendMsg(scroll, 0x1a2, 0, 0) == 0);
    CHECK(WinSendMsg(scroll, 0x1a6, (MPARAM)(uintptr_t)0x00640064, 0));
    CHECK(!WinIsWindowEnabled(scroll));
    CHECK(WinSendMsg(scroll, 0x1a6, (MPARAM)(uintptr_t)0x00640032, 0));
    CHECK(WinIsWindowEnabled(scroll));
    WinSendMsg(scroll, WM_PAINT, 0, 0);
    HPS ps = WinGetPS(scroll);
    uint32_t pixel;
    CHECK(ps && lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel != 0);
    CHECK(WinReleasePS(ps));
    WinSendMsg(scroll, WM_BUTTON1DOWN, (MPARAM)(uintptr_t)(5 | 195u << 16), 0);
    QMSG message;
    CHECK(WinPeekMsg(hab, &message, parent, WM_VSCROLL, WM_VSCROLL, 1));
    CHECK((uintptr_t)message.mp1 == 903 && ((uintptr_t)message.mp2 >> 16) == 1);
    CHECK(WinDestroyWindow(scroll));
    data[0] = 8;
    CHECK(!WinCreateWindow(parent, (PSZ)WC_SCROLLBAR, "", 3, 0, 0, 20, 200, parent, HWND_TOP, 903,
                           data, NULL));
}
static void host_focus_contracts(HAB hab, HWND frame, HWND client)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    CHECK(WinSetActiveWindow(HWND_DESKTOP, frame));
    CHECK(WinSetFocus(HWND_DESKTOP, client));
    LxHostEvent event = {0};
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == frame)
            event.window_data = anchor->windows[i];
    CHECK(event.window_data);
    event.type = LX_HOST_EVENT_KEY_DOWN;
    event.key = LX_HOST_KEY_CTRL;
    event.scancode = 0x1d;
    event.modifiers = LX_HOST_MOD_CTRL;
    CHECK(processHostEvent(anchor, &event));
    QMSG message;
    CHECK(WinGetMsg(hab, &message, client, WM_CHAR, WM_CHAR));
    CHECK(WinGetKeyState(HWND_DESKTOP, 10) & 0x8000);
    event.type = LX_HOST_EVENT_FOCUS_LOST;
    processHostEvent(anchor, &event);
    CHECK(!WinQueryActiveWindow(HWND_DESKTOP) && !WinQueryFocus(HWND_DESKTOP));
    CHECK(!(WinGetKeyState(HWND_DESKTOP, 10) & 0x8000));
    CHECK(!WinGetPhysKeyState(HWND_DESKTOP, 0x1d));
    event.type = LX_HOST_EVENT_FOCUS_GAINED;
    processHostEvent(anchor, &event);
    CHECK(WinQueryActiveWindow(HWND_DESKTOP) == frame && WinQueryFocus(HWND_DESKTOP) == client);
    event.type = LX_HOST_EVENT_KEY_DOWN;
    event.key = LX_HOST_KEY_ESCAPE;
    event.scancode = 1;
    event.modifiers = 0;
    CHECK(processHostEvent(anchor, &event));
    CHECK(WinGetMsg(hab, &message, client, WM_CHAR, WM_CHAR));
    CHECK(message.hwnd == client && ((uintptr_t)message.mp2 & 0xff) == 27);
    event.type = LX_HOST_EVENT_KEY_UP;
    CHECK(processHostEvent(anchor, &event));
    CHECK(WinGetMsg(hab, &message, client, WM_CHAR, WM_CHAR));
}
static void capture_release_contracts(HAB hab, HWND client)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    CHECK(WinSetFocus(HWND_DESKTOP, client));
    LxHostEvent event = {0};
    event.type = LX_HOST_EVENT_KEY_DOWN;
    event.key = LX_HOST_KEY_CTRL;
    event.scancode = 0x1d;
    event.modifiers = LX_HOST_MOD_CTRL;
    CHECK(processKeyboardEvent(anchor, &event));
    event.key = LX_HOST_KEY_UP;
    CHECK(processKeyboardEvent(anchor, &event));
    CHECK(WinPostMsg(client, WM_BUTTON1DOWN, 0, 0));
    QMSG message;
    for (unsigned i = 0; i < 3; i++)
        CHECK(WinGetMsg(hab, &message, client, 0, 0));
    CHECK(WinGetKeyState(HWND_DESKTOP, 10) & 0x8000);
    CHECK(WinGetKeyState(HWND_DESKTOP, 1) & 0x8000);
    CHECK(releaseInputState(anchor, 0));
    CHECK(WinGetMsg(hab, &message, client, WM_CHAR, WM_CHAR));
    CHECK(((uintptr_t)message.mp1 >> 24) == 0x1d && ((uintptr_t)message.mp1 & 0x46) == 0x46);
    CHECK(!(WinGetKeyState(HWND_DESKTOP, 10) & 0x8000));
    CHECK(WinGetMsg(hab, &message, client, WM_CHAR, WM_CHAR));
    CHECK(((uintptr_t)message.mp1 >> 24) == 0x61 && ((uintptr_t)message.mp1 & 0x46) == 0x46);
    CHECK(WinGetMsg(hab, &message, client, WM_BUTTON1UP, WM_BUTTON1UP));
    CHECK(!(WinGetKeyState(HWND_DESKTOP, 1) & 0x8000));
    CHECK(!WinGetPhysKeyState(HWND_DESKTOP, 0x61));
    CHECK(WinQueryFocus(HWND_DESKTOP) == client);
}
static void clipboard_contracts(HAB hab, HWND parent)
{
    ULONG table = WinQuerySystemAtomTable();
    ULONG atom = WinAddAtom(table, "WordPro clipboard format");
    CHECK(atom >= 0xc000 && WinAddAtom(table, "WORDPRO CLIPBOARD FORMAT") == atom);
    CHECK(WinFindAtom(table, "WordPro clipboard format") == atom);
    CHECK(!WinDeleteAtom(table, atom) && WinFindAtom(table, "WordPro clipboard format") == atom);
    CHECK(WinOpenClipbrd(hab) && !WinOpenClipbrd(hab));
    CHECK(WinEmptyClipbrd(hab) && !WinEnumClipbrdFmts(hab, 0));
    ULONG flags = 123;
    CHECK(!WinQueryClipbrdFmtInfo(hab, atom, &flags) && flags == 123);
    CHECK(WinSetClipbrdOwner(hab, parent) && WinQueryClipbrdOwner(hab) == parent);
    CHECK(WinSetClipbrdData(hab, 456, atom, 0x201));
    CHECK(WinEnumClipbrdFmts(hab, 0) == atom && !WinEnumClipbrdFmts(hab, atom));
    CHECK(WinQueryClipbrdFmtInfo(hab, atom, &flags) && flags == 0x201);
    CHECK(WinQueryClipbrdData(hab, atom) == 456);
    CHECK(WinCloseClipbrd(hab) && !WinCloseClipbrd(hab));
    CHECK(!WinSetClipbrdData(hab, 789, atom, 0x201));
    CHECK(WinOpenClipbrd(hab) && WinEmptyClipbrd(hab));
    CHECK(!WinQueryClipbrdOwner(hab) && !WinQueryClipbrdData(hab, atom));
    CHECK(WinCloseClipbrd(hab));
    CHECK(!WinDeleteAtom(table, atom) && !WinFindAtom(table, "WordPro clipboard format"));
}
static void menu_contracts(HWND parent)
{
    const uint8_t empty[] = {10, 0, 0, 0, 181, 1, 0, 0, 0, 0};
    HWND menu = WinCreateMenu(parent, (PVOID)empty);
    CHECK(menu);
    MENUITEM item = {-1, 1, 0, 42, 0, 123};
    CHECK((intptr_t)WinSendMsg(menu, 0x180, &item, "~Item") == 0);
    item.iPosition = 99;
    item.afAttribute = 0x4000;
    item.hItem = 456;
    CHECK(WinSendMsg(menu, 0x183, 0, &item));
    MENUITEM actual;
    CHECK(WinSendMsg(menu, 0x182, (MPARAM)(uintptr_t)42, &actual));
    CHECK(actual.iPosition == 0 && actual.afAttribute == 0x4000 && actual.hItem == 456);
    char text[16];
    CHECK((uintptr_t)WinSendMsg(menu, 0x18b, (MPARAM)(uintptr_t)(42 | sizeof text << 16), text) ==
          5);
    CHECK(!strcmp(text, "~Item"));
    CHECK((uintptr_t)WinSendMsg(menu, 0x18b, (MPARAM)(uintptr_t)(42 | 3u << 16), text) == 2);
    CHECK(!strcmp(text, "~I"));
    item.id = 43;
    CHECK(!WinSendMsg(menu, 0x183, 0, &item));
    CHECK(WinDestroyWindow(menu));
}
static Window *test_window(HWND handle)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == handle)
            return anchor->windows[i];
    return NULL;
}
static MPARAM menu_point(Window *window, unsigned id)
{
    for (size_t i = 0; i < window->menu->count; i++)
        if (window->menu->items[i].item.id == id)
        {
            RECTL rect = window->menu->items[i].rect;
            return (MPARAM)(uintptr_t)((uint16)((rect.xLeft + rect.xRight) / 2) |
                                       (uint32)(uint16)((rect.yBottom + rect.yTop) / 2) << 16);
        }
    CHECK(0);
    return 0;
}
static void paint_scheduling_contracts(HAB hab)
{
    QMSG message;
    while (WinPeekMsg(hab, &message, 0, 0, 0, 1))
        WinDispatchMsg(hab, &message);
    HWND parent = WinCreateWindow(HWND_DESKTOP, "test", "paint order", WS_VISIBLE, 0, 0, 32, 32, 0,
                                  HWND_TOP, 990, NULL, NULL);
    HWND child = WinCreateWindow(parent, "test", "child", WS_VISIBLE, 0, 0, 16, 16, parent,
                                 HWND_TOP, 991, NULL, NULL);
    CHECK(parent && child);
    /* An older child invalidation must not put child painting ahead of its parent. */
    HPS initial = WinBeginPaint(parent, 0, NULL);
    CHECK(initial && WinEndPaint(initial));
    CHECK(WinInvalidateRect(child, NULL, FALSE) && WinInvalidateRect(parent, NULL, FALSE));
    CHECK(WinPostMsg(parent, WM_USER + 151, 0, 0));
    CHECK(WinPeekMsg(hab, &message, parent, 0, 0, 1) && message.msg == WM_USER + 151);
    CHECK(WinPeekMsg(hab, &message, 0, WM_PAINT, WM_PAINT, 1));
    CHECK(message.hwnd == parent);
    /* PM_REMOVE removes no update region: deferred paint is returned again. */
    CHECK(WinPeekMsg(hab, &message, parent, WM_PAINT, WM_PAINT, 1));
    HPS paint = WinBeginPaint(parent, 0, NULL);
    RECTL bounds = {0, 0, 32, 32};
    CHECK(paint && WinFillRect(paint, &bounds, CLR_BLUE) && WinEndPaint(paint));
    CHECK(WinPeekMsg(hab, &message, child, WM_PAINT, WM_PAINT, 1));
    paint = WinBeginPaint(child, 0, NULL);
    CHECK(paint && WinFillRect(paint, &bounds, CLR_RED) && WinEndPaint(paint));
    HPS ps = WinGetPS(child);
    uint32_t pixel;
    CHECK(ps && lx_pm_raster_read(ps, 5, 5, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinReleasePS(ps));
    CHECK(!WinPeekMsg(hab, &message, parent, WM_PAINT, WM_PAINT, 1));
    CHECK(!WinPeekMsg(hab, &message, child, WM_PAINT, WM_PAINT, 1));
    CHECK(WinShowWindow(parent, FALSE) && WinInvalidateRect(child, NULL, FALSE));
    CHECK(!WinPeekMsg(hab, &message, child, WM_PAINT, WM_PAINT, 1));
    CHECK(WinShowWindow(parent, TRUE));
    CHECK(WinGetMsg(hab, &message, parent, WM_PAINT, WM_PAINT) && message.hwnd == parent);
    WinDispatchMsg(hab, &message); /* Default painting must validate too. */
    CHECK(!WinPeekMsg(hab, &message, parent, WM_PAINT, WM_PAINT, 1));
    CHECK(WinDestroyWindow(parent));
}
static void menu_tracking_contracts(HWND parent)
{
    const uint8_t empty[] = {10, 0, 0, 0, 181, 1, 0, 0, 0, 0};
    HWND bar = WinCreateMenu(parent, (PVOID)empty);
    HWND file = WinCreateMenu(HWND_DESKTOP, (PVOID)empty);
    HWND edit = WinCreateMenu(HWND_DESKTOP, (PVOID)empty);
    HWND nested = WinCreateMenu(HWND_DESKTOP, (PVOID)empty);
    CHECK(bar && file && edit && nested);
    Window *bar_window = test_window(bar), *file_window = test_window(file);
    test_window(file)->owner = bar_window;
    test_window(edit)->owner = bar_window;
    test_window(nested)->owner = file_window;
    MENUITEM item = {-1, 0x11, 0, 42, file, 0};
    CHECK((intptr_t)WinSendMsg(bar, 0x180, &item, "~File") == 0);
    item.id = 43;
    item.hwndSubMenu = edit;
    CHECK((intptr_t)WinSendMsg(bar, 0x180, &item, "~Edit") == 1);
    item = (MENUITEM){-1, 0x11, 0, 101, nested, 0};
    CHECK((intptr_t)WinSendMsg(file, 0x180, &item, "~First") == 0);
    item.id = 102;
    item.afStyle = 1;
    item.hwndSubMenu = 0;
    CHECK((intptr_t)WinSendMsg(file, 0x180, &item, "~Second") == 1);
    CHECK(WinSetWindowPos(bar, HWND_TOP, 0, 0, 300, 24, SWP_SIZE | SWP_SHOW));
    CHECK(WinSetFocus(HWND_DESKTOP, parent));
    changing_menu = bar;
    menu_initializations = 0;
    HPS paint = WinBeginPaint(parent, 0, NULL);
    CHECK(paint && WinEndPaint(paint));
    WinSendMsg(bar, WM_BUTTON1DOWN, menu_point(bar_window, 42), 0);
    CHECK(menu_initializations == 1 && WinIsWindowVisible(file));
    RECTL damage;
    CHECK(!WinQueryUpdateRect(parent, &damage));
    CHECK(bar_window->menu->items[bar_window->menu->selected].item.id == 42);
    WinSendMsg(bar, WM_BUTTON1DOWN, menu_point(bar_window, 42), 0);
    CHECK(menu_initializations == 1);
    WinSendMsg(file, WM_MOUSEMOVE, menu_point(file_window, 101), 0);
    CHECK(WinIsWindowVisible(nested));
    WinSendMsg(file, WM_MOUSEMOVE, menu_point(file_window, 102), 0);
    CHECK(file_window->menu->selected == 1 && !WinIsWindowVisible(nested));
    WinSendMsg(file, WM_CHAR, (MPARAM)(uintptr_t)2, (MPARAM)(uintptr_t)(0x16u << 16));
    CHECK(file_window->menu->selected == 0);
    WinSendMsg(file, WM_CHAR, (MPARAM)(uintptr_t)2, (MPARAM)(uintptr_t)(0x18u << 16));
    CHECK(file_window->menu->selected == 1);
    WinSendMsg(bar, WM_MOUSEMOVE, menu_point(bar_window, 43), 0);
    CHECK(menu_initializations == 3 && !WinIsWindowVisible(file) && WinIsWindowVisible(edit));
    changing_menu = 0;
    LxHostEvent click = {.type = LX_HOST_EVENT_BUTTON_DOWN,
                         .window_data = test_window(parent),
                         .x = test_window(parent)->w - 2,
                         .y = 0,
                         .button = 1};
    CHECK(processHostEvent(lxHostThreadState()->anchor_block, &click));
    CHECK(bar_window->menu->selected == -1 && !bar_window->menu->focus);
    CHECK(!WinIsWindowVisible(file) && !WinIsWindowVisible(edit) && !WinIsWindowVisible(nested));
    CHECK(WinDestroyWindow(nested) && WinDestroyWindow(file) && WinDestroyWindow(edit) &&
          WinDestroyWindow(bar));
}
static void list_contracts(HWND parent)
{
    HWND combo = WinCreateWindow(parent, (PSZ)WC_COMBOBOX, "", 4, 0, 0, 180, 120, parent, HWND_TOP,
                                 901, NULL, NULL);
    CHECK(combo);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0004,
                                     "Lotus Word Pro") == -1);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x161, (MPARAM)(intptr_t)-1, "Lotus Word Pro") == 0);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x161, (MPARAM)(intptr_t)-1, "Plain text") == 1);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0004,
                                     "Lotus Word Pro") == 0);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0x00010006, "Lotus") == 0);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0005, "text") == 1);
    CHECK((LONG)(intptr_t)WinSendMsg(combo, 0x16b, (MPARAM)(uintptr_t)0xffff0004,
                                     "lotus word pro") == -1);
    CHECK(WinSendMsg(combo, 0x164, 0, (MPARAM)(uintptr_t)TRUE));
    char text[32];
    CHECK(WinQueryWindowText(combo, sizeof text, text) == 14 && !strcmp(text, "Lotus Word Pro"));
    CHECK(WinDestroyWindow(combo));
}
static void graphics_contracts(HAB hab, HWND window)
{
    CHECK(sizeof(FATTRS) == 56);
    HDC window_dc = WinQueryWindowDC(window);
    CHECK(window_dc == WinOpenWindowDC(window));
    CHECK(!WinQueryWindowDC(0));
    HDC memory = DevOpenDC(hab, 8, "*", 0, NULL, window_dc);
    CHECK(memory && !DevOpenDC(hab, 8, "*", 0, NULL, 0xdeadbeef));
    SIZEL size = {32, 24};
    HPS ps = GpiCreatePS(hab, memory, &size, 0x5008);
    CHECK(ps && GpiQueryDevice(ps) == memory && DevCloseDC(memory) == (ULONG)-1);
    LONG colors[40];
    LONG color_data[4] = {-1, -1, -1, -1};
    CHECK(GpiQueryColorData(ps, 2, color_data) && color_data[0] == 0 && color_data[1] == 0 &&
          color_data[2] == -1);
    CHECK(GpiQueryColorData(ps, 4, color_data) && color_data[2] == 15 && color_data[3] == 0);
    CHECK(!GpiQueryColorData(ps, 5, color_data) && !GpiQueryColorData(0, 4, color_data));
    CHECK(GpiQueryLogColorTable(ps, 0, 0, 40, colors) == 16);
    CHECK(colors[0] == 0xffffff && colors[1] == 0xff && colors[2] == 0xff0000);
    LONG custom = 0x123456;
    CHECK(GpiCreateLogColorTable(ps, 0, 2, 20, 1, &custom));
    CHECK(GpiQueryColorData(ps, 3, color_data) && color_data[0] == 2 && color_data[2] == 20);
    LONG color_state = GpiSavePS(ps);
    CHECK(color_state > 0 && GpiCreateLogColorTable(ps, 0, 3, 0, 0, NULL));
    CHECK(GpiQueryColorData(ps, 3, color_data) && color_data[0] == 3);
    CHECK(GpiRestorePS(ps, color_state) && GpiQueryColorData(ps, 3, color_data) &&
          color_data[0] == 2 && color_data[2] == 20);
    CHECK(GpiQueryLogColorTable(ps, 0, 16, 5, colors) == 5 && colors[0] == -1 &&
          colors[4] == custom);
    CHECK(GpiQueryLogColorTable(ps, 2, 16, 3, colors) == 2 && colors[0] == 20 &&
          colors[1] == custom);
    CHECK(GpiCreateLogColorTable(ps, 0, 0, 0, 0, NULL));
    CHECK(GpiSetColor(ps, CLR_RED) && GpiQueryColor(ps) == CLR_RED);
    CHECK(GpiSetCp(ps, 437) && GpiQueryCp(ps) == 437 && !GpiSetCp(ps, 932));
    FATTRS attrs = {0};
    attrs.usRecordLength = sizeof attrs;
    strcpy(attrs.szFacename, "Courier");
    CHECK(GpiCreateLogFont(ps, NULL, 5, &attrs) == 2 && GpiSetCharSet(ps, 5));
    FONTMETRICS metrics;
    CHECK(GpiQueryFontMetrics(ps, sizeof metrics, &metrics) &&
          !strcmp(metrics.szFamilyname, "Courier"));
    SIZEF box = {24 * 65536, 24 * 65536};
    CHECK(GpiSetCharBox(ps, &box) && GpiQueryFontMetrics(ps, sizeof metrics, &metrics) &&
          metrics.lEmHeight == 24);
    CHECK(metrics.lLowerCaseAscent >= metrics.lXHeight &&
          metrics.lLowerCaseAscent < metrics.lMaxAscender &&
          metrics.lLowerCaseDescent <= metrics.lMaxDescender);
    LONG widths[2];
    CHECK(GpiQueryWidthTable(ps, 'W', 1, widths) && GpiQueryWidthTable(ps, 'i', 1, widths + 1) &&
          widths[0] == widths[1]);
    CHECK(GpiQueryNumberSetIds(ps) == 1 && !GpiSetCharSet(ps, 6));
    CHECK(GpiQueryCharMode(ps) == 1 && GpiSetCharMode(ps, 3));
    CHECK(!GpiSetCharMode(ps, 4));
    LONG saved = GpiSavePS(ps);
    CHECK(saved > 0 && GpiSetCharMode(ps, 0) && GpiQueryCharMode(ps) == 1 && GpiSetBackMix(ps, 2) &&
          GpiQueryBackMix(ps) == 2);
    CHECK(GpiSetCharSet(ps, 0) && GpiRestorePS(ps, saved) && GpiQueryCharSet(ps) == 5 &&
          GpiQueryBackMix(ps) == 5);
    CHECK(!GpiRestorePS(ps, saved) && GpiQueryCharMode(ps) == 3);
    POINTL converted = {16, 12};
    RECTL viewport = {10, 20, 74, 68};
    CHECK(GpiSetPageViewport(ps, &viewport) && GpiConvert(ps, 1, 5, 1, &converted));
    CHECK(converted.x == 42 && converted.y == 44);
    CHECK(GpiConvert(ps, 5, 2, 1, &converted) && converted.x == 16 && converted.y == 12);
    viewport = (RECTL){0, 0, 32, 24};
    CHECK(GpiSetPageViewport(ps, &viewport));
    uint8_t header[64] = {64};
    header[4] = 32;
    header[8] = 24;
    header[12] = 1;
    header[14] = 24;
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(bitmap && GpiSetBitmap(ps, bitmap) == 0 && !GpiDeleteBitmap(bitmap));
    RECTL fill = {0, 0, 32, 24};
    CHECK(WinFillRect(ps, &fill, CLR_RED));
    uint32_t pixel;
    CHECK(lx_pm_raster_read(ps, 3, 4, 1, 1, &pixel) && pixel == 0xff0000);
    RECTL outer = {0, 0, 16, 16}, hole = {4, 4, 12, 12};
    ULONG a = GpiCreateRegion(ps, 1, &outer), b = GpiCreateRegion(ps, 1, &hole),
          clipped = GpiCreateRegion(ps, 0, NULL);
    CHECK(a && b && clipped && GpiCombineRegion(ps, clipped, a, b, 7) == 3);
    RECTL bound;
    CHECK(GpiQueryRegionBox(ps, clipped, &bound) == 3 && WinEqualRect(hab, &bound, &outer));
    ULONG previous = 99;
    CHECK(GpiSetClipRegion(ps, clipped, &previous) == 3 && previous == 0 &&
          !GpiDestroyRegion(ps, clipped));
    CHECK(GpiRectVisible(ps, &hole) == 1 && GpiRectVisible(ps, &outer) == 2);
    CHECK(WinFillRect(ps, &outer, CLR_BLUE));
    CHECK(lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel == 0x0000ff);
    CHECK(lx_pm_raster_read(ps, 8, 8, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(GpiSetClipRegion(ps, 0, &previous) == 2 && previous == clipped);
    CHECK(GpiDestroyRegion(ps, clipped) && GpiDestroyRegion(ps, a) && GpiDestroyRegion(ps, b));
    CHECK(WinFillRect(ps, &outer, CLR_RED));

    ULONG system_bitmap = WinGetSysBitmap(HWND_DESKTOP, 18);
    POINTL origin = {0, 0};
    CHECK(system_bitmap && WinDrawBitmap(ps, system_bitmap, NULL, &origin, CLR_BLUE, CLR_WHITE, 0));
    CHECK(lx_pm_raster_read(ps, 4, 2, 1, 1, &pixel) && pixel == 0xff);
    CHECK(lx_pm_raster_read(ps, 2, 2, 1, 1, &pixel) && pixel == 0xffffff);
    CHECK(GpiDeleteBitmap(system_bitmap));
    CHECK(!WinGetSysBitmap(0, 18) && !WinGetSysBitmap(HWND_DESKTOP, 0));
    pointer_contracts(ps);
    CHECK(WinFillRect(ps, &outer, CLR_RED));
    RECTL border = {2, 2, 12, 12};
    CHECK(WinDrawBorder(ps, &border, 2, 1, CLR_BLUE, CLR_WHITE, 0));
    CHECK(lx_pm_raster_read(ps, 2, 5, 1, 1, &pixel) && pixel == 0xff);
    CHECK(lx_pm_raster_read(ps, 5, 5, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinDrawBorder(ps, &border, 1, 1, CLR_BLUE, CLR_WHITE, 8));
    CHECK(lx_pm_raster_read(ps, 5, 5, 1, 1, &pixel) && pixel == 0xffffff);
    CHECK(WinDrawBorder(ps, &border, 1, 1, CLR_BLUE, CLR_WHITE, 2));
    CHECK(lx_pm_raster_read(ps, 2, 5, 1, 1, &pixel) && pixel == 0xffff00);
    CHECK(!WinDrawBorder(ps, &border, -1, 1, CLR_BLUE, CLR_WHITE, 0));
    CHECK(WinFillRect(ps, &outer, CLR_RED));

    CHECK(GpiSetBitmapId(ps, bitmap, 9) && !GpiSetBitmapId(ps, bitmap, 9));
    CHECK(GpiQueryBitmapHandle(ps, 9) == bitmap && GpiQueryNumberSetIds(ps) == 2);
    LONG types[2], ids[2];
    char names[16];
    CHECK(GpiQuerySetIds(ps, 2, types, names, ids) && ids[0] == 5 && types[0] == 6 && ids[1] == 9 &&
          types[1] == 7);
    uint8_t queried[64] = {64};
    CHECK(GpiQueryBitmapInfoHeader(bitmap, queried) && queried[4] == 32 && queried[8] == 24 &&
          queried[14] == 24);
    memset(queried, 0xa5, sizeof queried);
    CHECK(GpiQueryBitmapParameters(bitmap, queried) && queried[0] == 12 && queried[4] == 32 &&
          queried[12] == 0xa5);
    HPS target = WinGetPS(window);
    POINTL points[3] = {{0, 0}, {32, 24}, {0, 0}};
    CHECK(GpiBitBlt(target, ps, 3, points, 0xcc, 2));
    CHECK(lx_pm_raster_read(target, 3, 4, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinReleasePS(target));
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteSetId(ps, 9) && GpiDeleteBitmap(bitmap));
    CHECK(!GpiDeleteBitmap(bitmap) && GpiDestroyPS(ps) && DevCloseDC(memory) == 0);
    CHECK(!DevQueryCaps(memory, 0, 1, widths));
}
static void frame_state_contracts(void)
{
    HWND parent = WinCreateWindow(HWND_DESKTOP, (PSZ)WC_FRAME, "", 0, 0, 0, 64, 48, 0, HWND_TOP, 0,
                                  NULL, NULL);
    HWND child = WinCreateWindow(parent, (PSZ)WC_FRAME, "", 0, 10, 7, 20, 12, parent, HWND_TOP, 0,
                                 NULL, NULL);
    CHECK(parent && child);
    CHECK(!WinSetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_MAXIMIZE | SWP_RESTORE));
    CHECK(WinSetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_MAXIMIZE));
    SWP position;
    CHECK(WinQueryWindowPos(child, &position) && (position.fl & SWP_MAXIMIZE));
    CHECK(position.x == 0 && position.y == 0 && position.cx == 64 && position.cy == 48);
    CHECK(WinQueryWindowUShort(child, 0x0c) == 10 && WinQueryWindowUShort(child, 0x10) == 20);
    CHECK(WinSetWindowPos(parent, HWND_TOP, 0, 0, 80, 60, SWP_SIZE));
    CHECK(WinQueryWindowPos(child, &position) && position.cx == 80 && position.cy == 60);
    CHECK(WinSetWindowUShort(child, 0x0c, 13));
    CHECK(WinSetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_RESTORE));
    CHECK(WinQueryWindowPos(child, &position) && !(position.fl & SWP_MAXIMIZE));
    CHECK(position.x == 13 && position.y == 7 && position.cx == 20 && position.cy == 12);
    CHECK(WinDestroyWindow(parent));
}
static void dialog_frame_contracts(HWND parent)
{
    HWND dialog = WinCreateWindow(parent, "test", "", WS_VISIBLE, 0, 0, 80, 60, parent, HWND_BOTTOM,
                                  810, NULL, NULL);
    HWND items[4];
    for (unsigned i = 0; i < 4; i++)
        items[i] = WinCreateWindow(dialog, "test", "",
                                   WS_VISIBLE | (i == 0 || i == 3 ? 0x10000 : 0) |
                                       (i == 0 || i == 2 ? 0x20000 : 0),
                                   0, 0, 10, 10, dialog, HWND_BOTTOM, 811 + i, NULL, NULL);
    HWND nested = WinCreateWindow(items[1], "test", "", WS_VISIBLE, 0, 0, 1, 1, items[1],
                                  HWND_BOTTOM, 815, NULL, NULL);
    CHECK(dialog && items[0] && items[1] && items[2] && items[3] && nested);
    CHECK(WinEnumDlgItem(dialog, 0, 0) == items[0]);
    CHECK(WinEnumDlgItem(dialog, 0, 1) == items[2]);
    CHECK(WinEnumDlgItem(dialog, items[2], 2) == items[0]);
    CHECK(WinEnumDlgItem(dialog, items[0], 3) == items[2]);
    CHECK(WinEnumDlgItem(dialog, nested, 4) == items[0]);
    CHECK(WinEnumDlgItem(dialog, nested, 5) == items[2]);
    CHECK(WinEnumDlgItem(dialog, items[2], 6) == items[0]);
    CHECK(WinEnumDlgItem(dialog, items[0], 7) == items[2]);
    CHECK(WinEnumDlgItem(dialog, items[3], 6) == items[3]);
    CHECK(WinEnumDlgItem(dialog, 0xffffffff, 2) == items[0]);
    CHECK(WinDestroyWindow(dialog));
    HWND group = WinCreateWindow(parent, (PSZ)WC_STATIC, "", WS_VISIBLE | 2, 0, 0, 80, 60, parent,
                                 HWND_TOP, 800, NULL, NULL);
    HWND content = WinCreateWindow(parent, "test", "", WS_VISIBLE, 10, 10, 20, 20, parent,
                                   HWND_BOTTOM, 801, NULL, NULL);
    CHECK(group && content);
    RECTL damaged;
    CHECK(WinQueryUpdateRect(content, &damaged));
    CHECK(damaged.xLeft == 0 && damaged.yBottom == 0 && damaged.xRight == 20 && damaged.yTop == 20);
    HPS ps = WinGetPS(content);
    RECTL rect = {0, 0, 20, 20};
    CHECK(WinFillRect(ps, &rect, CLR_RED));
    WinSendMsg(group, WM_PAINT, 0, 0);
    uint32_t pixel;
    CHECK(lx_pm_raster_read(ps, 5, 5, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK((LONG)(uintptr_t)WinSendMsg(group, WM_HITTEST, 0, 0) == HT_TRANSPARENT);
    LxHostEvent click = {.type = LX_HOST_EVENT_BUTTON_DOWN,
                         .window_data = test_window(parent),
                         .x = 15,
                         .y = test_window(parent)->h - 16,
                         .button = 1};
    CHECK(processHostEvent(lxHostThreadState()->anchor_block, &click));
    QMSG event;
    CHECK(WinPeekMsg(0, &event, content, WM_BUTTON1DOWN, WM_BUTTON1DOWN, 1));
    CHECK(WinSetWindowULong(content, -2, WS_VISIBLE | WS_CLIPSIBLINGS));
    CHECK(WinFillRect(ps, &rect, CLR_BLUE));
    CHECK(lx_pm_raster_read(ps, 5, 5, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinReleasePS(ps));
    CHECK(WinDestroyWindow(content) && WinDestroyWindow(group));
}
static void class_clip_contracts(HAB hab, HWND parent)
{
    CHECK(WinRegisterClass(hab, "ClassClipParent", WinDefWindowProc, CS_CLIPCHILDREN, 0));
    HWND panel = WinCreateWindow(parent, "ClassClipParent", "", WS_VISIBLE, 0, 0, 40, 40, parent,
                                 HWND_TOP, 810, NULL, NULL);
    HWND child = WinCreateWindow(panel, (PSZ)WC_STATIC, "", WS_VISIBLE | 1, 10, 10, 10, 10, panel,
                                 HWND_TOP, 811, NULL, NULL);
    CHECK(panel && child);
    HPS ps = WinGetPS(panel), content = WinGetPS(child);
    RECTL area = {0, 0, 40, 40};
    CHECK(WinFillRect(content, &area, CLR_RED));
    CHECK(WinFillRect(ps, &area, CLR_BLUE));
    uint32_t pixel;
    CHECK(lx_pm_raster_read(content, 5, 5, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 5, 5, 1, 1, &pixel) && pixel == 0x0000ff);
    CHECK(WinReleasePS(content) && WinReleasePS(ps));
    CHECK(WinDestroyWindow(panel));
}
static void raster_contracts(HAB hab, HWND window)
{
    HDC dc = DevOpenDC(hab, 8, "*", 0, NULL, 0);
    SIZEL size = {8, 8};
    HPS ps = GpiCreatePS(hab, dc, &size, 0x5008);
    uint8_t header[12] = {12, 0, 0, 0, 8, 0, 8, 0, 1, 0, 24, 0};
    ULONG bitmap = GpiCreateBitmap(ps, header, 0, NULL, NULL);
    CHECK(dc && ps && bitmap && GpiSetBitmap(ps, bitmap) == 0);
    RECTL all = {0, 0, 8, 8};
    CHECK(WinFillRect(ps, &all, CLR_WHITE));
    CHECK(GpiSetColor(ps, CLR_RED));
    POINTL from = {2, 1}, to = {2, 6};
    CHECK(GpiMove(ps, &from) && GpiLine(ps, &to));
    uint32_t pixel;
    CHECK(lx_pm_raster_read(ps, 2, 3, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(GpiSetColor(ps, CLR_WHITE) && GpiSetMix(ps, 4));
    CHECK(GpiMove(ps, &from) && GpiLine(ps, &to));
    CHECK(lx_pm_raster_read(ps, 2, 3, 1, 1, &pixel) && pixel == 0x00ffff);
    CHECK(GpiMove(ps, &from) && GpiLine(ps, &to));
    CHECK(lx_pm_raster_read(ps, 2, 3, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(GpiSetMix(ps, 2) && GpiSetLineType(ps, 8));
    CHECK(GpiMove(ps, &from) && GpiLine(ps, &to));
    CHECK(lx_pm_raster_read(ps, 2, 3, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(GpiSetLineType(ps, 7));
    POINTL segments[2] = {{4, 2}, {6, 2}}, current;
    from = (POINTL){4, 0};
    CHECK(GpiSetColor(ps, CLR_RED) && GpiMove(ps, &from));
    CHECK(GpiPolyLine(ps, 2, segments) && GpiQueryCurrentPosition(ps, &current));
    CHECK(current.x == 6 && current.y == 2);
    CHECK(lx_pm_raster_read(ps, 4, 1, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 5, 2, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(GpiPolyLine(ps, 0, NULL) && !GpiPolyLine(ps, -1, segments));
    RECTL shifted_view = {1, 1, 9, 9}, shifted_clip = {2, 2, 5, 3};
    CHECK(WinFillRect(ps, &all, CLR_WHITE) && GpiSetPageViewport(ps, &shifted_view));
    ULONG region = GpiCreateRegion(ps, 1, &shifted_clip);
    CHECK(region && GpiSetClipRegion(ps, region, NULL) == 2);
    from = (POINTL){1, 1};
    to = (POINTL){3, 1};
    CHECK(GpiMove(ps, &from) && GpiLine(ps, &to));
    CHECK(lx_pm_raster_read(ps, 2, 2, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 4, 2, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel == 0xffffff);
    CHECK(GpiSetClipRegion(ps, 0, NULL) == 2 && GpiDestroyRegion(ps, region));
    shifted_view = (RECTL){0, 0, 16, 16};
    CHECK(GpiSetPageViewport(ps, &shifted_view) && GpiMove(ps, &from) && GpiLine(ps, &to));
    CHECK(lx_pm_raster_read(ps, 6, 2, 1, 1, &pixel) && pixel == 0xff0000);
    shifted_view = (RECTL){0, 0, 8, 8};
    CHECK(GpiSetPageViewport(ps, &shifted_view));
    uint8_t info[18] = {12, 0, 0, 0, 2, 0, 2, 0, 1, 0, 1, 0, 0, 0, 0, 255, 255, 255};
    uint8_t bits[8] = {0x80, 0, 0, 0, 0x40, 0, 0, 0};
    ULONG tile = GpiCreateBitmap(ps, info, 4, bits, info);
    CHECK(tile && GpiSetBitmapId(ps, tile, 7) && GpiSetPatternSet(ps, 7));
    CHECK(GpiSetColor(ps, CLR_BLUE) && GpiSetBackColor(ps, CLR_WHITE) && GpiSetBackMix(ps, 2));
    uint8_t line[28] = {CLR_RED};
    CHECK(GpiSetAttrs(ps, 1, 1, 0, line));
    from = (POINTL){0, 0};
    to = (POINTL){5, 5};
    CHECK(GpiMove(ps, &from) && GpiBox(ps, 3, &to, 0, 0));
    CHECK(lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel == 0x0000ff);
    CHECK(lx_pm_raster_read(ps, 2, 1, 1, 1, &pixel) && pixel == 0xffffff);
    CHECK(lx_pm_raster_read(ps, 0, 1, 1, 1, &pixel) && pixel == 0xff0000);
    uint8_t area[24] = {CLR_BLUE, 0, 0, 0, 0xfe, 0xff, 0xff, 0xff, 2, 0,
                        2,        0, 7, 0, 0,    0,    1,    0,    0, 0};
    CHECK(GpiSetBackMix(ps, 5) && GpiSetAttrs(ps, 4, 0x5f, 0, area));
    POINTL reference;
    CHECK(GpiQueryPatternRefPoint(ps, &reference) && reference.x == 1 && reference.y == 0);
    CHECK(GpiMove(ps, &from) && GpiBox(ps, 1, &to, 0, 0));
    CHECK(lx_pm_raster_read(ps, 1, 1, 1, 1, &pixel) && pixel == 0xffffff);
    CHECK(lx_pm_raster_read(ps, 2, 1, 1, 1, &pixel) && pixel == 0x0000ff);
    LONG saved = GpiSavePS(ps);
    reference = (POINTL){INT32_MIN, INT32_MAX};
    CHECK(saved > 0 && GpiSetPatternRefPoint(ps, &reference));
    CHECK(GpiRestorePS(ps, saved) && GpiQueryPatternRefPoint(ps, &reference));
    CHECK(reference.x == 1 && reference.y == 0);
    CHECK(GpiSetPatternSet(ps, 0) && GpiDeleteSetId(ps, 7) && GpiDeleteBitmap(tile));
    CHECK(WinFillRect(ps, &all, CLR_GREEN));
    uint32_t blue = 0x0000ff;
    CHECK(lx_pm_raster_write(ps, 1, 1, 1, 1, &blue));
    HPS target = WinGetPS(window);
    POINTL points[3] = {{0, 0}, {8, 8}, {0, 0}};
    CHECK(WinFillRect(target, &all, CLR_RED) && GpiSetBackColor(target, CLR_GREEN));
    CHECK(GpiSetBackMix(target, 18) && GpiBitBlt(target, ps, 3, points, 0xcc, 2));
    CHECK(lx_pm_raster_read(target, 0, 0, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(lx_pm_raster_read(target, 1, 1, 1, 1, &pixel) && pixel == blue);
    CHECK(GpiSetBackColor(target, CLR_RED) && GpiSetBackMix(target, 19));
    CHECK(GpiBitBlt(target, ps, 3, points, 0xcc, 2));
    CHECK(lx_pm_raster_read(target, 0, 0, 1, 1, &pixel) && pixel == 0x00ff00);
    CHECK(lx_pm_raster_read(target, 1, 1, 1, 1, &pixel) && pixel == blue);
    CHECK(WinReleasePS(target));
    CHECK(GpiSetBitmap(ps, 0) == bitmap && GpiDeleteBitmap(bitmap));
    CHECK(GpiDestroyPS(ps) && DevCloseDC(dc) == 0);
}
int main(void)
{
    CHECK(sizeof(SWP) == 36 && sizeof(POINTL) == 8 && sizeof(RECTL) == 16 && sizeof(SIZEL) == 8);
    HAB hab = WinInitialize(0);
    CHECK(hab);
    CHECK(WinTerminate(hab) && WinInitialize(0) == hab);
    HMQ hmq = WinCreateMsgQueue(hab, 0);
    CHECK(WinInitialize(0) == hab);
    CHECK(WinTerminate(0) && WinQueryAnchorBlock(HWND_DESKTOP) == hab);
    CHECK(hmq);
    CHECK(WinRegisterClass(0, "test", procedure, CS_SIZEREDRAW, 8));
    ULONG flags = 0;
    HWND client;
    HWND frame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "test", "PM contracts", 0, 0,
                                    0, &client);
    CHECK(frame && client && creates == 1);
    HWND object =
        WinCreateWindow(HWND_OBJECT, "test", NULL, 0, 0, 0, 0, 0, 0, HWND_TOP, 77, NULL, NULL);
    CHECK(object && WinQueryWindow(object, 5) == HWND_OBJECT);
    CHECK(!WinGetPS(object) && !WinDestroyWindow(HWND_OBJECT));
    CHECK(WinSetParent(object, client, TRUE) && WinQueryWindow(object, 5) == client);
    CHECK(!WinSetParent(client, object, FALSE));
    CHECK(WinSetParent(object, HWND_DESKTOP, FALSE));
    CHECK(WinQueryWindow(object, 5) == HWND_DESKTOP);
    CHECK(WinSetParent(object, HWND_OBJECT, TRUE));
    CHECK(WinPostMsg(object, WM_USER + 7, (MPARAM)(uintptr_t)77, 0));
    QMSG object_message;
    CHECK(WinPeekMsg(hab, &object_message, object, WM_USER + 7, WM_USER + 7, 1) &&
          (uintptr_t)object_message.mp1 == 77);
    CHECK(WinDestroyWindow(object));
    CHECK(WinWindowFromID(frame, FID_CLIENT) == client);
    CHECK(WinSetWindowPtr(frame, 0, (void *)(uintptr_t)0x12345678));
    CHECK((uintptr_t)WinQueryWindowPtr(frame, 0) == 0x12345678);
    CHECK(!WinWindowFromID(frame, FID_MENU));
    CHECK(WinSetWindowULong(client, 0, 0x12345678) && WinQueryWindowULong(client, 0) == 0x12345678);
    CHECK(WinSetWindowPtr(client, 4, (void *)(uintptr_t)0xfedcba98) &&
          (uintptr_t)WinQueryWindowPtr(client, 4) == 0xfedcba98);
    CHECK(!WinSetWindowULong(client, 5, 42) && !WinSetWindowULong(client, -1, 42));
    CHECK(WinQueryWindowULong(client, -4) == hmq);
    CHECK(WinSubclassWindow(client, WinDefWindowProc) == procedure);
    CHECK(WinSubclassWindow(client, procedure) == WinDefWindowProc);
    CHECK(WinQueryWindowULong(client, -3) == (ULONG)(uintptr_t)procedure);
    CHECK(WinSetWindowPtr(client, -3, (PVOID)(uintptr_t)0x12345678));
    CHECK((uintptr_t)WinQueryWindowPtr(client, -3) == 0x12345678);
    CHECK(WinSubclassWindow(client, procedure) == (PFNWP)(uintptr_t)0x12345678);
    CHECK(WinQuerySysColor(HWND_DESKTOP, -20, 0) == 0xffffff);
    char mixed_case[] = "Suite i-123";
    CHECK(WinUpper(hab, 0, 0, mixed_case) == 11 && !strcmp(mixed_case, "SUITE I-123"));
    CHECK(WinUpperChar(hab, 437, 1, 'z') == 'Z');
    CHECK(!WinUpper(hab, 932, 0, mixed_case));
    CHECK(!WinQuerySysColor(HWND_DESKTOP, -51, 0));
    CHECK((WinGetLastError(hab) & 0xffff) == PMERR_PARAMETER_OUT_OF_RANGE);
    RECTL desktop;
    CHECK(WinQueryWindowRect(HWND_DESKTOP, &desktop));
    CHECK(WinQuerySysValue(HWND_DESKTOP, 20) == desktop.xRight);
    CHECK(WinQuerySysValue(HWND_DESKTOP, 21) == desktop.yTop);
    HPS screen = WinGetScreenPS(HWND_DESKTOP);
    CHECK(screen && sizeof(FONTMETRICS) == 228);
    LONG requested = 0;
    CHECK(GpiQueryFonts(screen, 1, "Helv", &requested, 228, NULL) == 4 && requested == 0);
    FONTMETRICS metrics[2];
    requested = 2;
    CHECK(GpiQueryFonts(screen, 1, "Helv", &requested, 228, metrics) == 2 && requested == 2);
    CHECK(!strcmp(metrics[0].szFamilyname, "Helv") && metrics[0].lMatch == 1);
    CHECK(metrics[0].lMaxAscender > 0 && metrics[0].lAveCharWidth > 0);
    CHECK(!(metrics[0].fsSelection & 0x20) && (metrics[1].fsSelection & 0x20));
    requested = 0;
    CHECK(!GpiQueryFonts(screen, 1, "Missing face", &requested, 228, NULL));
    CHECK(GpiQueryFontMetrics(screen, 228, metrics) && metrics[0].lMatch == 1);
    LONG capabilities[2];
    CHECK(DevQueryCaps(HWND_DESKTOP, 8, 2, capabilities) && capabilities[0] == 3780 &&
          capabilities[1] == 3780);
    CHECK(!DevQueryCaps(HWND_DESKTOP, 41, 2, capabilities));
    CHECK(GpiQueryColorIndex(screen, 0, 0xff0000) == CLR_RED);
    CHECK(GpiQueryColorIndex(screen, 0, 0x0100fe) == CLR_BLUE);
    CHECK(GpiQueryRGBColor(screen, 0, CLR_RED) == 0xff0000);
    CHECK(GpiQueryRGBColor(screen, 1, CLR_RED) == -255);
    CHECK(WinReleasePS(screen) && !WinReleasePS(screen));
    CHECK(WinSetWindowPos(frame, HWND_TOP, 10, 10, 64, 48, SWP_MOVE | SWP_SIZE | SWP_SHOW));
    graphics_contracts(hab, client);
    accelerator_contracts(hab, frame, client);
    menu_contracts(frame);
    selection_contracts(client);
    scrollbar_contracts(hab, client);
    clipboard_contracts(hab, frame);
    menu_tracking_contracts(frame);
    list_contracts(frame);
    hook_contracts(hab, client);
    busy_queue_host_events(hab, client);
    paint_scheduling_contracts(hab);
    keyboard_contracts(hab, client);
    host_focus_contracts(hab, frame, client);
    capture_release_contracts(hab, client);
    raster_contracts(hab, client);
    dialog_frame_contracts(client);
    class_clip_contracts(hab, client);
    frame_state_contracts();
    SWP position;
    RECTL rectangle;
    CHECK(WinQueryWindowPos(client, &position) && position.cx == 64 && position.cy == 48);
    CHECK(WinQueryWindowRect(client, &rectangle) && rectangle.xRight == 64 && sizes == 1);
    char classname[16];
    CHECK(WinQueryClassName(client, sizeof classname, classname) == 4 &&
          !strcmp(classname, "test"));
    CHECK(WinQueryAnchorBlock(client) == hab && WinQueryWindow(client, 5) == frame);
    CHECK(WinQueryWindow(client, 4) == frame);
    HENUM enumeration = WinBeginEnumWindows(frame);
    CHECK(enumeration);
    HWND transient =
        WinCreateWindow(frame, "test", NULL, 0, 0, 0, 1, 1, frame, HWND_TOP, 900, NULL, NULL);
    CHECK(transient && WinGetNextWindow(enumeration) == client);
    CHECK(!WinGetNextWindow(enumeration) && WinGetNextWindow(enumeration) == client);
    CHECK(WinEndEnumWindows(enumeration) && !WinEndEnumWindows(enumeration));
    CHECK(WinQueryWindow(transient, 0) == client && WinQueryWindow(client, 1) == transient);
    CHECK(WinEnableWindow(transient, FALSE) && !WinIsWindowEnabled(transient));
    CHECK(WinEnableWindow(transient, TRUE) && WinIsWindowEnabled(transient));
    CHECK(WinPostMsg(transient, WM_USER + 90, 0, 0) && WinDestroyWindow(transient));
    CHECK(WinPostMsg(client, WM_USER + 90, (MPARAM)(uintptr_t)3, 0));
    QMSG retired;
    CHECK(WinGetMsg(hab, &retired, 0, WM_USER + 90, WM_USER + 90) && retired.hwnd == client);
    ULONG color = CLR_RED, found = 0, result = 0;
    unsigned before = changes;
    CHECK(WinSetPresParam(frame, 2, 4, &color) && changes == before + 1);
    CHECK(WinQueryPresParam(client, 2, 0, &found, 4, &result, 2) == 4 && found == 2 &&
          result == 0xff0000);
    CHECK(!WinQueryPresParam(client, 2, 0, &found, 4, &result, 1));
    CHECK(WinRemovePresParam(frame, 2) && !WinRemovePresParam(frame, 2));
    CHECK(WinShowWindow(frame, FALSE) && !WinIsWindowVisible(frame));
    CHECK(WinShowWindow(frame, TRUE) && WinIsWindowVisible(frame));
    HDC dc = WinOpenWindowDC(client);
    SIZEL size = {64, 48};
    HPS ps = GpiCreatePS(hab, dc, &size, PU_PELS | GPIA_ASSOC);
    CHECK(ps && GpiSetColor(ps, CLR_RED) && GpiSetDrawingMode(ps, DM_DRAW));
    char font_name[] = "18.Courier Bold";
    CHECK(WinSetPresParam(frame, 15, sizeof font_name, font_name));
    CHECK(GpiQueryFontMetrics(ps, 228, metrics));
    CHECK(metrics[0].lEmHeight == 24 && (metrics[0].fsType & 1) && (metrics[0].fsSelection & 0x20));
    POINTL text_box[5], narrow_box[5];
    CHECK(GpiQueryTextBox(ps, 2, "WW", 5, text_box) && GpiQueryTextBox(ps, 2, "ii", 5, narrow_box));
    CHECK(text_box[4].x == narrow_box[4].x && text_box[0].y > 0 && text_box[1].y <= 0);
    CHECK(WinRemovePresParam(frame, 15));
    CHECK(GpiQueryTextBox(ps, 2, "WW", 5, text_box) && GpiQueryTextBox(ps, 2, "ii", 5, narrow_box));
    CHECK(text_box[4].x > narrow_box[4].x);

    POINTL point = {0, 0};
    CHECK(GpiMove(ps, &point));
    point = (POINTL){63, 47};
    CHECK(GpiLine(ps, &point));
    CHECK(GpiSetPel(ps, &point));
    CHECK(!GpiSetDrawingMode(ps, 99));
    HPS paint = WinBeginPaint(client, 0, &rectangle);
    CHECK(paint && paint != ps);
    CHECK(WinFillRect(paint, &rectangle, CLR_BLACK) && WinEndPaint(paint));
    CHECK(GpiMove(ps, &point));
    /* Growing the registry must retain window and presentation-space identities. */
    for (unsigned i = 0; i < 40; i++)
        CHECK(
            WinCreateWindow(frame, "test", NULL, 0, 0, 0, 1, 1, 0, HWND_TOP, 100 + i, NULL, NULL));
    CHECK(WinQueryWindowRect(client, &rectangle) && GpiMove(ps, &point));
    CHECK(WinPostMsg(client, WM_USER, (MPARAM)(uintptr_t)1, 0));
    CHECK(WinPostMsg(client, WM_USER + 1, (MPARAM)(uintptr_t)2, 0));
    QMSG q;
    CHECK(WinGetMsg(hab, &q, client, WM_USER + 1, WM_USER + 1) && (uintptr_t)q.mp1 == 2);
    CHECK(WinPostMsg(client, WM_USER + 2, (MPARAM)(uintptr_t)3, 0));
    CHECK(WinGetMsg(hab, &q, client, WM_USER, WM_USER) && (uintptr_t)q.mp1 == 1);
    CHECK(WinGetMsg(hab, &q, client, WM_USER + 2, WM_USER + 2) && (uintptr_t)q.mp1 == 3);
    ULONG timer = WinStartTimer(hab, client, 0, 5);
    CHECK(timer);
    CHECK(WinGetMsg(hab, &q, client, WM_TIMER, WM_TIMER) && (uintptr_t)q.mp1 == timer);
    CHECK(WinStopTimer(hab, client, timer) && !WinStopTimer(hab, client, timer));
    CHECK(WinDestroyWindow(frame));
    CHECK(GpiQueryDevice(ps) == 0 && GpiMove(ps, &point) && GpiDestroyPS(ps));
    HENUM abandoned = WinBeginEnumWindows(HWND_DESKTOP);
    CHECK(abandoned);
    LxHandleBorrow borrow = {0};
    CHECK(WinDestroyMsgQueue(hmq) && WinTerminate(hab));
    CHECK(lx_handle_borrow(abandoned, LX_HANDLE_PM_ENUM, &borrow));
    lx_handle_release(&borrow);
    lxHostThreadCleanup();
    CHECK(!lx_handle_borrow(abandoned, LX_HANDLE_PM_ENUM, &borrow));
    puts("PM frame, stable handles, retained drawing, queue and timer contracts passed");
    return 0;
}
