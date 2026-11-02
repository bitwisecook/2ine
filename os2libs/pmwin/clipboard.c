/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest clipboard metadata and system atoms are shared by PM threads. */
#include "os2libs/pmwin/internal.h"
#include "hal/host_sync.h"
#include <strings.h>

typedef struct ClipFormat
{
    ULONG format, data, flags;
    struct ClipFormat *next;
} ClipFormat;
typedef struct SystemAtom
{
    char *name;
    ULONG id, references;
    struct SystemAtom *next;
} SystemAtom;
static LxHostOnce clipboard_once = LX_HOST_ONCE_INIT;
static LxHostMutex *clipboard_mutex;
static AnchorBlock *clipboard_open;
static HWND clipboard_owner;
static ClipFormat *formats;
static SystemAtom *atoms;
static void initialize_clipboard(void)
{
    clipboard_mutex = lx_host_mutex_create(0, 0);
}
static BOOL lock_clipboard(void)
{
    lx_host_once(&clipboard_once, initialize_clipboard);
    return clipboard_mutex && lx_host_mutex_lock(clipboard_mutex, UINT32_MAX) == LX_HOST_LOCK_OK;
}
BOOL WinOpenClipbrd(HAB hab)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
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
    if (result)
        clipboard_open = NULL;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}
BOOL WinEmptyClipbrd(HAB hab)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
        return FALSE;
    BOOL result = clipboard_open == anchor;
    if (result)
    {
        while (formats)
        {
            ClipFormat *next = formats->next;
            free(formats);
            formats = next;
        }
        clipboard_owner = 0;
    }
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}
HWND WinQueryClipbrdOwner(HAB hab)
{
    if (!getAnchorBlock(hab) || !lock_clipboard())
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
        clipboard_owner = owner;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}
ULONG WinEnumClipbrdFmts(HAB hab, ULONG format)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
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
BOOL WinQueryClipbrdFmtInfo(HAB hab, ULONG format, PULONG flags)
{
    if (!getAnchorBlock(hab) || !flags || !lock_clipboard())
        return FALSE;
    ClipFormat *item = formats;
    while (item && item->format != format)
        item = item->next;
    if (item)
        *flags = item->flags;
    lx_host_mutex_unlock(clipboard_mutex);
    return item != NULL;
}
ULONG WinQueryClipbrdData(HAB hab, ULONG format)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !lock_clipboard())
        return 0;
    ClipFormat *item = clipboard_open == anchor ? formats : NULL;
    while (item && item->format != format)
        item = item->next;
    ULONG result = item ? item->data : 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}
BOOL WinSetClipbrdData(HAB hab, ULONG data, ULONG format, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    /* Ownership of arbitrary guest pointer allocations is not supported yet. */
    if (!anchor || !format || !data || (flags & ~0x603u) || ((flags & 0x400) && !(flags & 1)) ||
        !lock_clipboard())
        return FALSE;
    BOOL result = clipboard_open == anchor;
    if (result)
    {
        ClipFormat **slot = &formats;
        while (*slot && (*slot)->format != format)
            slot = &(*slot)->next;
        if (!*slot)
            *slot = calloc(1, sizeof **slot);
        result = *slot != NULL;
        if (result)
        {
            (*slot)->format = format;
            (*slot)->data = data;
            (*slot)->flags = flags;
        }
    }
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}
ULONG WinQuerySystemAtomTable(VOID)
{
    return 1;
}
static ULONG atom_name(ULONG table, PSZ name, BOOL add)
{
    if (table != WinQuerySystemAtomTable() || !name)
        return 0;
    uintptr_t value = (uintptr_t)name;
    if (value < 0x10000 || (value <= UINT32_MAX && value >= 0xffff0000))
        return value & 0xffff;
    if (!*name || strlen(name) > 255 || !lock_clipboard())
        return 0;
    SystemAtom *item = atoms;
    ULONG id = 0xc000;
    while (item && strcasecmp(item->name, name))
    {
        if (item->id >= id)
            id = item->id + 1;
        item = item->next;
    }
    if (item && add)
        item->references++;
    if (!item && add && id <= 0xffff)
    {
        item = calloc(1, sizeof *item);
        if (item)
        {
            item->name = strdup(name);
            if (!item->name)
            {
                free(item);
                item = NULL;
            }
            else
            {
                item->id = id;
                item->references = 1;
                item->next = atoms;
                atoms = item;
            }
        }
    }
    ULONG result = item ? item->id : 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}
ULONG WinAddAtom(ULONG table, PSZ name)
{
    return atom_name(table, name, TRUE);
}
ULONG WinFindAtom(ULONG table, PSZ name)
{
    return atom_name(table, name, FALSE);
}
ULONG WinDeleteAtom(ULONG table, ULONG atom)
{
    if (table != WinQuerySystemAtomTable() || !atom)
        return atom;
    if (atom < 0xc000)
        return 0;
    if (!lock_clipboard())
        return atom;
    SystemAtom **slot = &atoms;
    while (*slot && (*slot)->id != atom)
        slot = &(*slot)->next;
    ULONG result = *slot ? 0 : atom;
    if (*slot && !--(*slot)->references)
    {
        SystemAtom *item = *slot;
        *slot = item->next;
        free(item->name);
        free(item);
    }
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

ULONG lx_pm_clipboard_count(void)
{
    if (!lock_clipboard())
        return 0;
    ULONG count = 0;
    for (ClipFormat *item = formats; item; item = item->next)
        count++;
    lx_host_mutex_unlock(clipboard_mutex);
    return count;
}
