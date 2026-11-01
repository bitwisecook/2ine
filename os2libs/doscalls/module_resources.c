/* Public domain. No rights reserved. Wholly AI-authored. */
/* Resource copies retain their own mapping until the last matching release. */
#include "os2libs/doscalls/internal.h"
typedef struct ModuleResource
{
    HMODULE module;
    ULONG type, name, references;
    LxGuestMapping *mapping;
    struct ModuleResource *next;
} ModuleResource;
static ModuleResource *resources;

static const LxResourceTableEntry *find_resource(HMODULE handle, ULONG type, ULONG name,
                                                 LxHandleBorrow *borrow, APIRET *error)
{
    *error = ERROR_INVALID_PARAMETER;
    if (type > UINT16_MAX || name > UINT16_MAX)
        return NULL;
    if (!handle && GLoaderState.main_module)
        handle = GLoaderState.main_module->handle;
    if (!lx_handle_borrow(handle, LX_HANDLE_MODULE, borrow))
    {
        *error = ERROR_INVALID_HANDLE;
        return NULL;
    }
    LxModule *m = borrow->resource;
    for (uint32 i = 0; i < m->num_resources; i++)
        if (m->resources[i].type_id == type && m->resources[i].name_id == name)
        {
            *error = NO_ERROR;
            return &m->resources[i];
        }
    return NULL;
}
APIRET DosQueryResourceSize(HMODULE module, ULONG type, ULONG name, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow = {0};
    APIRET result;
    const LxResourceTableEntry *r = find_resource(module, type, name, &borrow, &result);
    if (r)
        *size = r->resource_size;
    lx_handle_release(&borrow);
    return result;
}
APIRET Dos32QueryResourceSize(HMODULE module, ULONG type, ULONG name, PULONG size)
{
    return DosQueryResourceSize(module, type, name, size);
}
APIRET DosGetResource(HMODULE module, ULONG type, ULONG name, PPVOID output)
{
    TRACE_NATIVE("DosGetResource(%u, %u, %u, %p)", (uint)module, (uint)type, (uint)name, output);
    if (!output)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow = {0};
    APIRET result;
    const LxResourceTableEntry *r = find_resource(module, type, name, &borrow, &result);
    if (!r)
    {
        lx_handle_release(&borrow);
        return result;
    }
    if ((uint64_t)r->resource_size + 4095 > SIZE_MAX)
    {
        lx_handle_release(&borrow);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    LxModule *m = borrow.resource;
    grabLock(&GMutexDosCalls);
    ModuleResource *block;
    for (block = resources; block; block = block->next)
        if (block->module == m->handle && block->type == type && block->name == name)
            break;
    if (!block)
    {
        block = calloc(1, sizeof *block);
        if (block)
        {
            size_t bytes = ((size_t)r->resource_size + 4095) & ~(size_t)4095;
            block->mapping = lx_execution_map(GLoaderState.execution, 0, bytes ? bytes : 4096,
                                              LX_GUEST_READ | LX_GUEST_WRITE, 0);
            if (!block->mapping ||
                !lx_execution_read(GLoaderState.execution,
                                   m->mmaps[r->object - 1].guest_base + r->offset,
                                   lx_execution_mapping_host(block->mapping), r->resource_size) ||
                !lx_execution_protect(GLoaderState.execution, block->mapping, LX_GUEST_READ))
            {
                if (block->mapping)
                    lx_execution_unmap(GLoaderState.execution, block->mapping);
                free(block);
                block = NULL;
            }
            else
            {
                block->module = m->handle;
                block->type = type;
                block->name = name;
                block->next = resources;
                resources = block;
            }
        }
    }
    result = block ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
    if (block)
    {
        block->references++;
        *output = lx_execution_mapping_host(block->mapping);
    }
    ungrabLock(&GMutexDosCalls);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosFreeResource(PVOID pointer)
{
    grabLock(&GMutexDosCalls);
    ModuleResource **slot = &resources;
    while (*slot && lx_execution_mapping_host((*slot)->mapping) != pointer)
        slot = &(*slot)->next;
    APIRET result = ERROR_INVALID_PARAMETER;
    if (*slot)
    {
        ModuleResource *block = *slot;
        result = NO_ERROR;
        if (--block->references == 0)
        {
            if (!lx_execution_unmap(GLoaderState.execution, block->mapping))
            {
                block->references++;
                result = ERROR_ACCESS_DENIED;
            }
            else
            {
                *slot = block->next;
                free(block);
            }
        }
    }
    ungrabLock(&GMutexDosCalls);
    return result;
}

void destroyModuleResources(void)
{
    while (resources)
    {
        ModuleResource *block = resources;
        resources = block->next;
        lx_execution_unmap(GLoaderState.execution, block->mapping);
        free(block);
    }
}
