/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/process/internal.h"

LxHostThreadState *lxHostThreadState(void)
{
    return &host_thread_state;
}

void lxHostThreadCleanup(void)
{
    if (host_thread_state.pm_cleanup)
    {
        void (*cleanup)(void) = host_thread_state.pm_cleanup;
        host_thread_state.pm_cleanup = NULL;
        cleanup();
    }
}

void lxInitOs2Tib(uint8 *tibspace, void *_topOfStack, const size_t stacklen, const uint32 tid)
{
    uint8 *topOfStack = (uint8 *)_topOfStack;

    LxTIB *tib = (LxTIB *)tibspace;
    LxTIB2 *tib2 = (LxTIB2 *)(tib + 1);
    FIXME("This is probably 50% wrong");
    uint32 top = (uint32)(uintptr_t)topOfStack;
    uint32 tib_address = 0;
    if (GProcessState.execution)
    {
        lx_execution_address(GProcessState.execution, tibspace, LXTIBSIZE, 0, &tib_address);
        uint32 guest_top;
        if (lx_execution_address(GProcessState.execution, topOfStack, 0, 0, &guest_top))
            top = guest_top;
    }
    tib->tib_pexchain = UINT32_MAX;
    tib->tib_pstack = top - stacklen;
    tib->tib_pstacklimit = top;
    tib->tib_ptib2 = tib_address ? tib_address + sizeof(LxTIB) : 0;
    tib->tib_version = 20;
    tib->tib_ordinal = 79;

    tib2->tib2_ultid = tid;
    tib2->tib2_ulpri = 512;
    tib2->tib2_version = 20;
    tib2->tib2_usMCCount = 0;
    tib2->tib2_fMCForceFlag = 0;
}

/* Bound to an execution instance, each thread's TIB is copied into its own
   guest mapping: a 64-KiB aligned block inside two tiles, so the TIB starts a
   16-bit segment. */
#define TIB_MAPPING_SIZE 0x20000u
#define TIB_ALIGNMENT 0x10000u

uint16 lxSetOs2Tib(uint8 *tibspace)
{
    if (!GProcessState.execution_bound)
    {
        DefaultTib = (LxTIB *)tibspace;
        return 1;
    }
    LxHostThreadState *state = lxHostThreadState();
    if (state->tib_mapping)
    {
        fprintf(stderr, "TIB is already attached\n");
        abort();
    }
    state->tib_mapping = lx_execution_map(GProcessState.execution, 0, TIB_MAPPING_SIZE,
                                          LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!state->tib_mapping)
    {
        fprintf(stderr, "Cannot allocate guest TIB\n");
        abort();
    }
    uint32 base = lx_execution_mapping_address(state->tib_mapping);
    uint32 address = (base + TIB_ALIGNMENT - 1) & ~(TIB_ALIGNMENT - 1);
    uint8 *tib = (uint8 *)lx_execution_mapping_host(state->tib_mapping) + (address - base);
    memcpy(tib, tibspace, LXTIBSIZE);
    lx_guest_store_u32(tib + offsetof(LxTIB, tib_ptib2), address + sizeof(LxTIB));
    return lx_execution_attach_tib(GProcessState.execution, tib);
}

LxTIB *lxGetOs2Tib(void)
{
    return GProcessState.execution_bound ? lx_execution_current_tib(GProcessState.execution)
                                         : DefaultTib;
}

void lxDeinitOs2Tib(const uint16 selector)
{
    lxHostThreadCleanup();
    if (!GProcessState.execution_bound)
    {
        DefaultTib = NULL;
        return;
    }
    lx_execution_detach_tib(GProcessState.execution, selector);
    LxHostThreadState *state = lxHostThreadState();
    if (state->tib_mapping)
        lx_execution_unmap(GProcessState.execution, state->tib_mapping);
    state->tib_mapping = NULL;
}
