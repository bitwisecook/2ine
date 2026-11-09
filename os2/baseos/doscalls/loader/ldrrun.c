/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Module handles and names, DosLoadModule and DosQueryAppType. */
#include "os2/baseos/doscalls/internal.h"
#include "emulator/arguments.h"

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
    return strlen(name) == length + sizeof ".DLL" - 1 && !strncasecmp(module->name, name, length) &&
           !strcasecmp(name + length, ".DLL");
}

APIRET DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosQueryModuleHandle('%s', %p)", pszModname, phmod);
    if (!pszModname || !*pszModname || !phmod)
        return ERROR_INVALID_PARAMETER;
    for (const unsigned char *p = (const unsigned char *)pszModname; *p; p++)
        if (*p < ' ' || strchr("\"<>|?*", *p))
            return ERROR_INVALID_NAME;
    grabLock(&GMutexDosCalls);
    LxModule *lxmod;
    for (lxmod = GProcessState.loaded_modules; lxmod; lxmod = lxmod->next)
    {
        if (moduleNameMatches(lxmod, pszModname))
            break;
    }
    ungrabLock(&GMutexDosCalls);

    if (!lxmod && GProcessState.main_module &&
        moduleNameMatches(GProcessState.main_module, pszModname))
        lxmod = GProcessState.main_module;
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
    LxModule *lxmod = GProcessState.loadModuleWithError
                          ? GProcessState.loadModuleWithError(pszModname, &error, pszName, cbName)
                          : GProcessState.loadModule(pszModname);
    if (!lxmod)
        return error ? error : ERROR_BAD_FORMAT;

    *phmod = lxmod ? lxmod->handle : 0;
    return NO_ERROR;
}

APIRET DosQueryModFromEIP(PHMODULE phmod, PULONG pulObject, ULONG cbModName, PCHAR pszModName,
                          PULONG pulOffset, ULONG ulEIP)
{
    TRACE_NATIVE("DosQueryModFromEIP(%p, %p, %u, %p, %p, %08X)", phmod, pulObject, (uint)cbModName,
                 pszModName, pulOffset, (uint)ulEIP);

    for (LxModule *lxmod = GProcessState.loaded_modules; lxmod; lxmod = lxmod->next)
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

/* A 16-bit procedure name is read from at most this many bytes. */
enum
{
    PROCEDURE_NAME_LIMIT = 256
};

/* Executable header fields DosQueryAppType reads (Toolkit newexe.h and exe386.h). */
enum
{
    MZ_HEADER_SIZE = 64,
    MZ_NEW_HEADER_OFFSET = 60,
    NEW_HEADER_SIZE = 64,
    /* Enough of an NE or LX header to hold its signature and module flags. */
    NEW_HEADER_MINIMUM = 24,
    NE_FLAGS_OFFSET = 12,
    NE_EXETYP_OFFSET = 54,
    NE_EXPVER_OFFSET = 62,
    LX_FLAGS_OFFSET = 16
};

enum
{
    NENOTP = 0x8000,
    NEIERR = 0x2000,
    /* The NE application type field matches FAPPTYP's low bits. */
    NE_APPTYPE_SHIFT = 8,
    NE_APPTYPE_BITS = 0x3,
    NE_WINDOWS = 0x2,
    NE_DEV386 = 0x4,
    WINDOWS_VERSION_3_0 = 0x300
};

enum
{
    E32NOLOAD = 0x2000,
    E32APPMASK = 0x0300,
    E32NOPMW = 0x0100,
    E32PMW = 0x0200,
    E32PMAPI = 0x0300,
    E32MODMASK = 0x38000,
    E32MODDLL = 0x08000,
    E32MODPROTDLL = 0x18000,
    E32MODPDEV = 0x20000,
    E32MODVDEV = 0x28000
};

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
    uint8 mz[MZ_HEADER_SIZE], lx[NEW_HEADER_SIZE];
    size_t actual;
    APIRET result = ERROR_INVALID_EXE_SIGNATURE;
    if (lx_host_file_read(file, mz, sizeof mz, &actual) || actual != sizeof mz || mz[0] != 'M' ||
        mz[1] != 'Z')
        goto done;
    /* A plain MZ executable is a DOS program; NE headers also describe Win16. */
    *pFlags = FAPPTYP_DOS;
    result = NO_ERROR;
    const uint32 header = lx_guest_load_u32(mz + MZ_NEW_HEADER_OFFSET);
    if (header < sizeof mz || lx_host_file_seek(file, header, FILE_BEGIN, NULL) ||
        lx_host_file_read(file, lx, sizeof lx, &actual) || actual < NEW_HEADER_MINIMUM)
        goto done;
    if (lx[0] == 'N' && lx[1] == 'E')
    {
        if (actual < sizeof lx)
        {
            result = ERROR_BAD_EXE_FORMAT;
            goto done;
        }
        const USHORT module_flags = lx_guest_load_u16(lx + NE_FLAGS_OFFSET);
        if (module_flags & NEIERR)
        {
            result = ERROR_EXE_MARKED_INVALID;
            goto done;
        }
        ULONG flags = (module_flags >> NE_APPTYPE_SHIFT) & NE_APPTYPE_BITS;
        if (module_flags & NENOTP)
            flags |= FAPPTYP_DLL;
        if (lx[NE_EXETYP_OFFSET] == NE_WINDOWS || lx[NE_EXETYP_OFFSET] == NE_DEV386)
            flags |= lx_guest_load_u16(lx + NE_EXPVER_OFFSET) >= WINDOWS_VERSION_3_0
                         ? FAPPTYP_WINDOWSPROT31
                         : FAPPTYP_WINDOWSPROT;
        *pFlags = flags;
        goto done;
    }
    if (lx[0] != 'L' || (lx[1] != 'X' && lx[1] != 'E'))
        goto done;
    const uint32 module_flags = lx_guest_load_u32(lx + LX_FLAGS_OFFSET);
    if (module_flags & E32NOLOAD)
    {
        result = ERROR_EXE_MARKED_INVALID;
        goto done;
    }
    const uint32 type = module_flags & E32MODMASK;
    ULONG flags = FAPPTYP_32BIT;
    switch (module_flags & E32APPMASK)
    {
        case E32NOPMW:
            flags |= FAPPTYP_NOTWINDOWCOMPAT;
            break;
        case E32PMW:
            flags |= FAPPTYP_WINDOWCOMPAT;
            break;
        case E32PMAPI:
            flags |= FAPPTYP_WINDOWAPI;
            break;
    }
    switch (type)
    {
        case E32MODDLL:
            flags |= FAPPTYP_DLL;
            break;
        case E32MODPDEV:
            flags |= FAPPTYP_PHYSDRV;
            break;
        case E32MODVDEV:
            flags |= FAPPTYP_VIRTDRV;
            break;
        case E32MODPROTDLL:
            flags |= FAPPTYP_PROTDLL;
            break;
    }
    *pFlags = flags;
    result = NO_ERROR;
done:
    lx_host_file_close(file);
    return result;
}

APIRET16 Dos16QAppType(PSZ name, PUSHORT flags)
{
    if (!flags)
        return ERROR_INVALID_PARAMETER;
    ULONG value = 0;
    APIRET result = DosQueryAppType(name, &value);
    if (!result)
        *flags = (USHORT)value;
    return result;
}

APIRET16 Dos16LoadModule(PCHAR failure, USHORT capacity, PSZ name, PUSHORT module)
{
    if (!module)
        return ERROR_INVALID_PARAMETER;
    HMODULE handle = 0;
    APIRET result = DosLoadModule(failure, capacity, name, &handle);
    if (!result)
        *module = (USHORT)handle;
    return result;
}

static LxModule *borrowLegacyModule(USHORT id, LxHandleBorrow *borrow)
{
    LxModule *found = NULL;
    grabLock(&GMutexDosCalls);
    for (LxModule *module = GProcessState.loaded_modules; module; module = module->next)
        if ((USHORT)module->handle == id &&
            lx_handle_borrow(module->handle, LX_HANDLE_MODULE, borrow))
        {
            found = borrow->resource;
            break;
        }
    if (!found && GProcessState.main_module && (USHORT)GProcessState.main_module->handle == id &&
        lx_handle_borrow(GProcessState.main_module->handle, LX_HANDLE_MODULE, borrow))
        found = borrow->resource;
    ungrabLock(&GMutexDosCalls);
    return found;
}

APIRET16 Dos16GetModName(USHORT id, USHORT capacity, PCHAR name)
{
    LxHandleBorrow borrow;
    LxModule *module = borrowLegacyModule(id, &borrow);
    if (!module)
        return ERROR_INVALID_HANDLE;
    APIRET result = name ? DosQueryModuleNameObject(module->handle, capacity, name, module)
                         : ERROR_INVALID_PARAMETER;
    lx_handle_release(&borrow);
    return result;
}

APIRET16 Dos16FreeModule(USHORT id)
{
    LxHandleBorrow borrow;
    LxModule *module = borrowLegacyModule(id, &borrow);
    if (!module)
        return ERROR_INVALID_HANDLE;
    APIRET result = DosFreeModuleObject(module->handle, module);
    lx_handle_release(&borrow);
    return result;
}

APIRET16 Dos16GetProcAddr(USHORT id, ULONG encoded, PULONG address)
{
    if (!address || !encoded)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    LxModule *module = borrowLegacyModule(id, &borrow);
    if (!module)
        return ERROR_INVALID_HANDLE;
    LxGuestArgument name = {0};
    APIRET result = ERROR_INVALID_NAME;
    if (encoded <= UINT16_MAX || lx_guest_argument_strings(GProcessState.execution, encoded, 1,
                                                           PROCEDURE_NAME_LIMIT, 0, &name))
    {
        const LxExport *entry = NULL;
        if (encoded <= UINT16_MAX && GProcessState.findModuleOrdinal)
            entry = GProcessState.findModuleOrdinal(module, encoded);
        else
            for (ULONG i = 0; i < module->num_exports; i++)
            {
                const LxExport *candidate = &module->exports[i];
                if (encoded <= UINT16_MAX ? candidate->ordinal == encoded
                                          : candidate->name && !strcmp(candidate->name, name.host))
                {
                    entry = candidate;
                    break;
                }
            }
        if (entry)
        {
            void *host =
                entry->indirect == LX_EXPORT_INDIRECT ? *(void **)entry->addr : entry->addr;
            ULONG far_address = GProcessState.convert32to1616(host);
            if (!far_address && entry->object && entry->object->alias != UINT16_MAX)
            {
                uintptr_t offset = (uintptr_t)host - (uintptr_t)entry->object->addr;
                if (offset <= UINT16_MAX)
                    far_address =
                        ((ULONG)ldtSelector(entry->object->alias) << SELECTOR_OFFSET_BITS) | offset;
            }
            result = far_address ? NO_ERROR : ERROR_INVALID_ADDRESS;
            if (!result)
                *address = far_address;
        }
        if (result == ERROR_INVALID_NAME && encoded <= UINT16_MAX)
            result = ERROR_INVALID_ORDINAL;
    }
    lx_guest_argument_finish(&name, 0);
    lx_handle_release(&borrow);
    return result;
}
