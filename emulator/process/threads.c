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

/* The exception-chain terminator (END_OF_CHAIN in bsexcpt.h). */
#define END_OF_CHAIN UINT32_MAX
/* The TIB version and ordinal reported to every guest thread. */
#define TIB_VERSION 20
#define TIB_ORDINAL 79
/* New threads run in the regular priority class at level 0 (bsedos.h). */
#define PRTYC_REGULAR 2

void lxInitOs2Tib(uint8 *tibspace, void *_topOfStack, const size_t stacklen, const uint32 tid)
{
    uint8 *topOfStack = (uint8 *)_topOfStack;

    LxGuestTib *tib = (LxGuestTib *)tibspace;
    LxGuestTib2 *tib2 = (LxGuestTib2 *)(tib + 1);
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
    lx_guest_store_u32(&tib->exception_chain, END_OF_CHAIN);
    lx_guest_store_u32(&tib->stack_base, top - (uint32)stacklen);
    lx_guest_store_u32(&tib->stack_limit, top);
    lx_guest_store_u32(&tib->tib2, tib_address ? tib_address + (uint32)sizeof(LxGuestTib) : 0);
    lx_guest_store_u32(&tib->version, TIB_VERSION);
    lx_guest_store_u32(&tib->ordinal, TIB_ORDINAL);

    lx_guest_store_u32(&tib2->thread_id, tid);
    lx_guest_store_u32(&tib2->priority, PRTYC_REGULAR << 8);
    lx_guest_store_u32(&tib2->version, TIB_VERSION);
    lx_guest_store_u16(&tib2->must_complete, 0);
    lx_guest_store_u16(&tib2->force_flag, 0);
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
        DefaultTib = (LxGuestTib *)tibspace;
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
    lx_guest_store_u32(tib + offsetof(LxGuestTib, tib2), address + (uint32)sizeof(LxGuestTib));
    return lx_execution_attach_tib(GProcessState.execution, tib);
}

LxGuestTib *lxGetOs2Tib(void)
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
