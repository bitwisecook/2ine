/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 atom references share the worker's case-insensitive string table. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
#include <stdio.h>
typedef struct Atom
{
    char *name;
    ULONG references;
} Atom;
static Atom atoms[0x4000];
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
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
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    ULONG result = 0, vacant = 0x4000;
    for (ULONG i = 0; i < 0x4000; i++)
    {
        if (!atoms[i].name)
        {
            if (vacant == 0x4000)
                vacant = i;
        }
        else if (!strcasecmp(atoms[i].name, name))
        {
            vacant = 0x4000;
            if (!add || atoms[i].references != UINT32_MAX)
            {
                result = 0xC000 + i;
                if (add)
                    atoms[i].references++;
            }
            break;
        }
    }
    if (!result && add && vacant < 0x4000)
    {
        atoms[vacant].name = strdup(name);
        if (atoms[vacant].name)
        {
            atoms[vacant].references = 1;
            result = 0xC000 + vacant;
        }
    }
    lx_host_monitor_unlock(monitor);
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
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    BOOL valid = atom >= 0xC000 && atom <= 0xFFFF && atoms[atom - 0xC000].name;
    if (valid && !--atoms[atom - 0xC000].references)
    {
        free(atoms[atom - 0xC000].name);
        atoms[atom - 0xC000].name = NULL;
    }
    lx_host_monitor_unlock(monitor);
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
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    const char *name;
    if (atom < 0xC000)
    {
        snprintf(integer, sizeof integer, "#%u", atom);
        name = integer;
    }
    else
        name = atoms[atom - 0xC000].name;
    size_t copied = 0;
    if (name)
    {
        copied = strlen(name);
        if (copied >= (size_t)capacity)
            copied = (size_t)capacity - 1;
        memcpy(output, name, copied);
    }
    output[copied] = 0;
    lx_host_monitor_unlock(monitor);
    if (!name)
        open32SetError(6);
    return (ULONG)copied;
}
