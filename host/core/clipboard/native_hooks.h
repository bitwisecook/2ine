/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_CORE_CLIPBOARD_NATIVE_HOOKS_H
#define TWOINE_HOST_CORE_CLIPBOARD_NATIVE_HOOKS_H
#include "host/clipboard.h"
typedef const void *(*LxClipboardNativeRead)(void *, const char *, size_t *);
int lx_clipboard_native_format(const char *mime);
int lx_clipboard_native_has(const char *mime);
void *lx_clipboard_native_read(const char *mime, size_t *size);
int lx_clipboard_native_publish(void *context, LxClipboardNativeRead read,
                                const LxHostClipboardItem *items, size_t count);
int lx_clipboard_native_owned(void);
const char *lx_clipboard_native_offer_mime(const char *mime);
#endif
