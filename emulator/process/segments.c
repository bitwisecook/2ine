/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* 16-bit segments, selectors and 16:16 far pointers for the OS/2 libraries.
   The execution instance owns the descriptor table; a host program running
   lib2ine without the loader has none, so every request fails. */
#include "emulator/process/internal.h"

/* The selector reported when no segment was allocated. */
#define NO_SELECTOR 0xFFFF

void *lxAllocSegment(uint16 *selector, const int iscode)
{
    if (GProcessState.execution_bound)
        return lx_execution_alloc_segment(GProcessState.execution, selector, iscode);
    if (selector)
        *selector = NO_SELECTOR;
    return NULL;
}

void lxFreeSegment(const uint16 selector)
{
    if (GProcessState.execution_bound)
        lx_execution_free_segment(GProcessState.execution, selector);
}

int lxCreateCodeAlias(const uint16 selDS, uint16 *outselector)
{
    return GProcessState.execution_bound &&
           lx_execution_code_alias(GProcessState.execution, selDS, outselector);
}

int lxFindSelector(const uint32 addr, uint16 *outselector, uint16 *outoffset, int iscode)
{
    return GProcessState.execution_bound &&
           lx_execution_find_selector(GProcessState.execution, addr, outselector, outoffset,
                                      iscode);
}

void lxFreeSelector(const uint16 selector)
{
    if (GProcessState.execution_bound)
        lx_execution_free_selector(GProcessState.execution, selector);
}

void *lxConvert1616to32(const uint32 addr1616)
{
    return GProcessState.execution_bound
               ? lx_execution_far_to_host(GProcessState.execution, addr1616)
               : NULL;
}

uint32 lxConvert32to1616(void *addr32)
{
    return GProcessState.execution_bound ? lx_execution_host_to_far(GProcessState.execution, addr32)
                                         : 0;
}
