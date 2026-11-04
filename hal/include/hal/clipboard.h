/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host clipboard MIME offers own immutable copies, never guest addresses. */
#ifndef TWOINE_HAL_CLIPBOARD_H
#define TWOINE_HAL_CLIPBOARD_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostClipboardItem
{
    const char *mime;
    const void *data;
    size_t size;
} LxHostClipboardItem;
/* A delayed provider returns malloc-owned bytes. It runs on the video/main
   thread without a clipboard HAL lock, and is called at most once per MIME. */
typedef void *(*LxHostClipboardProvider)(void *context, const char *mime, size_t *size);
typedef struct LxHostClipboardState
{
    uint64_t revision;
    int owned;
} LxHostClipboardState;
int lx_host_clipboard_state(LxHostClipboardState *state);
int lx_host_clipboard_has(const char *mime);
void *lx_host_clipboard_read(const char *mime, size_t *size);
int lx_host_clipboard_offer(const LxHostClipboardItem *items, size_t count,
                            LxHostClipboardProvider provider, void *context,
                            LxHostClipboardState *state);
/* Detach any delayed provider by materializing its current offer. Does not
   access another application's clipboard or clear it after ownership loss. */
int lx_host_clipboard_materialize(void);
/* Pixels are top-down RGBA bytes. Returned bytes/pixels use malloc ownership. */
void *lx_host_clipboard_encode_image(const char *mime, const void *rgba, unsigned width,
                                     unsigned height, size_t *size);
void *lx_host_clipboard_decode_image(const char *mime, const void *bytes, size_t size,
                                     unsigned *width, unsigned *height);
/* CF_TEXT uses an explicit supported OS/2 codepage. UTF-8 input is validated;
   unrepresentable Unicode characters become '?', as CF_TEXT has no Unicode. */
void *lx_host_clipboard_text_utf8(unsigned codepage, const void *bytes, size_t size,
                                  size_t *output_size);
void *lx_host_clipboard_text_os2(unsigned codepage, const void *utf8, size_t size,
                                 size_t *output_size);
/* CF_HTML offsets count UTF-8 bytes from the start of the complete payload. */
void *lx_host_clipboard_html_wrap(const void *html, size_t size, size_t *output_size);
void *lx_host_clipboard_html_extract(const void *data, size_t size, size_t *output_size);
#endif
