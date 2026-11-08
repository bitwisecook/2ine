/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"
#include "os2/include/bsedos16.h"
#include "os2/include/private/bsep.h"
#include "emulator/launch_record.h"
#include <ctype.h>

void runDosExitList(const uint32 why)
{
    while (GExitList)
    {
        ExitListItem *item = GExitList;
        PFNEXITLIST host_entry = item->host_entry;
        uint32 guest_entry = item->guest_entry;
        LxModule *module = item->module;
        GExitList = item->next;
        free(item);

        if (GProcessState.execution)
            lx_execution_callback32(GProcessState.execution, guest_entry, why);
        else
            host_entry(why);
        if (module)
            GProcessState.freeModule(module);
    }
}

static uint32 processExitCode;

static int processingExitList;

static void finishProcessExit(uint32 exitcode)
{
    processExitCode = exitcode;
    processingExitList = 1;
    /* LINFOSEG rfProcStatus bit 0 marks exit-list processing for 16-bit services. */
    if (linfo)
        linfo->rfProcStatus |= 1;
    releaseThreadSuspension();
    lx_launch_record_exit(exitcode);
    runDosExitList(TC_EXIT);
    GProcessState.terminate(exitcode);
}

static void (*threadExitObserver)(ULONG code);

void lx_doscalls_observe_thread_exit(void (*observer)(ULONG code))
{
    __atomic_store_n(&threadExitObserver, observer, __ATOMIC_RELEASE);
}

VOID DosExit_implementation(ULONG action, ULONG exitcode)
{

    if (action == EXIT_THREAD)
    {
        void (*observer)(ULONG) = __atomic_load_n(&threadExitObserver, __ATOMIC_ACQUIRE);
        if (observer)
            observer(exitcode);
        FIXME("if last thread, treat as DosExit(1)");
        lx_execution_end_thread(GProcessState.execution);
        lx_host_thread_exit(NULL);
    }

    lx_execution_terminate(GProcessState.execution, finishProcessExit, exitcode);
}

VOID DosExit(ULONG action, ULONG exitcode)
{
    TRACE_NATIVE("DosExit(%u, %u)", (uint)action, (uint)exitcode);
    DosExit_implementation(action, exitcode);
}

APIRET DosExitList(ULONG ordercode, PFNEXITLIST fn)
{
    TRACE_NATIVE("DosExitList(%u, %p)", (uint)ordercode, fn);

    const uint8 cmd = ordercode & 0xFF;
    if (fn == NULL && cmd != EXLST_EXIT)
        return ERROR_INVALID_FUNCTION;
    const uint8 arg = (ordercode >> 8) & 0xFF;
    ExitListItem *prev = NULL;
    ExitListItem *item = NULL;

    if ((cmd != EXLST_ADD) && (arg != 0))
        return ERROR_INVALID_DATA;

    switch (cmd)
    {
        case EXLST_ADD:
        {
            ExitListItem *newitem = (ExitListItem *)malloc(sizeof(ExitListItem));
            if (!newitem)
                return ERROR_NOT_ENOUGH_MEMORY;
            newitem->host_entry = GProcessState.execution ? NULL : fn;
            newitem->guest_entry = GProcessState.execution ? (uint32)(uintptr_t)fn : 0;
            newitem->priority = arg;
            newitem->module = NULL;
            if (GProcessState.execution)
                for (LxModule *module = GProcessState.loaded_modules; module; module = module->next)
                {
                    if (module == GProcessState.main_module || module->nativelib)
                        continue;
                    for (uint32 i = 0; i < module->num_mmaps; i++)
                    {
                        const LxMmaps *object = &module->mmaps[i];
                        if (newitem->guest_entry >= object->guest_base &&
                            (uint64)newitem->guest_entry - object->guest_base < object->size)
                        {
                            __atomic_add_fetch(&module->refcount, 1, __ATOMIC_ACQ_REL);
                            newitem->module = module;
                            break;
                        }
                    }
                    if (newitem->module)
                        break;
                }
            for (item = GExitList; item; item = item->next)
            {
                if (item->priority >= ((uint32)arg))
                    break;
                prev = item;
            }
            if (prev)
            {
                prev->next = newitem;
                newitem->next = item;
            }
            else
            {
                newitem->next = GExitList;
                GExitList = newitem;
            }
            return NO_ERROR;
        }

        case EXLST_REMOVE:
        {
            for (item = GExitList; item; item = item->next)
            {
                if (GProcessState.execution ? item->guest_entry == (uint32)(uintptr_t)fn
                                            : item->host_entry == fn)
                {
                    if (prev)
                        prev->next = item->next;
                    else
                        GExitList = item->next;
                    LxModule *module = item->module;
                    free(item);
                    if (module)
                        GProcessState.freeModule(module);
                    return NO_ERROR;
                }
                prev = item;
            }
            return ERROR_INVALID_FUNCTION;
        }

        case EXLST_EXIT:
            /* This is a transfer, not a return to the terminating handler. The
               current entry was removed before invocation, so continue the list. */
            if (!processingExitList)
                return ERROR_INVALID_FUNCTION;
            finishProcessExit(processExitCode);
            return NO_ERROR;

        default:
            return ERROR_INVALID_DATA;
    }

    return NO_ERROR;
}

APIRET DosR3ExitAddr(void)
{
    TRACE_NATIVE("DosR3ExitAddr()");
    FIXME("I have no idea what this is");
    return ERROR_INVALID_PARAMETER;
}

VOID Dos16Exit(USHORT action, USHORT exitcode)
{
    TRACE_NATIVE("Dos16Exit(%u, %u)", (uint)action, (uint)exitcode);
    DosExit_implementation(action, exitcode);
}
