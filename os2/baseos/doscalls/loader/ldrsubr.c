/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Procedure addresses, module freeing and header queries. */
#include "os2/baseos/doscalls/internal.h"

enum
{
    LAST_EXPORT_ORDINAL = 65533
};

APIRET DosQueryProcAddrObject(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN *ppfn,
                              const LxModule *lxmod)
{
    TRACE_NATIVE("DosQueryProcAddr(%u, %u, '%s', %p)", (uint)hmod, (uint)ordinal, pszName, ppfn);

    const LxExport *exports = lxmod->exports;
    const uint32 num_exports = lxmod->num_exports;

    /* Ordinals 65534 and 65535 are never valid exports. */
    if (ordinal > LAST_EXPORT_ORDINAL)
    {
        TRACE_NATIVE("unavailable export %s#%u", lxmod->name, (uint)ordinal);
        return ERROR_INVALID_ORDINAL;
    }
    else if (ordinal != 0)
    {
        for (uint32 i = 0; i < num_exports; i++, exports++)
        {
            if (exports->ordinal == ordinal)
            {
                if (ppfn)
                    *ppfn = (PFN)(exports->indirect == LX_EXPORT_INDIRECT ? *(void **)exports->addr
                                                                          : exports->addr);
                return NO_ERROR;
            }
        }
        TRACE_NATIVE("unavailable export %s#%u", lxmod->name, (uint)ordinal);
        return ERROR_INVALID_ORDINAL;
    }
    else
    {

        for (uint32 i = 0; i < num_exports; i++, exports++)
        {
            if (exports->name && (strcmp(exports->name, pszName) == 0))
            {
                if (ppfn)
                    *ppfn = (PFN)(exports->indirect == LX_EXPORT_INDIRECT ? *(void **)exports->addr
                                                                          : exports->addr);
                return NO_ERROR;
            }
        }
        return ERROR_INVALID_NAME;
    }

    __builtin_unreachable();
}

APIRET DosQueryProcAddr(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN *ppfn)
{
    LxHandleBorrow module;
    if (!lx_handle_borrow(hmod, LX_HANDLE_MODULE, &module))
        return ERROR_INVALID_HANDLE;
    const APIRET result = DosQueryProcAddrObject(hmod, ordinal, pszName, ppfn, module.resource);
    lx_handle_release(&module);
    return result;
}

APIRET DosFreeModuleObject(HMODULE hmod, LxModule *lxmod)
{
    TRACE_NATIVE("DosFreeModule(%u)", (uint)hmod);

    if ((!hmod) || (!GProcessState.freeModule))
        return ERROR_INVALID_HANDLE;

    LxModule *loaded;

    /* Validate the opaque handle before passing it back to the loader. */
    grabLock(&GMutexDosCalls);
    for (loaded = GProcessState.loaded_modules; loaded; loaded = loaded->next)
    {
        if (loaded == lxmod)
            break;
    }
    ungrabLock(&GMutexDosCalls);

    if (!loaded)
        return ERROR_INVALID_HANDLE;

    GProcessState.freeModule(lxmod);
    return NO_ERROR;
}

APIRET DosFreeModule(HMODULE hmod)
{
    LxHandleBorrow module;
    if (!lx_handle_borrow(hmod, LX_HANDLE_MODULE, &module))
        return ERROR_INVALID_HANDLE;
    const APIRET result = DosFreeModuleObject(hmod, module.resource);
    lx_handle_release(&module);
    return result;
}

APIRET DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer, ULONG cbBuffer,
                          ULONG ulSubFunction)
{
    TRACE_NATIVE("DosQueryHeaderInfo(%u, %u, %p, %u, %u)", (uint)hmod, (uint)ulIndex, pvBuffer,
                 (uint)cbBuffer, (uint)ulSubFunction);

    switch (ulSubFunction)
    {
            //case QHINF_EXEINFO:
            //case QHINF_READRSRCTBL:
            //case QHINF_READFILE:

        case QHINF_LIBPATHLENGTH:
            if (cbBuffer < sizeof(ULONG))
                return ERROR_BUFFER_OVERFLOW;
            *((ULONG *)pvBuffer) = GProcessState.libpathlen;
            return NO_ERROR;

        case QHINF_LIBPATH:
            if (cbBuffer < GProcessState.libpathlen)
                return ERROR_BUFFER_OVERFLOW;
            strcpy((char *)pvBuffer, GProcessState.libpath);
            return NO_ERROR;

            //case QHINF_FIXENTRY:
            //case QHINF_STE:
            //case QHINF_MAPSEL:

        default:
            FIXME("I don't know what this query wants");
            break;
    }

    return ERROR_INVALID_PARAMETER;
}

APIRET DosQueryProcType(HMODULE handle, ULONG ordinal, PSZ name, PULONG type)
{
    if (!type || (!ordinal && !name))
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_MODULE, &borrow))
        return ERROR_INVALID_HANDLE;
    const LxModule *module = borrow.resource;
    const LxExport *entry = NULL;
    for (ULONG i = 0; i < module->num_exports; i++)
        if (ordinal ? module->exports[i].ordinal == ordinal
                    : module->exports[i].name && !strcmp(module->exports[i].name, name))
        {
            entry = &module->exports[i];
            break;
        }
    APIRET result = entry ? NO_ERROR : ordinal ? ERROR_INVALID_ORDINAL : ERROR_INVALID_NAME;
    if (entry)
    {
        *type = 1;
        if (!module->nativelib && !module->is_lx)
            *type = 0;
        else if (entry->object && entry->object->bits)
            *type = entry->object->bits == 32;
        else if (entry->object && entry->object->alias != UINT16_MAX)
            *type = 0;
    }
    lx_handle_release(&borrow);
    return result;
}
