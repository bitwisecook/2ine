/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 string bundles contain a codepage word and sixteen counted ASCIIZ strings. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "formats/resources/icon.h"
#include "host/fs.h"
#include "host/sync.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/ps/pmmerge/pmwin/paint.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>

LONG WinLoadString(HAB hab, HMODULE module, ULONG id, LONG capacity, PCHAR output)
{
    TRACE_NATIVE("WinLoadString(%u, %u, %u, %d, %p)", (uint)hab, (uint)module, (uint)id,
                 (int)capacity, output);
    return loadStringTable(module, 5, id, capacity, output);
}

/* PM's own resources live in PMMRGRES.DLL. They are read from the installed file at run
   time, so a system without that file has no PM strings. */
static LxHostOnce pm_resources_once = LX_HOST_ONCE_INIT;
static uint8 *pm_resources;
static size_t pm_resources_size;

static void load_pm_resources(void)
{
    uint32 error = 0;
    char *path = GProcessState.makeUnixPath
                     ? GProcessState.makeUnixPath("C:\\OS2\\DLL\\PMMRGRES.DLL", &error)
                     : NULL;
    LxHostFsError status;
    if (path)
        pm_resources = lx_host_file_snapshot(path, 8 * 1024 * 1024, &pm_resources_size, &status);
    free(path);
}

LONG loadStringTable(HMODULE module, ULONG type, ULONG id, LONG capacity, PCHAR output)
{
    if (!output || capacity <= 0 || id > UINT16_MAX)
        return 0;
    output[0] = 0;
    const ULONG bundle = id / 16 + 1;
    BOOL system = module == (HMODULE)-1;
    size_t system_size = 0;
    ULONG size = 0;
    void *data = NULL;
    if (system)
    {
        lx_host_once(&pm_resources_once, load_pm_resources);
        data = tb_executable_resource(pm_resources, pm_resources_size, type, bundle, &system_size);
        size = (ULONG)system_size;
        if (!data || size < 2)
        {
            free(data);
            return 0;
        }
    }
    else if (!GProcessState.queryResourceSize || !GProcessState.getResource ||
             !GProcessState.freeResource ||
             GProcessState.queryResourceSize(module, type, bundle, &size) || size < 2 ||
             GProcessState.getResource(module, type, bundle, &data))
        return 0;
    const uint8 *bytes = data;
    size_t at = 2;
    LONG result = 0;
    for (unsigned i = 0; i <= (id & 15) && at < size; i++)
    {
        unsigned length = bytes[at++];
        if (length > size - at)
            break;
        if (i == (id & 15) && length && !bytes[at + length - 1])
        {
            size_t count = length - 1;
            if (count >= (size_t)capacity)
                count = (size_t)capacity - 1;
            memcpy(output, bytes + at, count);
            output[count] = 0;
            result = (LONG)count;
            break;
        }
        at += length;
    }
    if (system)
        free(data);
    else
        GProcessState.freeResource(data);
    return result;
}

ULONG WinLoadLibrary(HAB hab, PSZ name)
{
    if (!getAnchorBlock(hab) || !name || !GProcessState.loadModule)
        return 0;
    LxModule *m = GProcessState.loadModule(name);
    return m ? m->handle : 0;
}

BOOL WinDeleteLibrary(HAB hab, ULONG handle)
{
    LxHandleBorrow b = {0};
    if (!getAnchorBlock(hab) || !GProcessState.freeModule ||
        !lx_handle_borrow(handle, LX_HANDLE_MODULE, &b))
        return FALSE;
    GProcessState.freeModule(b.resource);
    lx_handle_release(&b);
    return TRUE;
}

LONG WinLoadMessage(HAB hab, HMODULE module, ULONG id, LONG capacity, PSZ output)
{
    (void)hab;
    return loadStringTable(module, 10, id, capacity, output);
}
