# Unicorn's generated backend retains its upstream GPL-2.0-or-later license.
# Make an optimized build-tree copy; never rewrite the supplied source tree.
function(twoine_optimize_unicorn source output)
    set(${output} "${source}" PARENT_SCOPE)
    if(NOT TWOINE_UNICORN_OPTIMIZATIONS OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        return()
    endif()
    # The fast path mirrors cpu_loop_exit_requested exactly. Unknown upstream
    # implementations keep their original helper rather than assuming its ABI.
    foreach(required qemu/tcg/aarch64/tcg-target.inc.c qemu/accel/tcg/tcg-runtime.c
                     qemu/include/exec/exec-all.h qemu/include/exec/cpu-defs.h
                     qemu/accel/tcg/cputlb.c)
        if(NOT EXISTS "${source}/${required}")
            message(STATUS "Unicorn optimizations: unrecognized source layout; using original backend")
            return()
        endif()
    endforeach()
    file(SHA256 "${source}/qemu/tcg/aarch64/tcg-target.inc.c" backend_hash)
    file(SHA256 "${source}/qemu/accel/tcg/tcg-runtime.c" helper_hash)
    file(SHA256 "${source}/qemu/include/exec/exec-all.h" exit_hash)
    file(SHA256 "${source}/qemu/include/exec/cpu-defs.h" defs_hash)
    file(SHA256 "${source}/qemu/accel/tcg/cputlb.c" tlb_hash)
    if(NOT backend_hash STREQUAL "8b2ea3487e97148c9905c85ee41fb0afef212b5ffd247eff523c577b31ef675a"
       OR NOT helper_hash STREQUAL "3e51962119f5b419e0b7de7c6f25e1a4dbda5bb4ad384084dab0f9df566d9f35"
       OR NOT exit_hash STREQUAL "958a8e9b371fa688e3e34c053a59bbf9072b59e8f82da3912dc552edcf563670"
       OR NOT defs_hash STREQUAL "929c0f9f711a341795e3a7a2746667154c33c3a560cb524dc985198e89c04577"
       OR NOT tlb_hash STREQUAL "902fe74afe0a88d33353cebcd73c98e739835eab37d8df76b096a8aeb2a9cb0e")
        message(STATUS "Unicorn optimizations: unrecognized source revision; using original backend")
        return()
    endif()
    file(READ "${source}/qemu/tcg/aarch64/tcg-target.inc.c" backend)
    string(REPLACE "#include \"qemu/bitops.h\""
        "#include \"qemu/bitops.h\"\n#include \"exec/helper-proto.h\"" backend "${backend}")
    string(REPLACE [=[static inline void tcg_out_call(TCGContext *s, tcg_insn_unit *target)
{
    ptrdiff_t offset = target - s->code_ptr;]=]
        [=[static inline void tcg_out_call(TCGContext *s, tcg_insn_unit *target)
{
    tcg_insn_unit *skip_exit = NULL;
    if (target == (tcg_insn_unit *)HELPER(check_exit_request)) {
        /* Preserve every exit check, avoiding the C call on the normal path.
           Backend emission keeps TCG temporaries live across this branch. */
        uintptr_t flag = (uintptr_t)&cpu_neg(s->uc->cpu)->icount_decr.u32;
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_TMP, flag);
        tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_TMP, TCG_REG_TMP, 0);
        skip_exit = s->code_ptr;
        /* TBZ W30, #31, after_call: same signed counter test as the helper. */
        tcg_out32(s, 0x36f8001e);
    }
    ptrdiff_t offset = target - s->code_ptr;]=] backend "${backend}")
    string(REPLACE [=[        tcg_out_callr(s, TCG_REG_TMP);
    }
}

void tb_target_set_jmp_target]=]
        [=[        tcg_out_callr(s, TCG_REG_TMP);
    }
    if (skip_exit) {
        ptrdiff_t skip = s->code_ptr - skip_exit;
        tcg_debug_assert(skip > 0 && skip < (1 << 13));
        *skip_exit |= (uint32_t)skip << 5;
    }
}

void tb_target_set_jmp_target]=] backend "${backend}")
    file(READ "${source}/qemu/include/exec/cpu-defs.h" defs)
    string(REPLACE [=[    MemTxAttrs attrs;
} CPUIOTLBEntry;]=]
        [=[    MemTxAttrs attrs;
    /* Same lifetime and invalidation as the cached RAM host address. */
    struct MemoryRegion *region;
} CPUIOTLBEntry;]=] defs "${defs}")
    file(READ "${source}/qemu/accel/tcg/cputlb.c" tlb)
    string(REPLACE [=[    desc->iotlb[index].attrs = attrs;]=]
        [=[    desc->iotlb[index].attrs = attrs;
    /* RAM only; snapshots and MMIO retain canonical region lookup. */
    desc->iotlb[index].region = is_ram && !cpu->uc->snapshot_level ? section->mr : NULL;]=]
        tlb "${tlb}")
    string(REPLACE [=[    MemoryRegion *mr = cpu->uc->memory_mapping(cpu->uc, tlbe->paddr | (mem_vaddr & ~TARGET_PAGE_MASK));]=]
        [=[    MemoryRegion *mr = cpu->uc->snapshot_level ? NULL : iotlbentry->region;
    if (!mr) {
        mr = cpu->uc->memory_mapping(cpu->uc, tlbe->paddr | (mem_vaddr & ~TARGET_PAGE_MASK));
    }]=] tlb "${tlb}")
    # Cache only the initial lookup after resolving the TLB entry. Fresh lookups
    # following memory hooks, CoW and faults remain untouched.
    string(REPLACE [=[    paddr = entry->paddr | (addr & ~TARGET_PAGE_MASK);
    mr = uc->memory_mapping(uc, paddr);

    // memory might be still unmapped]=]
        [=[    paddr = entry->paddr | (addr & ~TARGET_PAGE_MASK);
    mr = uc->snapshot_level ? NULL : env_tlb(env)->d[mmu_idx].iotlb[index].region;
    if (!mr) {
        mr = uc->memory_mapping(uc, paddr);
    }

    // memory might be still unmapped]=] tlb "${tlb}")
    string(REPLACE [=[    // Load the latest memory mapping.
    paddr = entry->paddr | (addr & ~TARGET_PAGE_MASK);
    mr = uc->memory_mapping(uc, paddr);]=]
        [=[    // Reuse the resolved RAM region until the TLB is invalidated.
    paddr = entry->paddr | (addr & ~TARGET_PAGE_MASK);
    mr = uc->snapshot_level ? NULL : env_tlb(env)->d[mmu_idx].iotlb[index].region;
    if (!mr) {
        mr = uc->memory_mapping(uc, paddr);
    }]=] tlb "${tlb}")
    set(patched "${CMAKE_CURRENT_BINARY_DIR}/unicorn-source")
    file(COPY "${source}/" DESTINATION "${patched}"
        REGEX "/\\.git(/|$)" EXCLUDE
        REGEX "/qemu/tcg/aarch64/tcg-target\\.inc\\.c$" EXCLUDE
        REGEX "/qemu/include/exec/cpu-defs\\.h$" EXCLUDE
        REGEX "/qemu/accel/tcg/cputlb\\.c$" EXCLUDE)
    # Configure only rewrites changed content, avoiding recompilation on every
    # configure. The remaining copied files retain their upstream timestamps.
    file(CONFIGURE OUTPUT "${patched}/qemu/tcg/aarch64/tcg-target.inc.c"
        CONTENT "${backend}" @ONLY)
    file(CONFIGURE OUTPUT "${patched}/qemu/include/exec/cpu-defs.h"
        CONTENT "${defs}" @ONLY)
    file(CONFIGURE OUTPUT "${patched}/qemu/accel/tcg/cputlb.c"
        CONTENT "${tlb}" @ONLY)
    message(STATUS "Unicorn optimizations: inline AArch64 exits and RAM region cache enabled")
    set(${output} "${patched}" PARENT_SCOPE)
endfunction()
