/* Public domain. No rights reserved. Wholly AI-authored. */
/* Packed resources, callback dismissal, nested ownership and modal queue unwinding. */
#include "os2libs/pmwin.h"
#include "runtime/runtime.h"
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
static uint8_t bytes[256];
static HWND owner;
static unsigned mode, initialized, destroyed, released;
static void make_template(void)
{
    memset(bytes, 0, sizeof bytes);
    DLGTEMPLATE *t = (void *)bytes;
    t->cbTemplate = sizeof bytes;
    t->codepage = 437;
    t->offadlgti = 14;
    t->iItemFocus = 1;
    DLGTITEM *items = (void *)(bytes + 14);
    for (unsigned i = 0; i < 4; i++)
    {
        items[i].offPresParams = items[i].offCtlData = 0xffff;
        items[i].cx = 80;
        items[i].cy = 12;
    }
    items[0].cChildren = 3;
    items[0].offClassName = 1;
    items[0].id = 777;
    items[0].cx = 100;
    items[0].cy = 80;
    items[0].cchText = 6;
    items[0].offText = 200;
    memcpy(bytes + 200, "Dialog", 7);
    items[1].offClassName = 3;
    items[1].id = 1;
    items[1].flStyle = WS_VISIBLE | 0x10000;
    items[1].cchText = 2;
    items[1].offText = 208;
    memcpy(bytes + 208, "OK", 3);
    items[2].offClassName = 7;
    items[2].id = 20;
    items[2].y = 15;
    items[2].cy = 30;
    items[3].offClassName = 5;
    items[3].flStyle = 1;
    items[3].id = 30;
    items[3].y = 50;
}
static MRESULT OS2API procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_INITDLG)
    {
        initialized++;
        CHECK(second == (PVOID)(uintptr_t)0x1234);
        CHECK((HWND)(uintptr_t)first == WinWindowFromID(window, 1));
        CHECK(WinQueryWindowUShort(WinWindowFromID(window, 20), -1) == 20);
        CHECK(WinSetDlgItemText(window, 30, "Owned text"));
        char text[6];
        CHECK(WinQueryDlgItemText(window, 30, sizeof text, text) == 5);
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
            WinPostQueueMsg(WinQueryWindowULong(owner, -4), WM_QUIT, (MPARAM)(uintptr_t)42, 0);
        else if (mode == 3)
            WinDestroyWindow(owner);
        else if (mode == 4)
        {
            HWND previous = owner;
            owner = window;
            mode = 1;
            CHECK(WinDlgBox(1, window, procedure, 0, 777, (PVOID)(uintptr_t)0x1234) == 99);
            CHECK(WinIsWindowEnabled(window));
            owner = previous;
            mode = 4;
            WinDismissDlg(window, 77);
        }
        else
            WinSendDlgItemMsg(window, 1, 0x120, 0, 0); /* BM_CLICK posts WM_COMMAND. */
    }
    if (message == WM_DESTROY)
    {
        CHECK(!WinWindowFromID(window, 1));
        destroyed++;
    }
    return WinDefDlgProc(window, message, first, second);
}
static uint32_t query(uint32_t module, uint32_t type, uint32_t name, uint32_t *size)
{
    (void)module;
    if (type != 4 || name != 777)
        return 87;
    *size = sizeof bytes;
    return 0;
}
static uint32_t get(uint32_t module, uint32_t type, uint32_t name, void **data)
{
    uint32_t size;
    if (query(module, type, name, &size))
        return 87;
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
    GLoaderState.getResource = get;
    GLoaderState.queryResourceSize = query;
    GLoaderState.freeResource = release_resource;
    make_template();
    for (mode = 0; mode <= 4; mode++)
    {
        ULONG flags = 0;
        owner = WinCreateStdWindow(1, 0, &flags, NULL, "Owner", 0, 0, 0, NULL);
        CHECK(owner);
        ULONG result = WinDlgBox(1, owner, procedure, 0, 777, (PVOID)(uintptr_t)0x1234);
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
    CHECK(initialized == 6 && destroyed == 6 && released == 6);
    CHECK(WinDlgBox(1, 0, procedure, 0, 999, NULL) == 0xffff);
    DLGTITEM *items = (void *)(bytes + 14);
    items[1].offText = 255;
    CHECK(!WinCreateDlg(1, 0, procedure, (void *)bytes, NULL));
    CHECK((WinGetLastError(anchor) & 0xffff) == PMERR_INVALID_RESOURCE_FORMAT);
    make_template();
    items[0].cChildren = 0xffff;
    CHECK(!WinCreateDlg(1, 0, procedure, (void *)bytes, NULL));
    make_template();
    mode = 1;
    HWND dialog = WinCreateDlg(1, 0, procedure, (void *)bytes, (PVOID)(uintptr_t)0x1234);
    CHECK(dialog);
    HWND list = WinWindowFromID(dialog, 20);
    CHECK((WinQueryWindowUShort(dialog, 8) & 0x10) && WinQueryWindowUShort(dialog, 10) == 99);
    CHECK(WinSetWindowUShort(dialog, 8, WinQueryWindowUShort(dialog, 8) & ~0x10));
    CHECK(WinPostMsg(dialog, WM_COMMAND, (MPARAM)(uintptr_t)7, 0));
    CHECK(WinProcessDlg(dialog) == 7);
    CHECK((intptr_t)WinSendMsg(list, 0x161, (MPARAM)(intptr_t)-1, "Wide") == 0);
    CHECK((intptr_t)WinSendMsg(list, 0x161, (MPARAM)(intptr_t)-2, "A") == 0);
    CHECK((intptr_t)WinSendMsg(list, 0x160, 0, 0) == 2);
    CHECK(WinSendMsg(list, 0x164, 0, (MPARAM)(uintptr_t)1));
    CHECK((intptr_t)WinSendMsg(list, 0x165, (MPARAM)(intptr_t)-1, 0) == 0);
    CHECK(!WinSendMsg(list, 0x164, (MPARAM)(uintptr_t)99, (MPARAM)(uintptr_t)1));
    CHECK(WinSendMsg(list, 0x164, (MPARAM)(intptr_t)-1, (MPARAM)(uintptr_t)1));
    CHECK((uint32_t)(uintptr_t)WinSendMsg(list, 0x165, (MPARAM)(intptr_t)-1, 0) == UINT32_MAX);
    char text[8];
    CHECK((intptr_t)WinSendMsg(list, 0x168, (MPARAM)(uintptr_t)(8u << 16), text) == 1 &&
          !strcmp(text, "A"));
    CHECK(WinSetWindowUShort(list, 2, 0xabcd) && WinQueryWindowUShort(list, 2) == 0xabcd);
    CHECK(!WinSetWindowUShort(list, 3, 0));
    CHECK(WinDestroyWindow(dialog));
    CHECK(WinDestroyMsgQueue(queue) && WinTerminate(anchor));
    puts(
        "modal resources, callback dismissal, owner restoration, nested dialogs and quit repost passed");
    return 0;
}
