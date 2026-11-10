/* Public domain. No rights reserved. Wholly AI-authored. */
/* Packed resources, callback dismissal, nested ownership and modal queue unwinding. */
#include "os2/include/pmwin.h"
#include "os2/include/pmerr.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmwctlp.h"
#include "emulator/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum
{
    TEMPLATE_ITEMS_OFFSET = offsetof(DLGTEMPLATE, adlgti),
    TEMPLATE_CODEPAGE = 437,
    TEMPLATE_NO_DATA = 0xffff,
    TEMPLATE_DIALOG_ID = 777,
    LIST_ID = 20,
    TEXT_ID = 30,
    /* The resource type the test resource loader serves. */
    RESOURCE_TYPE_DIALOG = 4,
    /* PC scan code of the Enter key. */
    ENTER_SCANCODE = 0x1c
};

static uint8_t bytes[256];
static HWND owner;
static unsigned mode, initialized, destroyed, released;
static unsigned pointer_commands;

static MRESULT OS2API pointer_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_COMMAND)
    {
        CHECK((USHORT)(uintptr_t)first == DID_OK);
        pointer_commands++;
        return 0;
    }
    return WinDefDlgProc(window, message, first, second);
}

static Window *native_window(HWND handle)
{
    AnchorBlock *anchor = lxHostThreadState()->anchor_block;
    for (size_t i = 0; i < anchor->windows_array_len; i++)
        if (anchor->windows[i] && anchor->windows[i]->hwnd == handle)
            return anchor->windows[i];
    return NULL;
}

static void check_cycle(HWND window, BOOL wanted_eligible, BOOL wanted_modal)
{
    int eligible = !wanted_eligible, modal = !wanted_modal;
    Window *native = native_window(window);
    CHECK(native && native->heavy.window);
    lx_host_window_get_cycle_policy(native->heavy.window, &eligible, &modal);
    CHECK(eligible == wanted_eligible && modal == wanted_modal);
}

static HWND cycle_previous, cycle_other;

static MRESULT OS2API cycle_reparent_procedure(HWND window, ULONG message, MPARAM first,
                                               MPARAM second)
{
    if (message == WM_INITDLG)
    {
        CHECK(WinPostMsg(window, WM_USER, 0, 0));
        return 0;
    }
    if (message == WM_USER)
    {
        check_cycle(cycle_previous, FALSE, TRUE);
        check_cycle(cycle_other, TRUE, FALSE);
        CHECK(WinSetParent(window, cycle_other, FALSE));
        check_cycle(cycle_previous, FALSE, FALSE);
        check_cycle(cycle_other, TRUE, TRUE);
        CHECK(WinSetParent(window, cycle_previous, FALSE));
        check_cycle(cycle_previous, FALSE, TRUE);
        check_cycle(cycle_other, TRUE, FALSE);
        CHECK(WinDismissDlg(window, 42));
        return 0;
    }
    return WinDefDlgProc(window, message, first, second);
}

static MRESULT OS2API paint_modal_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_INITDLG)
    {
        CHECK(WinPostMsg(window, WM_USER, 0, 0));
        return 0;
    }
    if (message == WM_USER)
    {
        AnchorBlock *anchor = lxHostThreadState()->anchor_block;
        Window *modal = native_window(window), *painting = native_window(owner);
        check_cycle(window, FALSE, TRUE);
        check_cycle(owner, FALSE, FALSE); /* Disabled owner is not a cycle target. */
        CHECK(modal && painting && anchor->paint_depth);
        CHECK(WinUpdateWindow(window));
        for (Window *child = modal->children; child; child = child->sibling)
            CHECK(WinUpdateWindow(child->hwnd));
        HPS display = WinGetPS(window);
        CHECK(display && WinReleasePS(display));
        CHECK(modal->last_present);
        HPS validate = WinBeginPaint(owner, 0, NULL);
        CHECK(validate && WinEndPaint(validate));
        display = WinGetPS(owner);
        CHECK(display && WinReleasePS(display));
        CHECK(!painting->last_present);
        CHECK(WinDismissDlg(window, 123));
        check_cycle(window, FALSE, FALSE);
        check_cycle(owner, TRUE, FALSE);
        return 0;
    }
    return WinDefDlgProc(window, message, first, second);
}

static MRESULT OS2API paint_owner_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_PAINT)
    {
        HPS paint = WinBeginPaint(window, 0, NULL);
        RECTL rect = {0, 0, 100, 100};
        CHECK(paint && WinFillRect(paint, &rect, CLR_BLUE) && WinEndPaint(paint));
        HWND modal = WinCreateDlg(HWND_DESKTOP, window, paint_modal_procedure, (void *)bytes, NULL);
        CHECK(modal && WinShowWindow(modal, TRUE));
        CHECK(WinProcessDlg(modal) == 123 && WinDestroyWindow(modal));
        CHECK(!native_window(window)->last_present);
        return 0;
    }
    return WinDefWindowProc(window, message, first, second);
}

static void make_template(void)
{
    memset(bytes, 0, sizeof bytes);
    DLGTEMPLATE *t = (void *)bytes;
    t->cbTemplate = sizeof bytes;
    t->codepage = TEMPLATE_CODEPAGE;
    t->offadlgti = TEMPLATE_ITEMS_OFFSET;
    t->iItemFocus = 1;
    DLGTITEM *items = (void *)(bytes + TEMPLATE_ITEMS_OFFSET);
    for (unsigned i = 0; i < 4; i++)
    {
        items[i].offPresParams = items[i].offCtlData = TEMPLATE_NO_DATA;
        items[i].cx = 80;
        items[i].cy = 12;
    }
    items[0].cChildren = 3;
    items[0].offClassName = PM_CLASS_FRAME;
    items[0].id = TEMPLATE_DIALOG_ID;
    items[0].cx = 100;
    items[0].cy = 80;
    items[0].cchText = 6;
    items[0].offText = 200;
    memcpy(bytes + 200, "Dialog", 7);
    items[0].offCtlData = 240;
    *(uint32_t *)(void *)(bytes + 240) = FCF_TITLEBAR | FCF_SYSMENU;
    items[1].offClassName = PM_CLASS_BUTTON;
    items[1].id = DID_OK;
    items[1].flStyle = WS_VISIBLE | WS_TABSTOP;
    items[1].cchText = 2;
    items[1].offText = 208;
    memcpy(bytes + 208, "OK", 3);
    items[2].offClassName = PM_CLASS_LISTBOX;
    items[2].id = LIST_ID;
    items[2].y = 15;
    items[2].cy = 30;
    items[3].offClassName = PM_CLASS_STATIC;
    items[3].flStyle = SS_TEXT;
    items[3].id = TEXT_ID;
    items[3].y = 50;
}

static MRESULT OS2API procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_INITDLG)
    {
        initialized++;
        HWND system = WinWindowFromID(window, FID_SYSMENU);
        CHECK(system);
        ULONG id = (ULONG)(uintptr_t)WinSendMsg(system, MM_ITEMIDFROMPOSITION, 0, 0);
        MENUITEM menu;
        CHECK(id == SC_SYSMENU && WinSendMsg(system, MM_QUERYITEM, (MPARAM)(uintptr_t)id, &menu));
        CHECK(menu.hwndSubMenu);
        CHECK((intptr_t)WinSendMsg(menu.hwndSubMenu, MM_ITEMPOSITIONFROMID,
                                   (MPARAM)(uintptr_t)SC_CLOSE, 0) >= 0);
        CHECK((intptr_t)WinSendMsg(menu.hwndSubMenu, MM_ITEMIDFROMPOSITION, (MPARAM)(uintptr_t)99,
                                   0) == MIT_ERROR);
        CHECK(second == (PVOID)(uintptr_t)0x1234);
        CHECK((HWND)(uintptr_t)first == WinWindowFromID(window, DID_OK));
        CHECK(WinQueryWindowUShort(WinWindowFromID(window, LIST_ID), QWS_ID) == LIST_ID);
        CHECK(WinSetDlgItemText(window, TEXT_ID, "Owned text"));
        char text[6];
        CHECK(WinQueryDlgItemText(window, TEXT_ID, sizeof text, text) == 5);
        CHECK(!strcmp(text, "Owned"));
        if (mode == 1)
            WinDismissDlg(window, 99);
        else
            WinPostMsg(window, WM_USER, 0, 0);
    }
    if (message == WM_USER)
    {
        CHECK(!WinIsWindowEnabled(owner));
        if (mode == 2)
            WinPostQueueMsg(WinQueryWindowULong(owner, QWL_HMQ), WM_QUIT, (MPARAM)(uintptr_t)42, 0);
        else if (mode == 3)
            WinDestroyWindow(owner);
        else if (mode == 4)
        {
            HWND previous = owner;
            owner = window;
            mode = 1;
            CHECK(WinDlgBox(HWND_DESKTOP, window, procedure, 0, TEMPLATE_DIALOG_ID,
                            (PVOID)(uintptr_t)0x1234) == 99);
            CHECK(WinIsWindowEnabled(window));
            owner = previous;
            mode = 4;
            WinDismissDlg(window, 77);
        }
        else if (mode == 5)
        {
            AnchorBlock *anchor = lxHostThreadState()->anchor_block;
            HWND button = WinWindowFromID(window, DID_OK);
            CHECK(WinSetFocus(HWND_DESKTOP, button));
            CHECK(!WinSetFocus(HWND_DESKTOP, owner));
            LxHostEvent event = {0};
            event.type = LX_HOST_EVENT_KEY_DOWN;
            event.key = LX_HOST_KEY_RETURN;
            event.scancode = ENTER_SCANCODE;
            CHECK(processKeyboardEvent(anchor, &event));
        }
        else
            WinSendDlgItemMsg(window, DID_OK, BM_CLICK, 0, 0); /* posts WM_COMMAND */
    }
    if (message == WM_DESTROY)
    {
        CHECK(WinWindowFromID(window, DID_OK));
        destroyed++;
    }
    return WinDefDlgProc(window, message, first, second);
}

static uint32_t query(uint32_t module, uint32_t type, uint32_t name, uint32_t *size)
{
    (void)module;
    if (type != RESOURCE_TYPE_DIALOG || name != TEMPLATE_DIALOG_ID)
        return ERROR_INVALID_PARAMETER;
    *size = sizeof bytes;
    return 0;
}

static uint32_t get(uint32_t module, uint32_t type, uint32_t name, void **data)
{
    uint32_t size;
    if (query(module, type, name, &size))
        return ERROR_INVALID_PARAMETER;
    *data = bytes;
    return 0;
}

static uint32_t release_resource(void *data)
{
    CHECK(data == bytes);
    released++;
    return 0;
}

int main(void)
{
    CHECK(sizeof(DLGTITEM) == 30 && sizeof(DLGTEMPLATE) == 44);
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(queue);
    GProcessState.getResource = get;
    GProcessState.queryResourceSize = query;
    GProcessState.freeResource = release_resource;
    make_template();
    for (mode = 0; mode <= 5; mode++)
    {
        ULONG flags = 0;
        owner = WinCreateStdWindow(HWND_DESKTOP, 0, &flags, NULL, "Owner", 0, 0, 0, NULL);
        CHECK(owner);
        CHECK(WinSetWindowPos(owner, HWND_TOP, 0, 0, 400, 400, SWP_SIZE | SWP_SHOW));
        ULONG result = WinDlgBox(mode == 5 ? owner : HWND_DESKTOP, owner, procedure, 0,
                                 TEMPLATE_DIALOG_ID, (PVOID)(uintptr_t)0x1234);
        CHECK(result == (mode == 1   ? 99u
                         : mode == 2 ? 2u
                         : mode == 3 ? 0xffffu
                         : mode == 4 ? 77u
                                     : 1u));
        if (mode == 2)
        {
            QMSG msg;
            CHECK(!WinGetMsg(anchor, &msg, 0, 0, 0) && msg.msg == WM_QUIT &&
                  (uintptr_t)msg.mp1 == 42);
        }
        if (mode != 3)
        {
            CHECK(WinIsWindowEnabled(owner));
            CHECK(WinDestroyWindow(owner));
        }
    }
    CHECK(initialized == 7 && destroyed == 7 && released == 7);
    ULONG cycle_flags = 0;
    cycle_previous =
        WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &cycle_flags, NULL, "First", 0, 0, 0, NULL);
    cycle_other =
        WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &cycle_flags, NULL, "Second", 0, 0, 0, NULL);
    CHECK(cycle_previous && cycle_other);
    HWND cycle_dialog =
        WinCreateDlg(cycle_previous, cycle_previous, cycle_reparent_procedure, (void *)bytes, NULL);
    CHECK(cycle_dialog && WinProcessDlg(cycle_dialog) == 42);
    check_cycle(cycle_previous, TRUE, FALSE);
    check_cycle(cycle_other, TRUE, FALSE);
    CHECK(WinDestroyWindow(cycle_dialog) && WinDestroyWindow(cycle_previous) &&
          WinDestroyWindow(cycle_other));
    CHECK(WinDlgBox(HWND_DESKTOP, 0, procedure, 0, 999, NULL) == 0xffff);
    DLGTITEM *items = (void *)(bytes + TEMPLATE_ITEMS_OFFSET);
    items[1].offText = 255;
    CHECK(!WinCreateDlg(HWND_DESKTOP, 0, procedure, (void *)bytes, NULL));
    CHECK((WinGetLastError(anchor) & 0xffff) == PMERR_INVALID_RESOURCE_FORMAT);
    make_template();
    items[0].cChildren = 0xffff;
    CHECK(!WinCreateDlg(HWND_DESKTOP, 0, procedure, (void *)bytes, NULL));
    make_template();
    mode = 1;
    HWND dialog = WinCreateDlg(HWND_DESKTOP, 0, procedure, (void *)bytes, (PVOID)(uintptr_t)0x1234);
    CHECK(dialog);
    HWND list = WinWindowFromID(dialog, LIST_ID);
    CHECK((WinQueryWindowUShort(dialog, QWS_FLAGS) & FF_DLGDISMISSED) &&
          WinQueryWindowUShort(dialog, QWS_RESULT) == 99);
    CHECK(WinSetWindowUShort(dialog, QWS_FLAGS,
                             WinQueryWindowUShort(dialog, QWS_FLAGS) & ~FF_DLGDISMISSED));
    CHECK(WinPostMsg(dialog, WM_COMMAND, (MPARAM)(uintptr_t)7, 0));
    CHECK(WinProcessDlg(dialog) == 7);
    CHECK((intptr_t)WinSendMsg(list, LM_INSERTITEM, (MPARAM)(intptr_t)LIT_END, "Wide") == 0);
    CHECK((intptr_t)WinSendMsg(list, LM_INSERTITEM, (MPARAM)(intptr_t)LIT_SORTASCENDING, "A") == 0);
    CHECK((intptr_t)WinSendMsg(list, LM_QUERYITEMCOUNT, 0, 0) == 2);
    CHECK(WinSendMsg(list, LM_SELECTITEM, 0, (MPARAM)(uintptr_t)TRUE));
    CHECK((intptr_t)WinSendMsg(list, LM_QUERYSELECTION, (MPARAM)(intptr_t)LIT_FIRST, 0) == 0);
    CHECK(!WinSendMsg(list, LM_SELECTITEM, (MPARAM)(uintptr_t)99, (MPARAM)(uintptr_t)TRUE));
    CHECK(WinSendMsg(list, LM_SELECTITEM, (MPARAM)(intptr_t)LIT_NONE, (MPARAM)(uintptr_t)TRUE));
    CHECK((uint32_t)(uintptr_t)WinSendMsg(list, LM_QUERYSELECTION, (MPARAM)(intptr_t)LIT_FIRST,
                                          0) == UINT32_MAX);
    char text[8];
    CHECK((intptr_t)WinSendMsg(list, LM_QUERYITEMTEXT, (MPARAM)(uintptr_t)(8u << 16), text) == 1 &&
          !strcmp(text, "A"));
    CHECK(WinSetWindowUShort(list, 2, 0xabcd) && WinQueryWindowUShort(list, 2) == 0xabcd);
    CHECK(!WinSetWindowUShort(list, 3, 0));
    CHECK(WinDestroyWindow(dialog));
    make_template();
    items = (void *)(bytes + TEMPLATE_ITEMS_OFFSET);
    items[0].flStyle |= FS_DLGBORDER;
    items[3].y = 72;
    items[3].cy = 8;
    for (unsigned dpi = 96; dpi <= 192; dpi *= 2)
    {
        GProcessState.display_dpi = dpi;
        dialog = WinCreateDlg(HWND_DESKTOP, 0, pointer_procedure, (void *)bytes, NULL);
        CHECK(dialog);
        RECTL content;
        CHECK(WinQueryWindowRect(dialog, &content) && WinCalcFrameRect(dialog, &content, TRUE));
        POINTL expected = {100, 80};
        CHECK(WinMapDlgPoints(dialog, &expected, 1, TRUE));
        CHECK(content.xRight - content.xLeft == expected.x &&
              content.yTop - content.yBottom == expected.y);
        SWP top, bottom, caption;
        CHECK(WinQueryWindowPos(WinWindowFromID(dialog, TEXT_ID), &top));
        CHECK(WinQueryWindowPos(WinWindowFromID(dialog, DID_OK), &bottom));
        CHECK(WinQueryWindowPos(WinWindowFromID(dialog, FID_TITLEBAR), &caption));
        CHECK(top.y + top.cy <= caption.y && bottom.y >= content.yBottom &&
              bottom.x >= content.xLeft);
        CHECK(WinShowWindow(dialog, TRUE));
        AnchorBlock *state = lxHostThreadState()->anchor_block;
        Window *root = NULL;
        for (size_t i = 0; i < state->windows_array_len; i++)
            if (state->windows[i] && state->windows[i]->hwnd == dialog)
                root = state->windows[i];
        CHECK(root);
        LxHostEvent click = {.type = LX_HOST_EVENT_BUTTON_DOWN,
                             .window_data = root,
                             .x = bottom.x + bottom.cx / 2,
                             .y = root->h - 1 - (bottom.y + bottom.cy / 2),
                             .button = 1};
        unsigned previous = pointer_commands;
        CHECK(processHostEvent(state, &click));
        click.type = LX_HOST_EVENT_BUTTON_UP;
        CHECK(processHostEvent(state, &click));
        QMSG event;
        unsigned dispatched = 0;
        while (WinPeekMsg(anchor, &event, 0, 0, 0, PM_REMOVE))
        {
            CHECK(dispatched++ < 100);
            WinDispatchMsg(anchor, &event);
        }
        CHECK(pointer_commands == previous + 1);
        CHECK(WinDestroyWindow(dialog));
    }
    GProcessState.display_dpi = 96;
    make_template();
    CHECK(WinRegisterClass(anchor, "PaintModalOwner", paint_owner_procedure, 0, 0));
    owner = WinCreateWindow(HWND_DESKTOP, "PaintModalOwner", "Paint owner", WS_VISIBLE, 0, 0, 100,
                            100, 0, HWND_TOP, 800, NULL, NULL);
    CHECK(owner && native_window(owner) && !native_window(owner)->last_present);
    WinSendMsg(owner, WM_PAINT, 0, 0);
    HPS display = WinGetPS(owner);
    CHECK(display && WinReleasePS(display));
    CHECK(native_window(owner)->last_present && WinDestroyWindow(owner));
    CHECK(WinDestroyMsgQueue(queue) && WinTerminate(anchor));
    puts(
        "modal resources, callback dismissal, owner restoration, nested dialogs and quit repost passed");
    return 0;
}
