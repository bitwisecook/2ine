/* Public domain. No rights reserved. Wholly AI-authored. */
/* The PM clipboard: format storage, ownership and transfers between guest
   memory and the host clipboard. */
#include "os2/include/private/pmwinp.h"
#include "os2/ps/pmmerge/pmwin/clipboard.h"
#include "os2/include/private/pmp.h"
#include "host/sync.h"
#include "host/clipboard.h"
#include "os2/include/private/pmgrep.h"
#include <strings.h>
#include <ctype.h>

enum
{
    DEFAULT_CODEPAGE = 437,
    MAX_CLIPBOARD_DATA = 64 * 1024 * 1024,
    MAX_GUEST_DATA_SCAN = 16 * 1024 * 1024,
    MAX_OFFERED_TYPES = 64
};

/* A PNG stream is its signature, then chunks of length, type, data and CRC. */
enum
{
    PNG_SIGNATURE_SIZE = 8,
    PNG_CHUNK_TYPE_OFFSET = 4,
    PNG_CHUNK_OVERHEAD = 12
};

typedef struct ClipFormat
{
    ULONG format, data, flags, codepage;
    uint64_t serial;
    LxGuestMapping *mapping;
    size_t length;
    char *name;
    int imported;
    struct ClipFormat *next;
} ClipFormat;

static LxHostOnce clipboard_once = LX_HOST_ONCE_INIT;
LxHostMutex *clipboard_mutex;
static AnchorBlock *clipboard_open;
static HWND clipboard_owner;
static AnchorBlock *owner_anchor;
static uint64_t clipboard_revision, clipboard_serial;
static BOOL clipboard_dirty;
static _Thread_local ULONG rendering_format;
static ClipFormat *formats;

static void initialize_clipboard(void)
{
    clipboard_mutex = lx_host_mutex_create(0, 0);
}

BOOL lock_clipboard(void)
{
    lx_host_once(&clipboard_once, initialize_clipboard);
    return clipboard_mutex && lx_host_mutex_lock(clipboard_mutex, UINT32_MAX) == LX_HOST_LOCK_OK;
}

static ClipFormat *find_format(ULONG format)
{
    for (ClipFormat *item = formats; item; item = item->next)
        if (item->format == format)
            return item;
    return NULL;
}

static const char *format_mime(ULONG format, const char *name)
{
    if (format == CF_TEXT)
        return "text/plain;charset=utf-8";
    if (format == CF_BITMAP)
        return "image/png";
    if (!name)
        return NULL;
    if (!strcasecmp(name, "Rich Text Format") || !strcasecmp(name, "text/rtf") ||
        !strcasecmp(name, "application/rtf"))
        return "text/rtf";
    if (!strcasecmp(name, "HTML Format") || !strcasecmp(name, "text/html"))
        return "text/html";
    if (!strcasecmp(name, "PNG") || !strcasecmp(name, "image/png"))
        return "image/png";
    return NULL;
}

typedef struct ClipboardFree
{
    ULONG address;
    struct ClipboardFree *next;
} ClipboardFree;

static ClipboardFree *pending_free;

static void drain_clipboard_free(void)
{
    if (!lock_clipboard())
        return;
    ClipboardFree *pending = pending_free;
    pending_free = NULL;
    lx_host_mutex_unlock(clipboard_mutex);
    while (pending)
    {
        ClipboardFree *next = pending->next;
        void *host = GProcessState.execution
                         ? lx_execution_memory(GProcessState.execution, pending->address, 1, 0)
                         : NULL;
        if (host && GProcessState.freeSharedMemory)
            GProcessState.freeSharedMemory(host);
        free(pending);
        pending = next;
    }
}

static void discard_format(ClipFormat *item)
{
    if (item->mapping)
        lx_execution_unmap(GProcessState.execution, item->mapping);
    else if (item->data && !(item->flags & CFI_OWNERFREE))
    {
        if (item->format == CF_BITMAP)
            lx_pm_bitmap_delete(item->data);
        else if ((item->flags & CFI_POINTER) && GProcessState.execution)
        {
            ClipboardFree *pending = malloc(sizeof *pending);
            if (pending)
            {
                pending->address = item->data;
                pending->next = pending_free;
                pending_free = pending;
            }
        }
    }
    free(item->name);
    free(item);
}

static void discard_formats(void)
{
    while (formats)
    {
        ClipFormat *item = formats;
        formats = item->next;
        discard_format(item);
    }
    clipboard_serial++;
}

static void synchronize_clipboard(void)
{
    LxHostClipboardState state;
    if (!lx_host_clipboard_state(&state) || !lock_clipboard())
        return;
    HWND previous = 0;
    if (!clipboard_dirty && !state.owned && state.revision != clipboard_revision)
    {
        previous = clipboard_owner;
        discard_formats();
        clipboard_owner = 0;
        owner_anchor = NULL;
    }
    clipboard_revision = state.revision;
    lx_host_mutex_unlock(clipboard_mutex);
    drain_clipboard_free();
    if (previous)
        WinSendMsg(previous, WM_DESTROYCLIPBOARD, 0, 0);
}

/* A root RTF group can contain embedded NUL bytes in a \binN payload. */
static size_t rtf_length(const unsigned char *bytes, size_t size)
{
    if (size < 6 || memcmp(bytes, "{\\rtf", 5))
        return 0;
    unsigned depth = 0;
    for (size_t i = 0; i < size; i++)
    {
        if (bytes[i] == '{')
            depth++;
        else if (bytes[i] == '}' && depth && !--depth)
            return i + 1;
        else if (bytes[i] == '\\' && i + 1 < size)
        {
            i++;
            if (size - i >= 3 && !memcmp(bytes + i, "bin", 3))
            {
                size_t at = i + 3, length = 0;
                if (at == size || !isdigit(bytes[at]))
                    continue;
                while (at < size && isdigit(bytes[at]))
                {
                    if (length > MAX_CLIPBOARD_DATA / 10)
                        return 0;
                    length = length * 10 + bytes[at++] - '0';
                }
                if (at < size && bytes[at] == ' ')
                    at++;
                if (length > size - at)
                    return 0;
                i = at + length - 1;
            }
        }
    }
    return 0;
}

static void *copy_pointer(ClipFormat *item, size_t *size)
{
    if (!GProcessState.execution || !item->data)
        return NULL;
    LxGuestMapping *mapping = lx_execution_mapping_at(GProcessState.execution, item->data);
    if (!mapping)
        return NULL;
    size_t offset = (size_t)item->data - lx_execution_mapping_address(mapping);
    size_t capacity = lx_execution_mapping_size(mapping) - offset;
    if (capacity > MAX_GUEST_DATA_SCAN)
        capacity = MAX_GUEST_DATA_SCAN;
    if (item->length && item->length + 1 < capacity)
        capacity = item->length + 1;
    LxGuestBorrow borrow = {0};
    if (!capacity ||
        !lx_execution_borrow(GProcessState.execution, item->data, capacity, LX_GUEST_READ, &borrow))
        return NULL;
    const unsigned char *source = borrow.host;
    size_t length = 0;
    if (item->name && !format_mime(item->format, item->name))
        length = item->length ? item->length : capacity;
    else if (item->name && (!strcasecmp(item->name, "PNG") || !strcasecmp(item->name, "image/png")))
    {
        if (item->length)
            length = item->length;
        else if (capacity >= PNG_SIGNATURE_SIZE + PNG_CHUNK_OVERHEAD &&
                 !memcmp(source, "\x89PNG\r\n\x1a\n", PNG_SIGNATURE_SIZE))
            /* An unsized PNG ends with its IEND chunk. */
            for (size_t at = PNG_SIGNATURE_SIZE; at <= capacity - PNG_CHUNK_OVERHEAD;)
            {
                uint32_t chunk = (uint32_t)source[at] << 24 | (uint32_t)source[at + 1] << 16 |
                                 (uint32_t)source[at + 2] << 8 | source[at + 3];
                if (chunk > capacity - at - PNG_CHUNK_OVERHEAD)
                    break;
                if (!memcmp(source + at + PNG_CHUNK_TYPE_OFFSET, "IEND", 4))
                {
                    if (!chunk)
                        length = at + PNG_CHUNK_OVERHEAD;
                    break;
                }
                at += (size_t)chunk + PNG_CHUNK_OVERHEAD;
            }
    }
    else if (item->name && format_mime(item->format, item->name) &&
             !strcmp(format_mime(item->format, item->name), "text/rtf"))
        length = rtf_length(source, capacity);
    else
    {
        const void *end = memchr(source, 0, capacity);
        if (end)
            length = (const unsigned char *)end - source;
    }
    void *copy =
        length <= capacity && (length || (capacity && !source[0])) ? malloc(length + 1) : NULL;
    if (copy)
    {
        memcpy(copy, source, length);
        ((char *)copy)[length] = 0;
        *size = length;
    }
    lx_execution_release(&borrow);
    return copy;
}

static void *bitmap_bytes(ULONG handle, const char *mime, size_t *size)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_BITMAP, &borrow))
        return NULL;
    const PmBitmap *bitmap = borrow.resource;
    size_t count = (size_t)bitmap->width * bitmap->height;
    uint8_t *rgba = malloc(count * 4);
    if (rgba)
        for (unsigned y = 0; y < bitmap->height; y++)
            for (unsigned x = 0; x < bitmap->width; x++)
            {
                uint32_t rgb = bitmap->pixels[(size_t)(bitmap->height - y - 1) * bitmap->width + x];
                size_t at = ((size_t)y * bitmap->width + x) * 4;
                rgba[at] = rgb >> 16;
                rgba[at + 1] = rgb >> 8;
                rgba[at + 2] = rgb;
                rgba[at + 3] = 255;
            }
    void *bytes =
        rgba ? lx_host_clipboard_encode_image(mime, rgba, bitmap->width, bitmap->height, size)
             : NULL;
    free(rgba);
    lx_handle_release(&borrow);
    return bytes;
}

static void *provide_clipboard(void *context, const char *mime, size_t *size)
{
    (void)context;
    if (!lock_clipboard())
        return NULL;
    ClipFormat *item = formats;
    while (item)
    {
        const char *candidate = format_mime(item->format, item->name);
        if (candidate &&
            (!strcmp(candidate, mime) || (item->format == CF_BITMAP && !strcmp(mime, "image/bmp"))))
            break;
        item = item->next;
    }
    ULONG format = item ? item->format : 0;
    HWND owner = clipboard_owner;
    BOOL delayed = item && !item->data;
    lx_host_mutex_unlock(clipboard_mutex);
    if (delayed && owner)
    {
        ULONG previous = rendering_format;
        rendering_format = format;
        WinSendMsg(owner, WM_RENDERFMT, (MPARAM)(uintptr_t)format, 0);
        rendering_format = previous;
    }
    if (!lock_clipboard())
        return NULL;
    item = find_format(format);
    void *bytes = NULL;
    if (item && item->data)
    {
        if (format == CF_BITMAP)
            bytes = bitmap_bytes(item->data, mime, size);
        else
        {
            bytes = copy_pointer(item, size);
            if (bytes && format == CF_TEXT)
            {
                void *converted = lx_host_clipboard_text_utf8(item->codepage, bytes, *size, size);
                free(bytes);
                bytes = converted;
            }
            else if (bytes && item->name && !strcasecmp(item->name, "HTML Format"))
            {
                void *html = lx_host_clipboard_html_extract(bytes, *size, size);
                free(bytes);
                bytes = html;
            }
        }
    }
    lx_host_mutex_unlock(clipboard_mutex);
    return bytes;
}

static BOOL publish_clipboard(void)
{
    if (!lock_clipboard())
        return FALSE;
    LxHostClipboardItem items[MAX_OFFERED_TYPES] = {{0}};
    size_t count = 0;
    /* Leave room for the second type a bitmap offers. */
    for (ClipFormat *item = formats; item && count < MAX_OFFERED_TYPES - 1; item = item->next)
    {
        const char *mime = format_mime(item->format, item->name);
        if (!mime)
            continue;
        BOOL duplicate = FALSE;
        for (size_t i = 0; i < count; i++)
            if (!strcmp(mime, items[i].mime))
                duplicate = TRUE;
        if (!duplicate)
            items[count++].mime = mime;
        if (item->format == CF_BITMAP)
            items[count++].mime = "image/bmp";
    }
    /* MIME names are static strings, and providers copy guest data on demand. */
    lx_host_mutex_unlock(clipboard_mutex);
    LxHostClipboardState state;
    BOOL result = lx_host_clipboard_offer(items, count, provide_clipboard, NULL, &state);
    if (result && lock_clipboard())
    {
        clipboard_revision = state.revision;
        clipboard_dirty = FALSE;
        lx_host_mutex_unlock(clipboard_mutex);
    }
    return result;
}

BOOL WinOpenClipbrd(HAB hab)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
        return FALSE;
    synchronize_clipboard();
    if (!lock_clipboard())
        return FALSE;
    BOOL result = clipboard_open == NULL;
    if (result)
        clipboard_open = anchor;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

BOOL WinCloseClipbrd(HAB hab)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
        return FALSE;
    BOOL result = clipboard_open == anchor;
    BOOL publish = result && clipboard_dirty;
    if (result)
        clipboard_open = NULL;
    lx_host_mutex_unlock(clipboard_mutex);
    /* Native publication failure never discards valid guest/private data. */
    if (publish)
        publish_clipboard();
    return result;
}

BOOL WinEmptyClipbrd(HAB hab)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
        return FALSE;
    BOOL result = clipboard_open == anchor;
    HWND previous = result ? clipboard_owner : 0;
    if (result)
    {
        discard_formats();
        clipboard_owner = 0;
        owner_anchor = NULL;
        clipboard_dirty = TRUE;
    }
    lx_host_mutex_unlock(clipboard_mutex);
    drain_clipboard_free();
    if (previous)
        WinSendMsg(previous, WM_DESTROYCLIPBOARD, 0, 0);
    return result;
}

HWND WinQueryClipbrdOwner(HAB hab)
{
    if (!getAnchorBlock(hab))
        return 0;
    synchronize_clipboard();
    if (!lock_clipboard())
        return 0;
    HWND result = clipboard_owner;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

BOOL WinSetClipbrdOwner(HAB hab, HWND owner)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || (owner && !getWindowFromHWND(anchor, owner)) || !lock_clipboard())
        return FALSE;
    BOOL result = clipboard_open == anchor;
    if (result)
    {
        clipboard_owner = owner;
        owner_anchor = owner ? anchor : NULL;
    }
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

static ULONG host_format(ULONG format)
{
    if (!lock_clipboard())
        return 0;
    BOOL changing = clipboard_dirty;
    lx_host_mutex_unlock(clipboard_mutex);
    if (changing)
        return 0;
    const char *name = NULL;
    if (format >= FIRST_STRING_ATOM && lock_clipboard())
    {
        const char *found = systemAtomName(format);
        name = found ? strdup(found) : NULL;
        lx_host_mutex_unlock(clipboard_mutex);
    }
    const char *mime = format_mime(format, name);
    BOOL available = mime && lx_host_clipboard_has(mime);
    if (!available && format == CF_TEXT)
        available = lx_host_clipboard_has("text/plain");
    if (!available && format == CF_BITMAP)
        available = lx_host_clipboard_has("image/bmp");
    if (!available && mime && !strcmp(mime, "text/rtf"))
        available = lx_host_clipboard_has("application/rtf");
    free((void *)name);
    return available;
}

BOOL WinQueryClipbrdFmtInfo(HAB hab, ULONG format, PULONG flags)
{
    if (!getAnchorBlock(hab) || !flags)
        return FALSE;
    synchronize_clipboard();
    if (!lock_clipboard())
        return FALSE;
    ClipFormat *item = find_format(format);
    if (item)
        *flags = item->flags;
    lx_host_mutex_unlock(clipboard_mutex);
    if (item)
        return TRUE;
    if (!host_format(format))
        return FALSE;
    *flags = format == CF_BITMAP ? CFI_HANDLE : CFI_POINTER;
    return TRUE;
}

static void import_host_formats(void)
{
    static const char *names[] = {"Rich Text Format", "HTML Format", "PNG", NULL};
    ULONG list[5] = {CF_TEXT, CF_BITMAP, 0, 0, 0};
    for (unsigned i = 0; names[i]; i++)
        list[i + 2] = WinFindAtom(SYSTEM_ATOM_TABLE, (PSZ)names[i]);
    /* Only create registered atoms when that native MIME is advertised. */
    const char *mimes[] = {"text/rtf", "text/html", "image/png"};
    for (unsigned i = 0; names[i]; i++)
        if (!list[i + 2] && lx_host_clipboard_has(mimes[i]))
            list[i + 2] = WinAddAtom(SYSTEM_ATOM_TABLE, (PSZ)names[i]);
    for (unsigned i = 0; i < 5; i++)
    {
        ULONG format = list[i];
        if (!format || !host_format(format) || !lock_clipboard())
            continue;
        if (!find_format(format))
        {
            ClipFormat *item = calloc(1, sizeof *item);
            if (item)
            {
                item->format = format;
                item->serial = ++clipboard_serial;
                item->flags = format == CF_BITMAP ? CFI_HANDLE : CFI_POINTER;
                item->codepage = GProcessState.process_codepage ? GProcessState.process_codepage
                                                                : DEFAULT_CODEPAGE;
                item->imported = 1;
                const char *name = systemAtomName(format);
                item->name = name ? strdup(name) : NULL;
                ClipFormat **end = &formats;
                while (*end)
                    end = &(*end)->next;
                *end = item;
            }
        }
        lx_host_mutex_unlock(clipboard_mutex);
    }
}

static void import_requested_format(ULONG format)
{
    if (!host_format(format) || !lock_clipboard())
        return;
    if (!find_format(format))
    {
        ClipFormat *item = calloc(1, sizeof *item);
        if (item)
        {
            item->format = format;
            item->serial = ++clipboard_serial;
            item->flags = format == CF_BITMAP ? CFI_HANDLE : CFI_POINTER;
            item->codepage =
                GProcessState.process_codepage ? GProcessState.process_codepage : DEFAULT_CODEPAGE;
            item->imported = 1;
            const char *name = systemAtomName(format);
            item->name = name ? strdup(name) : NULL;
            ClipFormat **end = &formats;
            while (*end)
                end = &(*end)->next;
            *end = item;
        }
    }
    lx_host_mutex_unlock(clipboard_mutex);
}

ULONG WinEnumClipbrdFmts(HAB hab, ULONG format)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
        return 0;
    synchronize_clipboard();
    LxHostClipboardState state;
    if (lx_host_clipboard_state(&state) && !state.owned)
        import_host_formats();
    import_requested_format(format);
    if (!lock_clipboard())
        return 0;
    ClipFormat *item = clipboard_open == anchor ? formats : NULL;
    if (format)
    {
        while (item && item->format != format)
            item = item->next;
        if (item)
            item = item->next;
    }
    ULONG result = item ? item->format : 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

static BOOL install_bytes(ClipFormat *item, const void *bytes, size_t size)
{
    if (!GProcessState.execution || size > MAX_CLIPBOARD_DATA)
        return FALSE;
    /* Room for a terminating NUL. */
    size_t capacity = (size + 1 + PM_GUEST_PAGE_SIZE - 1) & ~(size_t)(PM_GUEST_PAGE_SIZE - 1);
    LxGuestMapping *mapping =
        lx_execution_map(GProcessState.execution, 0, capacity, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return FALSE;
    memcpy(lx_execution_mapping_host(mapping), bytes, size);
    ((char *)lx_execution_mapping_host(mapping))[size] = 0;
    item->mapping = mapping;
    item->length = size;
    item->data = lx_execution_mapping_address(mapping);
    return TRUE;
}

ULONG WinQueryClipbrdData(HAB hab, ULONG format)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
        return 0;
    BOOL opened = clipboard_open == anchor;
    lx_host_mutex_unlock(clipboard_mutex);
    if (!opened)
        return 0;
    synchronize_clipboard();
    LxHostClipboardState state;
    if (lx_host_clipboard_state(&state) && !state.owned)
        import_host_formats();
    import_requested_format(format);
    if (!lock_clipboard())
        return 0;
    ClipFormat *item = find_format(format);
    BOOL imported = item && item->imported;
    BOOL cfhtml = item && item->name && !strcasecmp(item->name, "HTML Format");
    uint64_t serial = item ? item->serial : 0;
    HWND owner = item && !item->data && !imported ? clipboard_owner : 0;
    ULONG result = item ? item->data : 0;
    const char *mime = item ? format_mime(format, item->name) : NULL;
    lx_host_mutex_unlock(clipboard_mutex);
    if (result)
        return result;
    if (owner)
    {
        ULONG previous = rendering_format;
        rendering_format = format;
        WinSendMsg(owner, WM_RENDERFMT, (MPARAM)(uintptr_t)format, 0);
        rendering_format = previous;
    }
    else if (imported && mime)
    {
        size_t size = 0;
        void *bytes = lx_host_clipboard_read(mime, &size);
        if (!bytes && format == CF_TEXT)
            bytes = lx_host_clipboard_read("text/plain", &size);
        if (!bytes && format == CF_BITMAP)
            bytes = lx_host_clipboard_read("image/bmp", &size), mime = "image/bmp";
        if (!bytes && !strcmp(mime, "text/rtf"))
            bytes = lx_host_clipboard_read("application/rtf", &size);
        if (bytes && format == CF_TEXT)
        {
            void *converted = lx_host_clipboard_text_os2(
                GProcessState.process_codepage ? GProcessState.process_codepage : DEFAULT_CODEPAGE,
                bytes, size, &size);
            free(bytes);
            bytes = converted;
        }
        else if (bytes && cfhtml)
        {
            void *html = lx_host_clipboard_html_wrap(bytes, size, &size);
            free(bytes);
            bytes = html;
        }
        ULONG bitmap = 0;
        if (bytes && format == CF_BITMAP)
        {
            unsigned width, height;
            uint8_t *rgba = lx_host_clipboard_decode_image(mime, bytes, size, &width, &height);
            if (rgba)
            {
                bitmap = lx_pm_bitmap_create(width, height, 1, 24);
                LxHandleBorrow borrow = {0};
                PmBitmap *image = bitmap ? lx_pm_bitmap(bitmap, &borrow) : NULL;
                if (image)
                    for (unsigned y = 0; y < height; y++)
                        for (unsigned x = 0; x < width; x++)
                        {
                            const uint8_t *pixel = rgba + ((size_t)y * width + x) * 4;
                            image->pixels[(size_t)(height - y - 1) * width + x] =
                                (uint32_t)pixel[0] << 16 | (uint32_t)pixel[1] << 8 | pixel[2];
                        }
                lx_handle_release(&borrow);
                free(rgba);
            }
        }
        if (lock_clipboard())
        {
            item = find_format(format);
            if (item && item->serial == serial && item->imported && !item->data && bytes)
            {
                if (format == CF_BITMAP)
                    item->data = bitmap;
                else
                    install_bytes(item, bytes, size);
            }
            lx_host_mutex_unlock(clipboard_mutex);
        }
        free(bytes);
    }
    if (!lock_clipboard())
        return 0;
    item = find_format(format);
    result = item ? item->data : 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

BOOL WinSetClipbrdData(HAB hab, ULONG data, ULONG format, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    const ULONG kinds = CFI_HANDLE | CFI_POINTER;
    if (!anchor || !format || (flags & ~(ULONG)(CFI_OWNERFREE | CFI_OWNERDISPLAY | kinds)) ||
        (flags & kinds) == kinds || !lock_clipboard())
        return FALSE;
    BOOL result =
        clipboard_open == anchor || (rendering_format == format && owner_anchor == anchor);
    if (!data && !clipboard_owner)
        result = FALSE;
    if (result)
    {
        ClipFormat **slot = &formats;
        while (*slot && (*slot)->format != format)
            slot = &(*slot)->next;
        ClipFormat *item = calloc(1, sizeof *item);
        result = item != NULL;
        if (item)
        {
            item->format = format;
            item->data = data;
            item->flags = flags;
            item->codepage =
                GProcessState.process_codepage ? GProcessState.process_codepage : DEFAULT_CODEPAGE;
            const char *name = systemAtomName(format);
            item->name = name ? strdup(name) : NULL;
            if (*slot)
            {
                item->next = (*slot)->next;
                if ((*slot)->data == data)
                {
                    (*slot)->data = 0;
                    item->mapping = (*slot)->mapping;
                    (*slot)->mapping = NULL;
                    item->length = (*slot)->length;
                }
                discard_format(*slot);
            }
            *slot = item;
            if (!rendering_format)
                clipboard_dirty = TRUE;
            item->serial = ++clipboard_serial;
        }
    }
    lx_host_mutex_unlock(clipboard_mutex);
    drain_clipboard_free();
    return result;
}

uint64_t lx_pm_clipboard_serial(void)
{
    synchronize_clipboard();
    if (!lock_clipboard())
        return 0;
    uint64_t result = clipboard_serial;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

uint64_t lx_pm_clipboard_format_serial(ULONG format)
{
    synchronize_clipboard();
    if (!lock_clipboard())
        return 0;
    ClipFormat *item = find_format(format);
    uint64_t serial = item ? item->serial : 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return serial;
}

BOOL lx_pm_clipboard_set_bytes(HAB hab, ULONG format, const void *bytes, size_t size)
{
    ClipFormat staged = {0};
    if (!bytes || size > MAX_CLIPBOARD_DATA || !install_bytes(&staged, bytes, size))
        return FALSE;
    if (!WinSetClipbrdData(hab, staged.data, format, CFI_POINTER))
    {
        lx_execution_unmap(GProcessState.execution, staged.mapping);
        return FALSE;
    }
    if (!lock_clipboard())
        return FALSE;
    ClipFormat *item = find_format(format);
    item->mapping = staged.mapping;
    item->length = staged.length;
    lx_host_mutex_unlock(clipboard_mutex);
    return TRUE;
}

void *lx_pm_clipboard_copy_bytes(HAB hab, ULONG format, size_t *size)
{
    if (!size || !WinQueryClipbrdData(hab, format) || !lock_clipboard())
        return NULL;
    ClipFormat *item = find_format(format);
    void *result = item ? copy_pointer(item, size) : NULL;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

void lx_pm_clipboard_window_destroyed(HWND window)
{
    if (!lock_clipboard())
        return;
    BOOL owner = clipboard_owner == window;
    lx_host_mutex_unlock(clipboard_mutex);
    if (!owner)
        return;
    WinSendMsg(window, WM_RENDERALLFMTS, 0, 0);
    lx_host_clipboard_materialize();
    if (lock_clipboard())
    {
        clipboard_owner = 0;
        owner_anchor = NULL;
        lx_host_mutex_unlock(clipboard_mutex);
    }
}

ULONG lx_pm_clipboard_format_name(ULONG format, char *output, ULONG capacity)
{
    if (!output || !capacity || !lock_clipboard())
        return 0;
    const char *name = systemAtomName(format);
    size_t size = name ? strlen(name) : 0;
    if (size >= capacity)
        size = capacity - 1;
    if (size)
        memcpy(output, name, size);
    output[size] = 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return (ULONG)size;
}

ULONG lx_pm_clipboard_count(void)
{
    synchronize_clipboard();
    LxHostClipboardState state;
    if (lx_host_clipboard_state(&state) && !state.owned)
        import_host_formats();
    if (!lock_clipboard())
        return 0;
    ULONG count = 0;
    for (ClipFormat *item = formats; item; item = item->next)
        count++;
    lx_host_mutex_unlock(clipboard_mutex);
    return count;
}
