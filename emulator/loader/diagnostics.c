/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Loaded-module descriptions for fatal reports, fault addresses and load failures. */
#include "emulator/loader/internal.h"

int describeModuleAddress(const void *ptr, char *buf, const size_t buflen)
{
    const uint32 addr = (uint32)(size_t)ptr;

    for (LxModule *lxmod = GProcessState.loaded_modules; lxmod; lxmod = lxmod->next)
    {
        for (uint32 i = 0; i < lxmod->num_mmaps; i++)
        {
            const LxMmaps *mmap = &lxmod->mmaps[i];
            const uint32 base = mmap->guest_base;
            const uint32 end = base + (uint32)mmap->size;
            if ((addr >= base) && (addr < end))
            {
                snprintf(buf, buflen, "%s object #%u +0x%X [base=%08X size=%X]", lxmod->name,
                         (uint)i + 1, (uint)(addr - base), base, (uint)mmap->size);
                return 1;
            }
        }
    }

    *buf = '\0';
    return 0;
}

/* Runs in the fatal reporter, possibly from a signal handler: no locks or allocation. */
void lxWriteFatalModules(void)
{
    lx_host_fatal_write("\n-- OS/2 modules --\n");
    for (const LxModule *module = GProcessState.loaded_modules; module; module = module->next)
    {
        lx_host_fatal_write(module->name);
        lx_host_fatal_write(module->is_lx ? " LX " : " NE ");
        lx_host_fatal_write(module->os2path ? module->os2path : "(no OS/2 path)");
        for (uint32 i = 0; i < module->num_mmaps; i++)
        {
            lx_host_fatal_write(" obj");
            lx_host_fatal_write_decimal(i + 1);
            lx_host_fatal_write("=");
            lx_host_fatal_write_hex(module->mmaps[i].guest_base, 8);
            lx_host_fatal_write("+");
            lx_host_fatal_write_hex(module->mmaps[i].size, 8);
        }
        lx_host_fatal_write("\n");
    }
}

void lxDescribeModuleFailure(const char *program, char *buffer, size_t size)
{
    uint32 code;
    const char *module = lxLastModuleFailure(&code);
    const char *reason = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
                             ? "the file was not found"
                         : code == ERROR_ACCESS_DENIED ? "access was denied"
                         : code == ERROR_BAD_FORMAT    ? "it is not a valid OS/2 module"
                         : code == ERROR_MOD_NOT_FOUND ? "a required module was not found"
                         : code == ERROR_PROC_NOT_FOUND || code == ERROR_INVALID_ORDINAL
                             ? "a required function was not found"
                             : "it could not be loaded";
    if (*module && strcmp(module, program))
        snprintf(buffer, size, "Cannot load %s: %s (%s, OS/2 error %u)", program, reason, module,
                 (uint)code);
    else
        snprintf(buffer, size, "Cannot load %s: %s (OS/2 error %u)", program, reason, (uint)code);
}

int describeExecutionAddress(void *unused, uint32 address, char *buffer, size_t size)
{
    (void)unused;
    return describeModuleAddress((void *)(uintptr_t)address, buffer, size);
}
