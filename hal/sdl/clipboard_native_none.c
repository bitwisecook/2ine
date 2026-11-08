/* Public domain. No rights reserved. Wholly AI-authored. */
/* SDL's MIME provider is sufficient on Cocoa, X11 and Wayland. */
#include "hal/sdl/clipboard_native.h"

int lx_clipboard_native_format(const char *mime)
{
    (void)mime;
    return 0;
}

int lx_clipboard_native_has(const char *mime)
{
    (void)mime;
    return 0;
}

void *lx_clipboard_native_read(const char *mime, size_t *size)
{
    (void)mime;
    *size = 0;
    return NULL;
}

int lx_clipboard_native_publish(void *context, LxClipboardNativeRead read,
                                const LxHostClipboardItem *items, size_t count)
{
    (void)context;
    (void)read;
    (void)items;
    (void)count;
    return 1;
}

int lx_clipboard_native_owned(void)
{
    return 0;
}

const char *lx_clipboard_native_offer_mime(const char *mime)
{
    return mime;
}
