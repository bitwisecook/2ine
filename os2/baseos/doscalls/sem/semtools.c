/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

NamedSem *findNamedSemLocked(const char *name, int is_mutex)
{
    for (NamedSem *sem = GNamedSems; sem; sem = sem->next)
        if (sem->is_mutex == is_mutex && !strcasecmp(sem->name, name))
            return sem;
    return NULL;
}

APIRET publishSemaphore(void *value, LxHandleKind kind, const char *name, uint32 *output)
{
    void (*destroy)(void *) = kind == LX_HANDLE_MUTEX ? destroyMutexResource : destroyEventResource;
    uint32 public_handle = lx_handle_create(kind, value, destroy);
    if (!public_handle)
    {
        destroy(value);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    APIRET result = NO_ERROR;
    grabLock(&GMutexDosCalls);
    if (name && *name)
    {
        if (findNamedSemLocked(name, kind == LX_HANDLE_MUTEX))
            result = ERROR_DUPLICATE_NAME;
        else
        {
            NamedSem *entry = calloc(1, sizeof *entry);
            if (entry)
                entry->name = strdup(name);
            uint32 internal = entry && entry->name ? lx_handle_create(kind, value, destroy) : 0;
            if (!internal)
            {
                if (entry)
                {
                    free(entry->name);
                    free(entry);
                }
                result = ERROR_NOT_ENOUGH_MEMORY;
            }
            else
            {
                entry->is_mutex = kind == LX_HANDLE_MUTEX;
                entry->handle = value;
                entry->internal_handle = internal;
                entry->next = GNamedSems;
                GNamedSems = entry;
            }
        }
    }
    else if (!name && kind == LX_HANDLE_EVENT)
    {
        uint32 internal = lx_handle_create(kind, value, destroy);
        if (!internal)
            result = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            uint32 old = GLastUnnamedEventHandle;
            GLastUnnamedEventSem = value;
            GLastUnnamedEventHandle = internal;
            if (old)
                lx_handle_close(old, LX_HANDLE_EVENT);
        }
    }
    ungrabLock(&GMutexDosCalls);
    if (result)
        lx_handle_close(public_handle, kind);
    else
        *output = public_handle;
    return result;
}

APIRET openSemaphore(const char *name, LxHandleKind kind, uint32 *output)
{
    if (!output || (kind == LX_HANDLE_MUTEX && !name))
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    void *value = NULL;
    if ((!name || !*name) && kind == LX_HANDLE_EVENT)
        value = GLastUnnamedEventSem;
    else if (name)
    {
        NamedSem *entry = findNamedSemLocked(name, kind == LX_HANDLE_MUTEX);
        if (entry)
            value = entry->handle;
    }
    uint32 handle = value ? lx_handle_create(kind, value,
                                             kind == LX_HANDLE_MUTEX ? destroyMutexResource
                                                                     : destroyEventResource)
                          : 0;
    ungrabLock(&GMutexDosCalls);
    if (!value)
        return ERROR_SEM_NOT_FOUND;
    if (!handle)
        return ERROR_NOT_ENOUGH_MEMORY;
    *output = handle;
    return NO_ERROR;
}

void retireSemaphoreName(void *value, LxHandleKind kind)
{
    if (lx_handle_count(kind, value) != 1)
        return;
    for (NamedSem **link = &GNamedSems; *link; link = &(*link)->next)
    {
        NamedSem *entry = *link;
        if (entry->handle == value && entry->is_mutex == (kind == LX_HANDLE_MUTEX))
        {
            *link = entry->next;
            uint32 internal = entry->internal_handle;
            free(entry->name);
            free(entry);
            lx_handle_close(internal, kind);
            return;
        }
    }
    if (kind == LX_HANDLE_EVENT && GLastUnnamedEventSem == value)
    {
        uint32 internal = GLastUnnamedEventHandle;
        GLastUnnamedEventHandle = 0;
        GLastUnnamedEventSem = NULL;
        lx_handle_close(internal, kind);
    }
}
