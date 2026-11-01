/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
uint32 guestEntryAddress(void *host)
{
    uint32 address;
    if (!lx_execution_entry_address(GLoaderState.execution, host, &address))
    {
        fprintf(stderr, "%s\n", lx_execution_error(GLoaderState.execution));
        abort();
    }
    return address;
}

LxExecutionEntry executionEntry(const LxModule *module)
{
    LxExecutionEntry e = {0};
    e.entry = module->eip;
    e.stack = module->esp;
    e.module = module->handle;
    if (!lx_execution_address(GLoaderState.execution, GLoaderState.pib.pib_pchcmd, 1, LX_GUEST_READ,
                              &e.command) ||
        !lx_execution_address(GLoaderState.execution, GLoaderState.pib.pib_pchenv, 1, LX_GUEST_READ,
                              &e.environment))
    {
        fprintf(stderr, "Process parameters are outside guest memory\n");
        abort();
    }
    e.stack_bytes = GLoaderState.mainstacksize;
    e.data_bytes = module->autodatasize;
    if (!module->is_lx && module->header.ne.auto_data_segment)
        e.data_selector =
            lxSelectorToSegment(module->mmaps[module->header.ne.auto_data_segment - 1].alias);
    if (module->is_lx && module->entry16)
    {
        const LxHeader *header = &module->header.lx;
        const LxMmaps *code = &module->mmaps[header->eip_object - 1];
        const LxMmaps *stack = &module->mmaps[header->esp_object - 1];
        if (code->alias == 0xffff || stack->alias == 0xffff || header->eip > 0xffff ||
            header->esp > 0xffff)
        {
            fprintf(stderr, "Invalid USE16 LX entry or stack\n");
            abort();
        }
        e.entry = (uint32)lxSelectorToSegment(code->alias) << 16 | header->eip;
        e.stack = (uint32)lxSelectorToSegment(stack->alias) << 16 | header->esp;
        if (header->auto_ds_object_num)
        {
            const LxMmaps *data = &module->mmaps[header->auto_ds_object_num - 1];
            e.data_selector = lxSelectorToSegment(data->alias);
            e.data_bytes = (uint16)data->size;
        }
    }
    return e;
}

void runModule(LxModule *module)
{
    LxExecutionEntry e = executionEntry(module);
    GLoaderState.running = 1;
    lx_execution_program(GLoaderState.execution, &e, module->is_lx && !module->entry16 ? 32 : 16);
}

void runLxLibraryInitOrTerm(LxModule *module, int termination)
{
    LxExecutionEntry e = executionEntry(module);
    e.stack = GLoaderState.running ? 0 : GLoaderState.main_module->esp;
    lx_execution_library(GLoaderState.execution, &e, termination);
}

__attribute__((noreturn)) void lxTerminate(const uint32 exitcode)
{
    /* Guest threads may still execute these mappings; process exit releases them. */
    if (__atomic_load_n(&GLoaderState.ne_threads_started, __ATOMIC_ACQUIRE))
        lx_host_process_exit((int)(exitcode & 0xff));

    freeLxModule(GLoaderState.main_module);

    /* Dependencies precede their dependents in the load list; teardown starts at its head. */
    while (GLoaderState.loaded_modules)
    {
        LxModule *lxmod = GLoaderState.loaded_modules;
        lxmod->refcount = 1;
        freeLxModule(lxmod);
    }

    GLoaderState.lib2ine_shutdown();

    if (!GLoaderState.running)
        lx_execution_destroy(GLoaderState.execution);
    GLoaderState.execution = NULL;

    if (exitcode > 255)
        FIXME("deal with process exit codes > 255. We clamped this one!");

    lx_host_process_exit((int)(exitcode & 0xFF));
}

void runLxLibraryInit(LxModule *lxmod)
{

    if (1)
    {
        if (lxmod->header.lx.eip_object != 0)
            runLxLibraryInitOrTerm(lxmod, 0);
    }
}

void runLxLibraryTerm(LxModule *lxmod)
{
    // See notes about global initialization in runLxLibraryInit(); it
    //  applies to deinit here, too.
    if (1)
    {
        if (lxmod->header.lx.eip_object != 0)
            runLxLibraryInitOrTerm(lxmod, 1);
    }
}

void runNeLibraryInit(LxModule *lxmod)
{
    FIXME("write me");
}

void runNeLibraryTerm(LxModule *lxmod)
{
    FIXME("write me");
}

void runLibraryTerm(LxModule *lxmod)
{
    if (lxmod->is_lx)
    {
        runLxLibraryTerm(lxmod);
    }
    else
    {
        runNeLibraryTerm(lxmod);
    }
}

void executionReturned(void *unused, uint32 result)
{
    (void)unused;
    if (GLoaderState.dosExit)
        GLoaderState.dosExit(1, result);
    lxTerminate(result);
}
