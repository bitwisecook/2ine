/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 library handles share the loader's checked OS/2 module registry. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
static HMODULE module_handle(HMODULE module)
{
    return (!module || module == UINT32_MAX) && GLoaderState.main_module
               ? GLoaderState.main_module->handle
               : module;
}
HMODULE GetModuleHandle(PSZ name)
{
    if (!name)
        return module_handle(0);
    const char *base = name;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    char module[256];
    size_t length = strlen(base);
    if (length >= sizeof module)
    {
        open32SetError(126);
        return 0;
    }
    memcpy(module, base, length + 1);
    char *extension = strrchr(module, '.');
    if (extension)
        *extension = 0;
    for (LxModule *loaded = GLoaderState.loaded_modules; loaded; loaded = loaded->next)
        if (!loaded->load_failed && !strcasecmp(module, loaded->name))
            return loaded->handle;
    open32SetError(126);
    return 0;
}

ULONG GetModuleFileName(HMODULE module, PSZ output, ULONG capacity)
{
    if (!output || !capacity)
    {
        open32SetError(87);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(module_handle(module), LX_HANDLE_MODULE, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    LxModule *loaded = borrow.resource;
    size_t length = strlen(loaded->os2path);
    size_t copied = length < capacity ? length : capacity - 1;
    memcpy(output, loaded->os2path, copied);
    output[copied] = 0;
    lx_handle_release(&borrow);
    if (length >= capacity)
    {
        open32SetError(122);
        return capacity;
    }
    return (ULONG)length;
}
HMODULE LoadLibrary(PSZ name)
{
    TRACE_NATIVE("LoadLibrary('%s')", name);
    LxModule *module = name && GLoaderState.loadModule ? GLoaderState.loadModule(name) : NULL;
    if (!module)
        open32SetError(name ? 126 : 87);
    return module ? module->handle : 0;
}
BOOL FreeLibrary(HMODULE module)
{
    LxHandleBorrow borrow = {0};
    if (!GLoaderState.freeModule || !lx_handle_borrow(module, LX_HANDLE_MODULE, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    GLoaderState.freeModule(borrow.resource);
    lx_handle_release(&borrow);
    return TRUE;
}

PFN GetProcAddress(HMODULE module, PSZ name)
{
    if (!name)
    {
        open32SetError(87);
        return NULL;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(module_handle(module), LX_HANDLE_MODULE, &borrow))
    {
        open32SetError(6);
        return NULL;
    }
    const LxModule *loaded = borrow.resource;
    ULONG ordinal = (uintptr_t)name <= UINT16_MAX ? (ULONG)(uintptr_t)name : 0;
    PFN address = NULL;
    for (uint32 i = 0; i < loaded->num_exports; i++)
    {
        const LxExport *entry = &loaded->exports[i];
        if (ordinal ? entry->ordinal == ordinal : entry->name && !strcmp(entry->name, name))
        {
            address = (PFN)(entry->indirect ? *(void **)entry->addr : entry->addr);
            break;
        }
    }
    lx_handle_release(&borrow);
    if (!address)
        open32SetError(127);
    return address;
}

void *open32DosService(const char *name)
{
    for (LxModule *m = GLoaderState.loaded_modules; m; m = m->next)
        if (m->nativelib && !strcasecmp(m->name, "DOSCALLS"))
            return lx_host_library_symbol(m->nativelib, name);
    return NULL;
}
