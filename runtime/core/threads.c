/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "runtime/core/internal.h"
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

void initOs2Tib_lib2ine(uint8 *tibspace, void *_topOfStack, const size_t stacklen, const uint32 tid)
{
    uint8 *topOfStack = (uint8 *)_topOfStack;

    LxTIB *tib = (LxTIB *)tibspace;
    LxTIB2 *tib2 = (LxTIB2 *)(tib + 1);
    FIXME("This is probably 50% wrong");
    uint32 top = (uint32)(uintptr_t)topOfStack;
    uint32 tib_address = 0;
    if (GLoaderState.execution)
    {
        lx_execution_address(GLoaderState.execution, tibspace, LXTIBSIZE, 0, &tib_address);
        uint32 guest_top;
        if (lx_execution_address(GLoaderState.execution, topOfStack, 0, 0, &guest_top))
            top = guest_top;
    }
    tib->tib_pexchain = 0;
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

uint16 setOs2Tib_lib2ine(uint8 *tibspace)
{
    DefaultTib = (LxTIB *)tibspace;
    return 1;
}

LxTIB *getOs2Tib_lib2ine(void)
{
    return DefaultTib;
}

void deinitOs2Tib_lib2ine(const uint16 selector)
{
    lxHostThreadCleanup();
    DefaultTib = NULL;
}
