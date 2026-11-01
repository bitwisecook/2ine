/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared Open32 icons retain validated copies of original OS/2 pointer resources. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
typedef struct Open32Icon
{
    ULONG handle, module, identity, size;
    uint8 *bytes;
    struct Open32Icon *next;
} Open32Icon;
static Open32Icon *icons;
static void destroy_icon(void *resource)
{
    Open32Icon *icon = resource;
    free(icon->bytes);
    free(icon);
}
ULONG LoadIcon(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadIcon(%u, %p)", module, name);
    if ((!module || module == UINT32_MAX) && GLoaderState.main_module)
        module = GLoaderState.main_module->handle;
    ULONG resource = FindResource(module, name, (PSZ)(uintptr_t)3);
    if (!resource)
        return 0;
    for (Open32Icon *icon = icons; icon; icon = icon->next)
        if (icon->module == module && icon->identity == resource)
            return icon->handle;
    ULONG size = 0;
    void *data = NULL;
    if (!GLoaderState.queryResourceSize || !GLoaderState.getResource ||
        GLoaderState.queryResourceSize(module, 1, resource & 0xFFFF, &size) || size < 26 ||
        size > 1048576 || GLoaderState.getResource(module, 1, resource & 0xFFFF, &data))
    {
        open32SetError(1814);
        return 0;
    }
    const uint8 *bytes = data;
    ULONG offset = lx_guest_load_u16(bytes) == 0x4142 ? 14 : 0;
    Open32Icon *icon = NULL;
    if (size - offset >= 26 && (lx_guest_load_u16(bytes + offset) == 0x4943 ||
                                lx_guest_load_u16(bytes + offset) == 0x4349))
    {
        ULONG header = lx_guest_load_u32(bytes + offset + 14);
        if ((header == 12 || header == 40 || header == 64) && header <= size - offset - 14 &&
            lx_guest_load_u32(bytes + offset + 10) < size)
        {
            icon = calloc(1, sizeof *icon);
            if (icon)
                icon->bytes = malloc(size);
            if (icon && !icon->bytes)
            {
                free(icon);
                icon = NULL;
            }
            if (icon)
                memcpy(icon->bytes, bytes, size);
        }
    }
    if (GLoaderState.freeResource)
        GLoaderState.freeResource(data);
    if (!icon)
    {
        open32SetError(1814);
        return 0;
    }
    icon->size = size;
    icon->module = module;
    icon->identity = resource;
    icon->handle = lx_handle_create(LX_HANDLE_OPEN32_ICON, icon, destroy_icon);
    if (!icon->handle)
    {
        destroy_icon(icon);
        open32SetError(8);
        return 0;
    }
    icon->next = icons;
    icons = icon;
    return icon->handle;
}
BOOL DestroyIcon(ULONG handle)
{
    /* LoadIcon returns shared resources; none of these handles is caller-owned. */
    (void)handle;
    open32SetError(6);
    return FALSE;
}
void open32IconsCleanup(void)
{
    while (icons)
    {
        Open32Icon *icon = icons;
        icons = icon->next;
        lx_handle_close(icon->handle, LX_HANDLE_OPEN32_ICON);
    }
}
