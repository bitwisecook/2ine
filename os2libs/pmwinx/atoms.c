/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 atom references share the worker's case-insensitive string table. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin.h"
#include "host/clock.h"
#include "host/shared_memory.h"
#include "host/sync.h"
#include <stdio.h>

/* Global atoms are shared by every process of a ball, as named semaphores are. The word
   lock is taken only for table scans, and a lock left by a dead process is taken over. */
typedef struct SharedAtom
{
    uint32 references;
    char name[256];
} SharedAtom;

typedef struct AtomTable
{
    uint32 lock;
    SharedAtom atoms[0x4000];
} AtomTable;

static AtomTable *table;
static LxHostOnce once = LX_HOST_ONCE_INIT;

static void initialize(void)
{
    uint64 hash = 1469598103934665603ULL;
    const char *scope = getenv("TWOINE_BALL");
    if (!scope)
        scope = getenv("TWOINE_IPC_DIR");
    for (const char *p = scope; p && *p; p++)
        hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
    char name[40];
    snprintf(name, sizeof name, "/2ine-gatom1-%016llx", (unsigned long long)hash);
    LxHostShared *shared = NULL;
    LxHostSharedResult opened = lx_host_shared_open(name, sizeof *table, &shared);
    if (opened == LX_HOST_SHARED_EXISTS)
        opened = lx_host_shared_open(name, 0, &shared);
    if (opened == LX_HOST_SHARED_OK && lx_host_shared_size(shared) == sizeof *table)
        table = lx_host_shared_map(shared, NULL, 0);
}

static BOOL lock_atoms(void)
{
    lx_host_once(&once, initialize);
    if (!table)
        return FALSE;
    for (unsigned attempt = 0;; attempt++)
    {
        uint32 expected = 0;
        if (__atomic_compare_exchange_n(&table->lock, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
            return TRUE;
        if (attempt == 2000)
        {
            __atomic_store_n(&table->lock, 1, __ATOMIC_ACQUIRE);
            return TRUE;
        }
        lx_host_sleep_ms(1);
    }
}

static void unlock_atoms(void)
{
    __atomic_store_n(&table->lock, 0, __ATOMIC_RELEASE);
}

static ULONG integer_atom(PSZ name)
{
    if ((uintptr_t)name <= 0xFFFF)
        return (ULONG)(uintptr_t)name < 0xC000 ? (ULONG)(uintptr_t)name : 0;
    if (*name != '#')
        return 0;
    char *end;
    unsigned long value = strtoul(name + 1, &end, 10);
    return end != name + 1 && !*end && value > 0 && value < 0xC000 ? (ULONG)value : 0;
}

static ULONG find_atom(PSZ name, BOOL add)
{
    ULONG integer = integer_atom(name);
    if (integer)
        return integer;
    if ((uintptr_t)name <= 0xFFFF || !*name || strlen(name) > 255 || *name == '#')
    {
        open32SetError(87);
        return 0;
    }
    if (!lock_atoms())
    {
        open32SetError(8);
        return 0;
    }
    ULONG result = 0, vacant = 0x4000;
    for (ULONG i = 0; i < 0x4000; i++)
    {
        SharedAtom *atom = table->atoms + i;
        if (!atom->references)
        {
            if (vacant == 0x4000)
                vacant = i;
        }
        else if (!strcasecmp(atom->name, name))
        {
            vacant = 0x4000;
            if (!add || atom->references != UINT32_MAX)
            {
                result = 0xC000 + i;
                if (add)
                    atom->references++;
            }
            break;
        }
    }
    if (!result && add && vacant < 0x4000)
    {
        SharedAtom *atom = table->atoms + vacant;
        memcpy(atom->name, name, strlen(name) + 1);
        atom->references = 1;
        result = 0xC000 + vacant;
    }
    unlock_atoms();
    if (!result)
        open32SetError(add ? 8 : 2);
    return result;
}

ULONG GlobalAddAtom(PSZ name)
{
    return find_atom(name, TRUE);
}

ULONG GlobalFindAtom(PSZ name)
{
    return find_atom(name, FALSE);
}

ULONG GlobalDeleteAtom(ULONG atom)
{
    if (atom && atom < 0xC000)
        return 0;
    if (!lock_atoms())
    {
        open32SetError(6);
        return atom;
    }
    BOOL valid = atom >= 0xC000 && atom <= 0xFFFF && table->atoms[atom - 0xC000].references;
    if (valid)
        table->atoms[atom - 0xC000].references--;
    unlock_atoms();
    if (!valid)
        open32SetError(6);
    return valid ? 0 : atom;
}

ULONG GlobalGetAtomName(ULONG atom, PSZ output, LONG capacity)
{
    if (!output || capacity <= 0 || !atom || atom > 0xFFFF)
    {
        open32SetError(87);
        return 0;
    }
    char integer[8];
    const char *name = NULL;
    BOOL locked = FALSE;
    if (atom < 0xC000)
    {
        snprintf(integer, sizeof integer, "#%u", atom);
        name = integer;
    }
    else if ((locked = lock_atoms()) && table->atoms[atom - 0xC000].references)
        name = table->atoms[atom - 0xC000].name;
    size_t copied = 0;
    if (name)
    {
        copied = strlen(name);
        if (copied >= (size_t)capacity)
            copied = (size_t)capacity - 1;
        memcpy(output, name, copied);
    }
    output[copied] = 0;
    if (locked)
        unlock_atoms();
    if (!name)
        open32SetError(6);
    return (ULONG)copied;
}

/* Registered formats share PM's system atom IDs. */
ULONG RegisterClipboardFormat(PSZ name)
{
    if (!name || !*name || strlen(name) > 255)
    {
        open32SetError(87);
        return 0;
    }
    ULONG atom = WinFindAtom(WinQuerySystemAtomTable(), name);
    return atom ? atom : WinAddAtom(WinQuerySystemAtomTable(), name);
}

ULONG RegisterWindowMessage(PSZ name)
{
    return RegisterClipboardFormat(name);
}

/* Process-local atom references are independent of the global table. */
typedef struct LocalAtom
{
    char *name;
    ULONG references;
} LocalAtom;

static LocalAtom local_atoms[0x4000];
static LxHostOnce local_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *local_monitor;

static void local_initialize(void)
{
    local_monitor = lx_host_monitor_create();
    if (!local_monitor)
        abort();
}

static ULONG local_integer_atom(PSZ name)
{
    if ((uintptr_t)name <= 0xFFFF)
        return (ULONG)(uintptr_t)name < 0xC000 ? (ULONG)(uintptr_t)name : 0;
    if (*name != '#')
        return 0;
    char *end;
    unsigned long value = strtoul(name + 1, &end, 10);
    return end != name + 1 && !*end && value > 0 && value < 0xC000 ? (ULONG)value : 0;
}

static ULONG local_find_atom(PSZ name, BOOL add)
{
    ULONG integer = local_integer_atom(name);
    if (integer)
        return integer;
    if ((uintptr_t)name <= 0xFFFF || !*name || strlen(name) > 255 || *name == '#')
    {
        open32SetError(87);
        return 0;
    }
    lx_host_once(&local_once, local_initialize);
    lx_host_monitor_lock(local_monitor);
    ULONG result = 0, vacant = 0x4000;
    for (ULONG i = 0; i < 0x4000; i++)
    {
        if (!local_atoms[i].name)
        {
            if (vacant == 0x4000)
                vacant = i;
        }
        else if (!strcasecmp(local_atoms[i].name, name))
        {
            vacant = 0x4000;
            if (!add || local_atoms[i].references != UINT32_MAX)
            {
                result = 0xC000 + i;
                if (add)
                    local_atoms[i].references++;
            }
            break;
        }
    }
    if (!result && add && vacant < 0x4000)
    {
        local_atoms[vacant].name = strdup(name);
        if (local_atoms[vacant].name)
        {
            local_atoms[vacant].references = 1;
            result = 0xC000 + vacant;
        }
    }
    lx_host_monitor_unlock(local_monitor);
    if (!result)
        open32SetError(add ? 8 : 2);
    return result;
}

ULONG AddAtom(PSZ name)
{
    return local_find_atom(name, TRUE);
}

ULONG FindAtom(PSZ name)
{
    return local_find_atom(name, FALSE);
}

ULONG DeleteAtom(ULONG atom)
{
    if (atom && atom < 0xC000)
        return 0;
    lx_host_once(&local_once, local_initialize);
    lx_host_monitor_lock(local_monitor);
    BOOL valid = atom >= 0xC000 && atom <= 0xFFFF && local_atoms[atom - 0xC000].name;
    if (valid && !--local_atoms[atom - 0xC000].references)
    {
        free(local_atoms[atom - 0xC000].name);
        local_atoms[atom - 0xC000].name = NULL;
    }
    lx_host_monitor_unlock(local_monitor);
    if (!valid)
        open32SetError(6);
    return valid ? 0 : atom;
}
