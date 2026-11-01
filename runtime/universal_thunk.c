/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host-independent guest code for OS/2's tiled 16-to-32-bit argument thunk. */
#include "runtime/execution.h"
#include "guest/abi.h"
#include "runtime/x86/unithunk_bytes.h"
#include <string.h>

int lx_execution_universal_thunk(LxExecution *execution, uint32_t ordinal,
                                 LxExecutionBridge *bridge)
{
    if (ordinal != 424 && ordinal != 576)
        return 0;
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, 0x20000, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    uint32_t base = lx_execution_mapping_address(mapping);
    uint32_t aligned = (base + 0xffff) & ~0xffffu;
    uint8_t *code = (uint8_t *)lx_execution_mapping_host(mapping) + aligned - base;
    uint16_t selector, offset;
    if (!lx_execution_find_selector(execution, aligned, &selector, &offset, 1) || offset)
    {
        lx_execution_unmap(execution, mapping);
        return 0;
    }
    uint16_t flat_data = 0x23;
#if defined(__i386__)
    if (!strcmp(lx_execution_backend(execution), "native"))
        __asm__ __volatile__("movw %%ds,%0" : "=r"(flat_data));
#endif
    memcpy(code, UniversalThunk, sizeof UniversalThunk);
    lx_guest_store_u16(code + UNITHUNK_DATA, flat_data);
    lx_guest_store_u32(code + UNITHUNK_FLAT_ADDRESS, aligned + UNITHUNK_FLAT);
    lx_guest_store_u16(code + UNITHUNK_FLAT_CS, lx_execution_flat_code_selector(execution));
    lx_guest_store_u16(code + UNITHUNK_RETURN_CS, (selector << 3) | 7);
    if (!lx_execution_protect(execution, mapping, LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        lx_execution_free_selector(execution, selector);
        lx_execution_unmap(execution, mapping);
        return 0;
    }
    unsigned entry = ordinal == 424 ? UNITHUNK_SEL_TO_FLAT : 0;
    *bridge = (LxExecutionBridge){code + entry, aligned + entry, aligned, sizeof UniversalThunk,
                                  selector};
    return 1;
}
