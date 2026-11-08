/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native registered formats omitted by SDL's Windows text/image adapter. */
#include "host/core/clipboard/native_hooks.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#define CLIP_LIMIT (64u * 1024u * 1024u)
#include <windows.h>
static DWORD native_sequence;
static HWND native_owner;

static int windows_clipboard(void)
{
    const char *driver = SDL_GetCurrentVideoDriver();
    return driver && !strcmp(driver, "windows");
}

static UINT windows_format(const char *mime)
{
    if (!strcmp(mime, "text/rtf") || !strcmp(mime, "application/rtf"))
        return RegisterClipboardFormatW(L"Rich Text Format");
    if (!strcmp(mime, "text/html"))
        return RegisterClipboardFormatW(L"HTML Format");
    return 0;
}

int lx_clipboard_native_has(const char *mime)
{
    UINT format = windows_format(mime);
    return format && IsClipboardFormatAvailable(format);
}

void *lx_clipboard_native_read(const char *mime, size_t *size)
{
    UINT format = windows_format(mime);
    if (!format || !OpenClipboard(NULL))
        return NULL;
    HGLOBAL handle = GetClipboardData(format);
    SIZE_T length = handle ? GlobalSize(handle) : 0;
    void *source = length && length <= CLIP_LIMIT ? GlobalLock(handle) : NULL;
    void *copy = source ? malloc(length + 1) : NULL;
    if (copy)
    {
        memcpy(copy, source, length);
        ((char *)copy)[length] = 0;
        *size = length;
    }
    if (source)
        GlobalUnlock(handle);
    CloseClipboard();
    if (copy && !strcmp(mime, "text/html"))
    {
        void *html = lx_host_clipboard_html_extract(copy, *size, size);
        free(copy);
        copy = html;
    }
    return copy;
}

int lx_clipboard_native_publish(void *context, LxClipboardNativeRead read,
                                const LxHostClipboardItem *items, size_t count)
{
    if (!windows_clipboard())
        return 1;
    int result = 1;
    for (size_t i = 0; i < count; i++)
    {
        const char *mime = items[i].mime;
        UINT format = windows_format(mime);
        if (!format)
            continue;
        size_t size = 0;
        const void *source = read(context, mime, &size);
        void *html = NULL;
        if (source && !strcmp(mime, "text/html"))
            source = html = lx_host_clipboard_html_wrap(source, size, &size);
        HGLOBAL memory = source && size <= CLIP_LIMIT ? GlobalAlloc(GMEM_MOVEABLE, size + 1) : NULL;
        void *target = memory ? GlobalLock(memory) : NULL;
        if (target)
        {
            memcpy(target, source, size);
            ((char *)target)[size] = 0;
            GlobalUnlock(memory);
        }
        int opened = target && OpenClipboard(NULL);
        if (!opened || !SetClipboardData(format, memory))
        {
            result = 0;
            if (memory)
                GlobalFree(memory);
        }
        if (opened)
            CloseClipboard();
        free(html);
    }
    native_owner = GetClipboardOwner();
    native_sequence = GetClipboardSequenceNumber();
    return result;
}

int lx_clipboard_native_owned(void)
{
    return windows_clipboard() && native_owner && GetClipboardOwner() == native_owner &&
           GetClipboardSequenceNumber() == native_sequence;
}

int lx_clipboard_native_format(const char *mime)
{
    return windows_clipboard() && windows_format(mime);
}

const char *lx_clipboard_native_offer_mime(const char *mime)
{
    return windows_clipboard() && !strcmp(mime, "text/html") ? "application/x-2ine-native-html"
                                                             : mime;
}
