/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
APIRET DosQueryModuleNameObject(HMODULE hmod, ULONG buflen, PCHAR buf, const LxModule *lxmod)
{
    TRACE_NATIVE("DosQueryModuleName(%u, %u, %p)", (uint)hmod, (uint)buflen, buf);

    if (strlen(lxmod->os2path) >= buflen)
        return ERROR_BAD_LENGTH;
    strcpy(buf, lxmod->os2path);
    return NO_ERROR;
}

APIRET DosQueryModuleName(HMODULE hmod, ULONG buflen, PCHAR buf)
{
    LxHandleBorrow module;
    if (!lx_handle_borrow(hmod, LX_HANDLE_MODULE, &module))
        return ERROR_INVALID_HANDLE;
    const APIRET result = DosQueryModuleNameObject(hmod, buflen, buf, module.resource);
    lx_handle_release(&module);
    return result;
}

static int moduleNameMatches(const LxModule *module, const char *name)
{
    if (!strcasecmp(module->name, name) || (module->os2path && !strcasecmp(module->os2path, name)))
        return 1;
    if (strpbrk(name, "\\/:"))
        return 0;
    const char *base = module->os2path;
    if (base)
    {
        for (const char *p = base; *p; p++)
            if (*p == '\\' || *p == '/')
                base = p + 1;
        if (!strcasecmp(base, name))
            return 1;
    }
    size_t length = strlen(module->name);
    return strlen(name) == length + 4 && !strncasecmp(module->name, name, length) &&
           !strcasecmp(name + length, ".DLL");
}

APIRET DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosQueryModuleHandle('%s', %p)", pszModname, phmod);
    if (!pszModname || !*pszModname || !phmod)
        return ERROR_INVALID_PARAMETER;
    for (const unsigned char *p = (const unsigned char *)pszModname; *p; p++)
        if (*p < 32 || strchr("\"<>|?*", *p))
            return ERROR_INVALID_NAME;
    grabLock(&GMutexDosCalls);
    LxModule *lxmod;
    for (lxmod = GLoaderState.loaded_modules; lxmod; lxmod = lxmod->next)
    {
        if (moduleNameMatches(lxmod, pszModname))
            break;
    }
    ungrabLock(&GMutexDosCalls);

    if (!lxmod && GLoaderState.main_module &&
        moduleNameMatches(GLoaderState.main_module, pszModname))
        lxmod = GLoaderState.main_module;
    if (phmod)
        *phmod = lxmod ? lxmod->handle : 0;

    return lxmod ? NO_ERROR : ERROR_MOD_NOT_FOUND;
}

APIRET16 Dos16GetModHandle(PSZ name, PUSHORT module)
{
    if (!module)
        return ERROR_INVALID_PARAMETER;
    HMODULE handle = 0;
    APIRET result = DosQueryModuleHandle(name, &handle);
    /* Legacy module IDs use the registry's nonzero 16-bit slot index. */
    if (!result)
        *module = (USHORT)handle;
    return result;
}

APIRET DosQueryProcAddrObject(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN *ppfn,
                              const LxModule *lxmod)
{
    TRACE_NATIVE("DosQueryProcAddr(%u, %u, '%s', %p)", (uint)hmod, (uint)ordinal, pszName, ppfn);

    const LxExport *exports = lxmod->exports;
    const uint32 num_exports = lxmod->num_exports;

    if (ordinal > 65533)
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

APIRET DosLoadModule(PSZ pszName, ULONG cbName, PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosLoadModule(%p, %u, '%s', %p)", pszName, (uint)cbName, pszModname, phmod);

    if (!pszModname || !*pszModname || !phmod || (cbName && !pszName))
        return ERROR_INVALID_PARAMETER;
    if (cbName)
        *pszName = 0;

    FIXME("there's no mutex on this global state at the moment!");
    *phmod = 0;
    uint32 error = ERROR_BAD_FORMAT;
    LxModule *lxmod = GLoaderState.loadModuleWithError
                          ? GLoaderState.loadModuleWithError(pszModname, &error, pszName, cbName)
                          : GLoaderState.loadModule(pszModname);
    if (!lxmod)
        return error ? error : ERROR_BAD_FORMAT;

    *phmod = lxmod ? lxmod->handle : 0;
    return NO_ERROR;
}

APIRET DosFreeModuleObject(HMODULE hmod, LxModule *lxmod)
{
    TRACE_NATIVE("DosFreeModule(%u)", (uint)hmod);

    if ((!hmod) || (!GLoaderState.freeModule))
        return ERROR_INVALID_HANDLE;

    LxModule *loaded;

    /* Validate the opaque handle before passing it back to the loader. */
    grabLock(&GMutexDosCalls);
    for (loaded = GLoaderState.loaded_modules; loaded; loaded = loaded->next)
    {
        if (loaded == lxmod)
            break;
    }
    ungrabLock(&GMutexDosCalls);

    if (!loaded)
        return ERROR_INVALID_HANDLE;

    GLoaderState.freeModule(lxmod);
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

APIRET DosQueryModFromEIP(PHMODULE phmod, PULONG pulObject, ULONG cbModName, PCHAR pszModName,
                          PULONG pulOffset, ULONG ulEIP)
{
    TRACE_NATIVE("DosQueryModFromEIP(%p, %p, %u, %p, %p, %08X)", phmod, pulObject, (uint)cbModName,
                 pszModName, pulOffset, (uint)ulEIP);

    for (LxModule *lxmod = GLoaderState.loaded_modules; lxmod; lxmod = lxmod->next)
    {
        for (uint32 i = 0; i < lxmod->num_mmaps; i++)
        {
            const uint32 base = lxmod->mmaps[i].mapping
                                    ? lx_execution_mapping_address(lxmod->mmaps[i].mapping)
                                    : (uint32)(size_t)lxmod->mmaps[i].addr;
            const uint64 end = (uint64)base + lxmod->mmaps[i].size;
            if ((ulEIP >= base) && (ulEIP < end))
            {
                if (phmod)
                    *phmod = lxmod ? lxmod->handle : 0;
                if (pulOffset)
                    *pulOffset = ulEIP - base;
                if (pulObject)
                    *pulObject = i + 1;
                if (pszModName)
                {
                    const size_t len = strlen(lxmod->name);
                    if (len + 1 > cbModName)
                        return ERROR_BUFFER_OVERFLOW;
                    strcpy(pszModName, lxmod->name);
                }
                return NO_ERROR;
            }
        }
    }

    return ERROR_FILE_NOT_FOUND;
}

APIRET DosQueryAppType(PSZ pszName, PULONG pFlags)
{
    TRACE_NATIVE("DosQueryAppType('%s', %p)", pszName, pFlags);

    if (!pFlags)
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *path = makeUnixPath(pszName, &err);
    if (!path)
        return err;

    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(path, LX_HOST_FILE_READ, 0, &error);
    free(path);
    if (!file)
        return fileError(error);
    uint8 mz[64], lx[64];
    size_t actual;
    APIRET result = ERROR_INVALID_EXE_SIGNATURE;
    if (lx_host_file_read(file, mz, sizeof mz, &actual) || actual != sizeof mz || mz[0] != 'M' ||
        mz[1] != 'Z')
        goto done;
    /* A plain MZ executable is a DOS program; NE headers also describe Win16. */
    *pFlags = FAPPTYP_DOS;
    result = NO_ERROR;
    const uint32 header = lx_guest_load_u32(mz + 60);
    if (header < sizeof mz || lx_host_file_seek(file, header, 0, NULL) ||
        lx_host_file_read(file, lx, sizeof lx, &actual) || actual < 24)
        goto done;
    if (lx[0] == 'N' && lx[1] == 'E')
    {
        if (actual < sizeof lx)
        {
            result = ERROR_BAD_EXE_FORMAT;
            goto done;
        }
        const USHORT module_flags = lx_guest_load_u16(lx + 12);
        if (module_flags & 0x2000)
        {
            result = ERROR_EXE_MARKED_INVALID;
            goto done;
        }
        ULONG flags = (module_flags >> 8) & 3;
        if (module_flags & 0x8000)
            flags |= FAPPTYP_DLL;
        if (lx[54] == 2 || lx[54] == 4)
            flags |=
                lx_guest_load_u16(lx + 62) >= 0x300 ? FAPPTYP_WINDOWSPROT31 : FAPPTYP_WINDOWSPROT;
        *pFlags = flags;
        goto done;
    }
    if (lx[0] != 'L' || (lx[1] != 'X' && lx[1] != 'E'))
        goto done;
    const uint32 module_flags = lx_guest_load_u32(lx + 16);
    if (module_flags & 0x2000)
    {
        result = ERROR_EXE_MARKED_INVALID;
        goto done;
    }
    const uint32 type = module_flags & 0x38000;
    ULONG flags = FAPPTYP_32BIT;
    switch (module_flags & 0x300)
    {
        case 0x100:
            flags |= FAPPTYP_NOTWINDOWCOMPAT;
            break;
        case 0x200:
            flags |= FAPPTYP_WINDOWCOMPAT;
            break;
        case 0x300:
            flags |= FAPPTYP_WINDOWAPI;
            break;
    }
    switch (type)
    {
        case 0x8000:
            flags |= FAPPTYP_DLL;
            break;
        case 0x20000:
            flags |= FAPPTYP_PHYSDRV;
            break;
        case 0x28000:
            flags |= FAPPTYP_VIRTDRV;
            break;
        case 0x18000:
            flags |= FAPPTYP_PROTDLL;
            break;
    }
    *pFlags = flags;
    result = NO_ERROR;
done:
    lx_host_file_close(file);
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
            *((ULONG *)pvBuffer) = GLoaderState.libpathlen;
            return NO_ERROR;

        case QHINF_LIBPATH:
            if (cbBuffer < GLoaderState.libpathlen)
                return ERROR_BUFFER_OVERFLOW;
            strcpy((char *)pvBuffer, GLoaderState.libpath);
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

APIRET DosQueryExtLIBPATH(PSZ path, ULONG flags)
{
    if (!path || flags < BEGIN_LIBPATH || flags > LIBPATHSTRICT)
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    if (flags == LIBPATHSTRICT)
        strcpy(path, GLoaderState.libpath_strict ? "T" : "F");
    else
        strcpy(path,
               flags == BEGIN_LIBPATH ? GLoaderState.begin_libpath : GLoaderState.end_libpath);
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}
APIRET DosSetExtLIBPATH(PSZ path, ULONG flags)
{
    if (flags < BEGIN_LIBPATH || flags > LIBPATHSTRICT || (path && strlen(path) >= 1024))
        return ERROR_INVALID_PARAMETER;
    if (flags == LIBPATHSTRICT && path && *path && strcasecmp(path, "T") && strcasecmp(path, "F"))
        return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    if (flags == LIBPATHSTRICT)
        GLoaderState.libpath_strict = path && !strcasecmp(path, "T");
    else
        strcpy(flags == BEGIN_LIBPATH ? GLoaderState.begin_libpath : GLoaderState.end_libpath,
               path ? path : "");
    ungrabLock(&GMutexDosCalls);
    return NO_ERROR;
}
APIRET16 Dos16QueryExtLIBPATH(PCHAR path, ULONG flags)
{
    return DosQueryExtLIBPATH(path, flags);
}
APIRET16 Dos16SetExtLIBPATH(PSZ path, ULONG flags)
{
    return DosSetExtLIBPATH(path, flags);
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
