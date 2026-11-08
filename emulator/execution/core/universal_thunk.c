/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host-independent guest code for OS/2's tiled 16-to-32-bit argument thunk. */
#include "emulator/execution.h"
#include "emulator/abi.h"
#include "emulator/execution/core/x86/unithunk_bytes.h"
#include <string.h>

/* The thunk starts a 64-KiB aligned block inside a two-tile mapping, so its
   tiled code selector has offset zero. */
#define THUNK_MAPPING_SIZE 0x20000u
#define THUNK_ALIGNMENT 0x10000u
/* Selector bits for an LDT entry at ring 3. */
#define LDT_RPL3 7

int lx_execution_universal_thunk(LxExecution *execution, uint32_t ordinal,
                                 LxExecutionBridge *bridge)
{
    if (ordinal != LX_ORDINAL_DOS16SELTOFLAT && ordinal != LX_ORDINAL_THK16_UNITHUNK)
        return 0;
    LxGuestMapping *mapping =
        lx_execution_map(execution, 0, THUNK_MAPPING_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    uint32_t base = lx_execution_mapping_address(mapping);
    uint32_t aligned = (base + THUNK_ALIGNMENT - 1) & ~(THUNK_ALIGNMENT - 1);
    uint8_t *code = (uint8_t *)lx_execution_mapping_host(mapping) + aligned - base;
    uint16_t selector, offset;
    if (!lx_execution_find_selector(execution, aligned, &selector, &offset, 1) || offset)
    {
        lx_execution_unmap(execution, mapping);
        return 0;
    }
    uint16_t flat_data = lx_execution_flat_data_selector(execution);
    memcpy(code, UniversalThunk, sizeof UniversalThunk);
    lx_guest_store_u16(code + UNITHUNK_DATA, flat_data);
    lx_guest_store_u32(code + UNITHUNK_FLAT_ADDRESS, aligned + UNITHUNK_FLAT);
    lx_guest_store_u16(code + UNITHUNK_FLAT_CS, lx_execution_flat_code_selector(execution));
    lx_guest_store_u16(code + UNITHUNK_RETURN_CS, (uint16_t)((selector << 3) | LDT_RPL3));
    if (!lx_execution_protect(execution, mapping, LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        lx_execution_free_selector(execution, selector);
        lx_execution_unmap(execution, mapping);
        return 0;
    }
    unsigned entry = ordinal == LX_ORDINAL_DOS16SELTOFLAT ? UNITHUNK_SEL_TO_FLAT : 0;
    *bridge = (LxExecutionBridge){code + entry, aligned + entry, aligned, sizeof UniversalThunk,
                                  selector};
    return 1;
}
