/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE string handles: atoms with per-instance reference counts, and the name service. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include <stdio.h>
#include "os2/ps/awe/dde/dde.h"

enum
{
    /* Longest DDE string handle text: an atom name. */
    DDE_STRING_LIMIT = 255
};

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
    if (!text || strlen(text) > DDE_STRING_LIMIT || (codepage && codepage != WIN32_CP_WINANSI))
        instance->error = WIN32_DMLERR_INVALIDPARAMETER;
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
            instance->error = WIN32_DMLERR_LOW_MEMORY;
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
        instance->error = WIN32_DMLERR_INVALIDPARAMETER;
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
    char name[DDE_STRING_LIMIT + 1];
    if (!GlobalGetAtomName(atom, name, sizeof name))
    {
        LxHandleBorrow borrow;
        if (dde_borrow_instance(id, &borrow))
        {
            ((DdeInstance *)borrow.resource)->error = WIN32_DMLERR_INVALIDPARAMETER;
            lx_handle_release(&borrow);
        }
        return FALSE;
    }
    return DdeCreateStringHandle(id, name, WIN32_CP_WINANSI) == atom;
}

ULONG DdeQueryString(ULONG id, ULONG atom, PSZ output, ULONG capacity, LONG codepage)
{
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    char name[DDE_STRING_LIMIT + 1];
    ULONG length = GlobalGetAtomName(atom, name, sizeof name), result = 0;
    if (!length || (codepage && codepage != WIN32_CP_WINANSI))
        instance->error = WIN32_DMLERR_INVALIDPARAMETER;
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
    char left[DDE_STRING_LIMIT + 1], right[DDE_STRING_LIMIT + 1];
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
    if (reserved ||
        (command != WIN32_DNS_REGISTER && command != WIN32_DNS_UNREGISTER &&
         command != WIN32_DNS_FILTERON && command != WIN32_DNS_FILTEROFF) ||
        (command == WIN32_DNS_REGISTER && (!atom || !*link)) || (atom && !*link))
        instance->error = WIN32_DMLERR_INVALIDPARAMETER;
    else if (command == WIN32_DNS_FILTERON || command == WIN32_DNS_FILTEROFF)
    {
        instance->filter = command == WIN32_DNS_FILTERON;
        if (command == WIN32_DNS_FILTEROFF) /* This server answers for any service. */
            dde_publish_service(instance->handle, 0);
        else
            dde_withdraw_service(instance->handle, 0, FALSE);
        result = 1;
    }
    else if (command == WIN32_DNS_REGISTER && (instance->flags & WIN32_APPCMD_CLIENTONLY))
        instance->error =
            WIN32_DMLERR_DLL_USAGE; /* Client-only dde_instances cannot register a service. */
    else
    {
        if (command == WIN32_DNS_REGISTER)
        {
            DdeString *entry = *link;
            char name[DDE_STRING_LIMIT + 1];
            if (entry->service)
                result = 1;
            else if (GlobalGetAtomName(atom, name, sizeof name) && GlobalAddAtom(name) == atom)
            {
                entry->service = TRUE;
                result = 1;
            }
            else
                instance->error = WIN32_DMLERR_LOW_MEMORY;
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
        if (result && command == WIN32_DNS_REGISTER)
            dde_publish_service(instance->handle, atom);
        else if (result)
            dde_withdraw_service(instance->handle, atom, !atom);
        if (result)
            dde_notify_peers(instance, command == WIN32_DNS_REGISTER ? 0x10A2 : 0x10D2,
                             command == WIN32_DNS_REGISTER ? WIN32_CBF_SKIP_REGISTRATIONS
                                                           : WIN32_CBF_SKIP_UNREGISTRATIONS,
                             atom);
    }
    lx_handle_release(&borrow);
    return result;
}
