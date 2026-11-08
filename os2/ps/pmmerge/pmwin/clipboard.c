/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest clipboard metadata and system atoms are shared by PM threads. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "host/sync.h"
#include "host/clipboard.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include <strings.h>
#include <ctype.h>

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

typedef struct SystemAtom
{
    char *name;
    ULONG id, references, table;
    struct SystemAtom *next;
} SystemAtom;

static LxHostOnce clipboard_once = LX_HOST_ONCE_INIT;
static LxHostMutex *clipboard_mutex;
static AnchorBlock *clipboard_open;
static HWND clipboard_owner;
static AnchorBlock *owner_anchor;
static uint64_t clipboard_revision, clipboard_serial;
static BOOL clipboard_dirty;
static _Thread_local ULONG rendering_format;
static ClipFormat *formats;
static SystemAtom *atoms;

typedef struct PrivateAtomTable
{
    ULONG handle;
    struct PrivateAtomTable *next;
} PrivateAtomTable;

static PrivateAtomTable *atom_tables;
static ULONG next_atom_table = 2;

static BOOL valid_atom_table(ULONG table)
{
    if (table == 1)
        return TRUE;
    for (PrivateAtomTable *item = atom_tables; item; item = item->next)
        if (item->handle == table)
            return TRUE;
    return FALSE;
}

static void initialize_clipboard(void)
{
    clipboard_mutex = lx_host_mutex_create(0, 0);
}

static BOOL lock_clipboard(void)
{
    lx_host_once(&clipboard_once, initialize_clipboard);
    return clipboard_mutex && lx_host_mutex_lock(clipboard_mutex, UINT32_MAX) == LX_HOST_LOCK_OK;
}

#include "os2/ps/pmmerge/pmwin/clipboard_transfer.h"
#include "os2/include/private/pmp.h"

ULONG WinQuerySystemAtomTable(VOID)
{
    return 1;
}

ULONG lx_pm_clipboard_format_name(ULONG format, char *output, ULONG capacity)
{
    if (!output || !capacity || !lock_clipboard())
        return 0;
    const char *name = format_name(format);
    size_t size = name ? strlen(name) : 0;
    if (size >= capacity)
        size = capacity - 1;
    if (size)
        memcpy(output, name, size);
    output[size] = 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return (ULONG)size;
}

static ULONG atom_name(ULONG table, PSZ name, BOOL add)
{
    if (!name)
        return 0;
    uintptr_t value = (uintptr_t)name;
    if (value < 0x10000 || (value <= UINT32_MAX && value >= 0xffff0000))
    {
        if (!lock_clipboard())
            return 0;
        ULONG result = valid_atom_table(table) ? (ULONG)(value & 0xffff) : 0;
        lx_host_mutex_unlock(clipboard_mutex);
        return result;
    }
    if (!*name || strlen(name) > 255 || !lock_clipboard())
        return 0;
    if (!valid_atom_table(table))
    {
        lx_host_mutex_unlock(clipboard_mutex);
        return 0;
    }
    SystemAtom *item = atoms;
    ULONG id = 0xc000;
    while (item && (item->table != table || strcasecmp(item->name, name)))
    {
        if (item->table == table && item->id >= id)
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
                item->table = table;
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
    if (!atom)
        return atom;
    if (!lock_clipboard())
        return atom;
    if (!valid_atom_table(table))
    {
        lx_host_mutex_unlock(clipboard_mutex);
        return atom;
    }
    if (atom < 0xc000)
    {
        lx_host_mutex_unlock(clipboard_mutex);
        return 0;
    }
    SystemAtom **slot = &atoms;
    while (*slot && ((*slot)->table != table || (*slot)->id != atom))
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

ULONG WinCreateAtomTable(ULONG initial, ULONG buckets)
{
    if (initial > 16 * 1024 * 1024 || buckets > 65535 || !lock_clipboard())
        return 0;
    PrivateAtomTable *table = next_atom_table < UINT32_MAX ? calloc(1, sizeof *table) : NULL;
    if (table)
    {
        table->handle = next_atom_table++;
        table->next = atom_tables;
        atom_tables = table;
    }
    ULONG result = table ? table->handle : 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return result;
}

ULONG WinDestroyAtomTable(ULONG handle)
{
    if (!lock_clipboard())
        return handle;
    PrivateAtomTable **slot = &atom_tables;
    while (*slot && (*slot)->handle != handle)
        slot = &(*slot)->next;
    BOOL valid = *slot != NULL;
    if (valid)
    {
        PrivateAtomTable *table = *slot;
        *slot = table->next;
        free(table);
        SystemAtom **atom = &atoms;
        while (*atom)
        {
            SystemAtom *item = *atom;
            if (item->table == handle)
            {
                *atom = item->next;
                free(item->name);
                free(item);
            }
            else
                atom = &item->next;
        }
    }
    lx_host_mutex_unlock(clipboard_mutex);
    return valid ? 0 : handle;
}

ULONG WinQueryAtomName(ULONG table, ULONG atom, PSZ output, ULONG capacity)
{
    if (!output || !capacity || !lock_clipboard())
        return 0;
    const char *name = NULL;
    char number[24];
    if (atom && atom < 0xc000)
    {
        snprintf(number, sizeof number, "#%u", (unsigned)atom);
        name = number;
    }
    else
        for (SystemAtom *p = atoms; p; p = p->next)
            if (p->table == table && p->id == atom)
            {
                name = p->name;
                break;
            }
    size_t n = name ? strlen(name) : 0;
    if (n >= capacity)
        n = capacity - 1;
    if (n)
        memcpy(output, name, n);
    output[n] = 0;
    lx_host_mutex_unlock(clipboard_mutex);
    return n;
}
