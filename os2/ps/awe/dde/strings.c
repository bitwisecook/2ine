/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE string handles: atoms with per-instance reference counts, and the name service. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include <stdio.h>
#include "os2/ps/awe/dde/dde.h"

static DdeString **find_string(DdeInstance *instance, ULONG atom)
{
    DdeString **link = &instance->strings;
    while (*link && (*link)->atom != atom)
        link = &(*link)->next;
    return link;
}

ULONG DdeCreateStringHandle(ULONG id, PSZ text, LONG codepage)
{
    TRACE_NATIVE("DdeCreateStringHandle(%u, '%s', %d)", (uint)id, text ? text : "", (int)codepage);
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    ULONG atom = 0;
    if (!text || strlen(text) > 255 || (codepage && codepage != 1004))
        instance->error = 0x4006;
    else if (*text)
    {
        atom = GlobalAddAtom(text);
        DdeString **link = find_string(instance, atom);
        DdeString *entry = *link;
        if (atom && !entry)
        {
            entry = calloc(1, sizeof *entry);
            if (entry)
            {
                entry->atom = atom;
                *link = entry;
            }
        }
        if (!atom || !entry || entry->references == UINT32_MAX)
        {
            if (atom)
                GlobalDeleteAtom(atom);
            atom = 0;
            instance->error = 0x4007;
        }
        else
            entry->references++;
    }
    lx_handle_release(&borrow);
    return atom;
}

BOOL DdeFreeStringHandle(ULONG id, ULONG atom)
{
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return FALSE;
    DdeInstance *instance = borrow.resource;
    DdeString **link = find_string(instance, atom);
    BOOL result = *link && (*link)->references;
    if (!result)
        instance->error = 0x4006;
    else
    {
        DdeString *entry = *link;
        GlobalDeleteAtom(entry->atom);
        if (!--entry->references && !entry->service)
        {
            *link = entry->next;
            free(entry);
        }
    }
    lx_handle_release(&borrow);
    return result;
}

BOOL DdeKeepStringHandle(ULONG id, ULONG atom)
{
    char name[256];
    if (!GlobalGetAtomName(atom, name, sizeof name))
    {
        LxHandleBorrow borrow;
        if (dde_borrow_instance(id, &borrow))
        {
            ((DdeInstance *)borrow.resource)->error = 0x4006;
            lx_handle_release(&borrow);
        }
        return FALSE;
    }
    return DdeCreateStringHandle(id, name, 1004) == atom;
}

ULONG DdeQueryString(ULONG id, ULONG atom, PSZ output, ULONG capacity, LONG codepage)
{
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    char name[256];
    ULONG length = GlobalGetAtomName(atom, name, sizeof name), result = 0;
    if (!length || (codepage && codepage != 1004))
        instance->error = 0x4006;
    else if (!output)
        result = length;
    else if (capacity)
    {
        result = length < capacity ? length : capacity - 1;
        memcpy(output, name, result);
        output[result] = 0;
    }
    lx_handle_release(&borrow);
    return result;
}

LONG DdeCmpStringHandles(ULONG first, ULONG second)
{
    if (first == second)
        return 0;
    char left[256], right[256];
    if (!GlobalGetAtomName(first, left, sizeof left))
        return -1;
    if (!GlobalGetAtomName(second, right, sizeof right))
        return 1;
    int result = strcasecmp(left, right);
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

ULONG DdeNameService(ULONG id, ULONG atom, ULONG reserved, ULONG command)
{
    TRACE_NATIVE("DdeNameService(%u, %u, %u, %u)", (uint)id, (uint)atom, (uint)reserved,
                 (uint)command);
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    ULONG result = 0;
    DdeString **link = find_string(instance, atom);
    if (reserved || (command != 1 && command != 2 && command != 4 && command != 8) ||
        (command == 1 && (!atom || !*link)) || (atom && !*link))
        instance->error = 0x4006;
    else if (command == 4 || command == 8)
    {
        instance->filter = command == 4;
        if (command == 8) /* DNS_FILTEROFF: this server answers for any service. */
            dde_publish_service(instance->handle, 0);
        else
            dde_withdraw_service(instance->handle, 0, FALSE);
        result = 1;
    }
    else if (command == 1 && (instance->flags & 0x10))
        instance->error = 0x4004; /* Client-only dde_instances cannot register a service. */
    else
    {
        if (command == 1)
        {
            DdeString *entry = *link;
            char name[256];
            if (entry->service)
                result = 1;
            else if (GlobalGetAtomName(atom, name, sizeof name) && GlobalAddAtom(name) == atom)
            {
                entry->service = TRUE;
                result = 1;
            }
            else
                instance->error = 0x4007;
        }
        else
        {
            for (DdeString **item = &instance->strings; *item;)
            {
                DdeString *entry = *item;
                if (entry->service && (!atom || atom == entry->atom))
                {
                    entry->service = FALSE;
                    GlobalDeleteAtom(entry->atom);
                    if (!entry->references)
                    {
                        *item = entry->next;
                        free(entry);
                        continue;
                    }
                }
                item = &entry->next;
            }
            result = 1;
        }
        if (result && command == 1)
            dde_publish_service(instance->handle, atom);
        else if (result)
            dde_withdraw_service(instance->handle, atom, !atom);
        if (result)
            dde_notify_peers(instance, command == 1 ? 0x10A2 : 0x10D2,
                             command == 1 ? 0x80000 : 0x100000, atom);
    }
    lx_handle_release(&borrow);
    return result;
}
