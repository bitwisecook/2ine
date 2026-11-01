/* Public domain. No rights reserved. Wholly AI-authored. */
/* Presentation attributes are copied, inherited through owners and changed synchronously. */
#include "os2libs/pmwin/internal.h"
#include "guest/abi.h"

static void notify(AnchorBlock *anchor, Window *window, ULONG id)
{
    HWND subject = window->hwnd;
    sendMessage(window, WM_PRESPARAMCHANGED, (MPARAM)(uintptr_t)id, 0);
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *w = anchor->windows[i];
        if (!w || !w->hwnd || w->hwnd == subject)
            continue;
        for (Window *owner = w->owner; owner; owner = owner->owner)
            if (owner->hwnd == subject)
            {
                sendMessage(w, WM_PRESPARAMCHANGED, (MPARAM)(uintptr_t)id, 0);
                break;
            }
    }
}
void destroyPresentationParameters(Window *window)
{
    while (window->parameters)
    {
        PmPresentationParameter *p = window->parameters;
        window->parameters = p->next;
        free(p);
    }
}
BOOL WinSetPresParam(HWND window, ULONG id, ULONG size, PVOID data)
{
    TRACE_NATIVE("WinSetPresParam(%u, %u, %u, %p)", (uint)window, (uint)id, (uint)size, data);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w || !id || !data || !size || size > 65536)
        return FALSE;
    PmPresentationParameter *p = malloc(sizeof *p + size);
    if (!p)
        return FALSE;
    p->id = id;
    p->size = size;
    memcpy(p->data, data, size);
    PmPresentationParameter **slot = &w->parameters;
    while (*slot && (*slot)->id != id)
        slot = &(*slot)->next;
    PmPresentationParameter *previous = *slot;
    p->next = previous ? previous->next : NULL;
    *slot = p;
    free(previous);
    notify(anchor, w, id);
    return TRUE;
}
BOOL WinRemovePresParam(HWND window, ULONG id)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w)
        return FALSE;
    PmPresentationParameter **slot = &w->parameters;
    while (*slot && (*slot)->id != id)
        slot = &(*slot)->next;
    if (!*slot)
        return FALSE;
    PmPresentationParameter *p = *slot;
    *slot = p->next;
    free(p);
    notify(anchor, w, id);
    return TRUE;
}
ULONG WinQueryPresParam(HWND window, ULONG first, ULONG second, PULONG found, ULONG capacity,
                        PVOID output, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *w = anchor ? getWindowFromHWND(anchor, window) : NULL;
    if (!w || (flags & ~15u) || (capacity && !output))
        return 0;
    for (; w; w = flags & 1 ? NULL : w->owner)
    {
        PmPresentationParameter *p = NULL;
        for (PmPresentationParameter *entry = w->parameters; entry; entry = entry->next)
            if (entry->id == first || (second && entry->id == second))
            {
                p = entry;
                if (p->id == first)
                    break;
            }
        if (!p)
            continue;
        uint8 color[4];
        const void *data = p->data;
        ULONG size = p->size;
        if ((p->id == first && (flags & 2)) || (p->id == second && (flags & 4)))
        {
            if (size != 4)
                return 0;
            LONG index = (LONG)lx_guest_load_u32(p->data);
            if (index < CLR_FALSE || index > CLR_PALEGRAY)
                return 0;
            uint8 r, g, b;
            os2ColorIndexToRGB(index, &r, &g, &b);
            lx_guest_store_u32(color, (ULONG)r << 16 | (ULONG)g << 8 | b);
            data = color;
            size = sizeof color;
        }
        /* Every host RGB value is a pure color in the true-color renderer. */
        if (found)
            *found = p->id;
        ULONG copied = size < capacity ? size : capacity;
        if (copied)
            memcpy(output, data, copied);
        return copied;
    }
    return 0;
}
