/* Public domain. No rights reserved. Wholly AI-authored. */
/* Atom tables: the system atom table, which also names the clipboard formats,
   and private tables. They share the clipboard lock. */
#include "os2/ps/pmmerge/pmwin/clipboard.h"
#include <strings.h>
#include <ctype.h>

enum
{
    MAX_ATOM_NAME = 255,
    MAX_ATOM_TABLE_INITIAL_SIZE = 16 * 1024 * 1024,
    MAX_ATOM_TABLE_BUCKETS = 65535
};

typedef struct SystemAtom
{
    char *name;
    ULONG id, references, table;
    struct SystemAtom *next;
} SystemAtom;

static SystemAtom *atoms;

typedef struct PrivateAtomTable
{
    ULONG handle;
    struct PrivateAtomTable *next;
} PrivateAtomTable;

static PrivateAtomTable *atom_tables;
static ULONG next_atom_table = SYSTEM_ATOM_TABLE + 1;

static BOOL valid_atom_table(ULONG table)
{
    if (table == SYSTEM_ATOM_TABLE)
        return TRUE;
    for (PrivateAtomTable *item = atom_tables; item; item = item->next)
        if (item->handle == table)
            return TRUE;
    return FALSE;
}

ULONG WinQuerySystemAtomTable(VOID)
{
    return SYSTEM_ATOM_TABLE;
}

static ULONG atom_name(ULONG table, PSZ name, BOOL add)
{
    if (!name)
        return 0;
    uintptr_t value = (uintptr_t)name;
    /* An integer atom arrives bare or in MAKEINTATOM form. */
    if (value < 0x10000 || (value <= UINT32_MAX && value >= PM_INTEGER_ATOM_BASE))
    {
        if (!lock_clipboard())
            return 0;
        ULONG result = valid_atom_table(table) ? (ULONG)(value & 0xffff) : 0;
        lx_host_mutex_unlock(clipboard_mutex);
        return result;
    }
    if (!*name || strlen(name) > MAX_ATOM_NAME || !lock_clipboard())
        return 0;
    if (!valid_atom_table(table))
    {
        lx_host_mutex_unlock(clipboard_mutex);
        return 0;
    }
    SystemAtom *item = atoms;
    ULONG id = FIRST_STRING_ATOM;
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
    if (atom < FIRST_STRING_ATOM)
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

ULONG WinCreateAtomTable(ULONG initial, ULONG buckets)
{
    if (initial > MAX_ATOM_TABLE_INITIAL_SIZE || buckets > MAX_ATOM_TABLE_BUCKETS ||
        !lock_clipboard())
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
    if (atom && atom < FIRST_STRING_ATOM)
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

/* The name of a clipboard format registered in the system atom table, or NULL.
   The caller holds the clipboard lock. */
const char *systemAtomName(ULONG format)
{
    for (SystemAtom *item = atoms; item; item = item->next)
        if (item->table == SYSTEM_ATOM_TABLE && item->id == format)
            return item->name;
    return NULL;
}
