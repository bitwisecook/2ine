/* Public domain. No rights reserved. Wholly AI-authored. */
/* USE16 adapters preserve word-sized outputs and selector-based addresses. */
#include "os2/baseos/doscalls/internal.h"
#include "emulator/arguments.h"

APIRET16 Dos16RmDir(PSZ path, ULONG reserved)
{
    return reserved ? ERROR_INVALID_PARAMETER : DosDeleteDir(path);
}

APIRET16 Dos16ForceDelete(PSZ path, ULONG reserved)
{
    return reserved ? ERROR_INVALID_PARAMETER : DosForceDelete(path);
}

APIRET16 Dos16SearchPath(USHORT flags, PSZ path, PSZ name, PBYTE result, USHORT capacity)
{
    return DosSearchPath(flags, path, name, result, capacity);
}

APIRET16 Dos16DevIOCtl2(PVOID data, USHORT data_size, PVOID parameters, USHORT parameter_size,
                        USHORT function, USHORT category, USHORT device)
{
    ULONG ds = data_size, ps = parameter_size;
    return DosDevIOCtl(device == UINT16_MAX ? UINT32_MAX : device, category, function, parameters,
                       ps, &ps, data, ds, &ds);
}

APIRET16 Dos16QFSAttach(PSZ device, USHORT ordinal, USHORT level, PVOID buffer, PUSHORT size,
                        ULONG reserved)
{
    if (reserved || !size)
        return ERROR_INVALID_PARAMETER;
    ULONG length = *size;
    APIRET result = DosQueryFSAttach(device, ordinal, level, buffer, &length);
    *size = length > UINT16_MAX ? UINT16_MAX : (USHORT)length;
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

APIRET16 Dos16SetCp(USHORT codepage, USHORT reserved)
{
    return Dos16SetProcCp(codepage, reserved);
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
    if (encoded <= UINT16_MAX ||
        lx_guest_argument_strings(GProcessState.execution, encoded, 1, 256, 0, &name))
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
                    far_address = ((ULONG)((entry->object->alias << 3) | 7) << 16) | offset;
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
