/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM ownership, codepages, delayed rendering and isolated host image fixtures. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/include/private/pmp.h"
#include "host/clipboard.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
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
static HAB anchor;
static unsigned rendered, destroyed;
static const char rich[] = "{\\rtf1\\bin3 a\0b}";
static ULONG rich_format;
static unsigned released;

static uint32 release_memory(void *host)
{
    ULONG flags = 123;
    CHECK(!WinQueryClipbrdFmtInfo(anchor, 0xbfff, &flags));
    uint32 address;
    CHECK(lx_execution_address(GProcessState.execution, host, 1, 0, &address));
    LxGuestMapping *mapping = lx_execution_mapping_at(GProcessState.execution, address);
    CHECK(mapping && lx_execution_unmap(GProcessState.execution, mapping));
    released++;
    return 0;
}

static MRESULT procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_RENDERFMT)
    {
        rendered++;
        ULONG format = (ULONG)(uintptr_t)first;
        CHECK(lx_pm_clipboard_set_bytes(anchor, format, format == 1 ? "delayed\0" : rich,
                                        format == 1 ? 7 : sizeof rich - 1));
        return 0;
    }
    if (message == WM_DESTROYCLIPBOARD)
        destroyed++;
    return WinDefWindowProc(window, message, first, second);
}

static const void *external_image(void *context, const char *mime, size_t *size)
{
    if (mime && !strcmp(mime, "image/png"))
    {
        LxHostClipboardItem *item = context;
        *size = item->size;
        return item->data;
    }
    *size = 0;
    return NULL;
}

int main(void)
{
    CHECK(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    char error[256];
    GProcessState.execution = lx_execution_create(TWOINE_PORTABLE_LOADER ? "unicorn" : "native",
                                                  NULL, error, sizeof error);
    CHECK(GProcessState.execution);
    anchor = WinInitialize(0);
    CHECK(anchor && WinCreateMsgQueue(anchor, 0));
    CHECK(!strcmp(SDL_GetCurrentVideoDriver(), "dummy"));
    CHECK(WinRegisterClass(anchor, "ClipboardFixture", procedure, 0, 0));
    HWND window = WinCreateWindow(HWND_DESKTOP, "ClipboardFixture", "fixture", 0, 0, 0, 32, 32,
                                  HWND_DESKTOP, HWND_TOP, 1, NULL, NULL);
    CHECK(window);
    GProcessState.process_codepage = 850;
    GProcessState.freeSharedMemory = release_memory;
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, 4096, 3, 0);
    CHECK(mapping);
    void *allocation = lx_execution_mapping_host(mapping);
    const unsigned char text[] = {'c', 'a', 'f', 0x82, '\r', '\n', 0};
    memcpy(allocation, text, sizeof text);
    uint32_t address;
    CHECK(lx_execution_address(GProcessState.execution, allocation, sizeof text, LX_GUEST_READ,
                               &address));
    CHECK(WinOpenClipbrd(anchor) && WinEmptyClipbrd(anchor));
    ULONG emptied_flags = 123;
    CHECK(!WinQueryClipbrdFmtInfo(anchor, 2, &emptied_flags) && emptied_flags == 123);
    CHECK(WinSetClipbrdOwner(anchor, window));
    CHECK(WinSetClipbrdData(anchor, address, 1, 0x400));
    CHECK(WinCloseClipbrd(anchor));
    size_t size;
    void *copy = lx_host_clipboard_read("text/plain;charset=utf-8", &size);
    CHECK(copy && size == 6 && !memcmp(copy, "caf\xc3\xa9\n", size));
    free(copy);
    CHECK(WinOpenClipbrd(anchor) && WinEmptyClipbrd(anchor));
    CHECK(released == 1 &&
          !lx_execution_memory(GProcessState.execution, address, 1, LX_GUEST_READ));
    rich_format = WinAddAtom(1, "Rich Text Format");
    CHECK(rich_format && lx_pm_clipboard_set_bytes(anchor, rich_format, rich, sizeof rich - 1));
    CHECK(WinCloseClipbrd(anchor));
    copy = lx_host_clipboard_read("text/rtf", &size);
    CHECK(copy && size == sizeof rich - 1 && !memcmp(copy, rich, size));
    free(copy);
    CHECK(WinOpenClipbrd(anchor) && WinEmptyClipbrd(anchor));
    CHECK(lx_pm_clipboard_set_bytes(anchor, 1, "packet", 6));
    CHECK(lx_pm_clipboard_set_bytes(anchor, rich_format, rich, sizeof rich - 1));
    ULONG html_format = WinAddAtom(1, "HTML Format");
    size_t html_size;
    void *html = lx_host_clipboard_html_wrap("<b>packet</b>", 13, &html_size);
    CHECK(html && lx_pm_clipboard_set_bytes(anchor, html_format, html, html_size));
    free(html);
    ULONG packet_bitmap = lx_pm_bitmap_create(2, 2, 1, 24);
    LxHandleBorrow packet_borrow = {0};
    PmBitmap *packet_image = lx_pm_bitmap(packet_bitmap, &packet_borrow);
    CHECK(packet_image);
    const uint32_t corners[] = {0x0000ff, 0x102030, 0xff0000, 0x00ff00};
    memcpy(packet_image->pixels, corners, sizeof corners);
    lx_handle_release(&packet_borrow);
    CHECK(WinSetClipbrdData(anchor, packet_bitmap, 2, 0));
    CHECK(WinCloseClipbrd(anchor));
    CHECK(lx_pm_clipboard_count() == 4);
    copy = lx_host_clipboard_read("text/html", &size);
    CHECK(copy && strstr(copy, "<b>packet</b>"));
    free(copy);
    copy = lx_host_clipboard_read("text/plain;charset=utf-8", &size);
    CHECK(copy && size == 6 && !memcmp(copy, "packet", 6));
    free(copy);
    copy = lx_host_clipboard_read("application/rtf", &size);
    CHECK(copy && size == sizeof rich - 1 && !memcmp(copy, rich, size));
    free(copy);
    copy = lx_host_clipboard_read("image/png", &size);
    unsigned packet_width, packet_height;
    uint8_t *packet_rgba =
        lx_host_clipboard_decode_image("image/png", copy, size, &packet_width, &packet_height);
    CHECK(packet_rgba && packet_width == 2 && packet_height == 2);
    CHECK(packet_rgba[0] == 255 && packet_rgba[1] == 0 && packet_rgba[2] == 0);
    CHECK(packet_rgba[8] == 0 && packet_rgba[9] == 0 && packet_rgba[10] == 255);
    free(copy);
    free(packet_rgba);
    CHECK(WinOpenClipbrd(anchor) && WinEmptyClipbrd(anchor));
    LxGuestMapping *owner_memory = lx_execution_map(GProcessState.execution, 0, 4096, 3, 0);
    CHECK(owner_memory);
    memcpy(lx_execution_mapping_host(owner_memory), "owner", 6);
    ULONG owner_address = lx_execution_mapping_address(owner_memory);
    CHECK(WinSetClipbrdData(anchor, owner_address, 1, 0x401));
    CHECK(WinEmptyClipbrd(anchor));
    CHECK(released == 1 &&
          lx_execution_memory(GProcessState.execution, owner_address, 1, LX_GUEST_READ));
    CHECK(lx_execution_unmap(GProcessState.execution, owner_memory));
    CHECK(WinCloseClipbrd(anchor));
    const unsigned char rgba[] = {255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0, 16, 32, 48, 255};
    void *png = lx_host_clipboard_encode_image("image/png", rgba, 2, 2, &size);
    CHECK(png);
    LxHostClipboardItem item = {"image/png", png, size};
    const char *mimes[] = {"image/png"};
    CHECK(SDL_SetClipboardData(external_image, NULL, &item, mimes, 1));
    SDL_Event external_change = {0};
    external_change.type = SDL_EVENT_CLIPBOARD_UPDATE;
    external_change.clipboard.owner = false;
    CHECK(SDL_PushEvent(&external_change));
    CHECK(WinOpenClipbrd(anchor));
    ULONG flags = 123;
    CHECK(!WinQueryClipbrdFmtInfo(anchor, rich_format, &flags) && flags == 123);
    CHECK(WinQueryClipbrdFmtInfo(anchor, 2, &flags));
    ULONG bitmap = WinQueryClipbrdData(anchor, 2);
    LxHandleBorrow borrow = {0};
    PmBitmap *image = lx_pm_bitmap(bitmap, &borrow);
    CHECK(image && image->width == 2 && image->height == 2);
    CHECK(image->pixels[0] == 0x0000ff && image->pixels[2] == 0xff0000);
    lx_handle_release(&borrow);
    ULONG png_format = WinFindAtom(1, "PNG");
    CHECK(png_format && WinQueryClipbrdData(anchor, png_format));
    size_t raw_size;
    copy = lx_pm_clipboard_copy_bytes(anchor, png_format, &raw_size);
    CHECK(copy && raw_size == size && !memcmp(copy, png, size));
    free(copy);
    CHECK(WinCloseClipbrd(anchor));
    /* Replacing the external fixture transfers no reference to its stack data. */
    CHECK(WinOpenClipbrd(anchor) && WinEmptyClipbrd(anchor));
    CHECK(WinSetClipbrdOwner(anchor, window));
    CHECK(WinSetClipbrdData(anchor, 0, 1, 0x400));
    CHECK(WinSetClipbrdData(anchor, 0, rich_format, 0x400));
    ULONG private_format = WinAddAtom(1, "Private Fixture");
    CHECK(WinSetClipbrdData(anchor, 123, private_format, 0x201));
    CHECK(WinCloseClipbrd(anchor) && rendered == 0);
    copy = lx_host_clipboard_read("text/plain;charset=utf-8", &size);
    CHECK(copy && size == 7 && !memcmp(copy, "delayed", size) && rendered == 1);
    free(copy);
    copy = lx_host_clipboard_read("text/rtf", &size);
    CHECK(copy && size == sizeof rich - 1 && !memcmp(copy, rich, size) && rendered == 2);
    free(copy);
    CHECK(WinOpenClipbrd(anchor));
    CHECK(WinQueryClipbrdData(anchor, private_format) == 123);
    CHECK(WinCloseClipbrd(anchor));
    CHECK(WinDestroyWindow(window));
    copy = lx_host_clipboard_read("text/rtf", &size);
    CHECK(copy && rendered == 2 && destroyed >= 1);
    free(copy);
    free(png);
    CHECK(WinOpenClipbrd(anchor) && WinEmptyClipbrd(anchor) && WinCloseClipbrd(anchor));
    CHECK(WinTerminate(anchor));
    return 0;
}
