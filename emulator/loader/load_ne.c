/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/loader/internal.h"

/* Loads 16-bit NE modules: segments, segment fixups and the entry table. */

enum
{
    /* Deeper import chains than this are treated as circular. */
    MAX_DEPENDENCY_DEPTH = 32,
    /* Names are length-prefixed with a byte. */
    MAX_NAME_BUFFER = 256,
    /* Allocation-failure value of the selector out-parameter. */
    NO_SELECTOR = 0xFFFF,
    /* Minimum stack reserved above the automatic data, and the highest usable stack top. */
    MIN_AUTODATA_STACK = 0x8000,
    MAX_AUTODATA_STACK_TOP = 0xFFF0,
    /* SP zero in a far stack pointer means the top of a 64 KiB segment. */
    SEGMENT_TOP_SP = 0xFFFF,
    /* A fixup source chain ends at this offset. */
    CHAIN_END = 0xFFFF
};

int sanityCheckNeModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(NeHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const NeHeader *ne = (const NeHeader *)exe;
    if (ne->exe_type > NE_OS2)
    {
        fprintf(stderr, "Not an OS/2 NE module file (exe_type is %d, not 1)\n", (int)ne->exe_type);
        return 0;
    }

    return 1;
}

int fixupNeSegment(const NeSegmentTableEntry *seg, LxModule *lxmod, uint8 *dst,
                   const uint32 sector_shift, const uint8 *origexe, uint8 *exe, uint32 exelen)
{
    assert(!lxmod->is_lx);

    if ((seg->segment_flags & NSRELOC) == 0)
        return 1;

    const uint8 *segptr = origexe + (seg->offset << sector_shift);
    const uint8 *fixupptr = segptr + (seg->size ? seg->size : 0xFFFF);
    const uint32 num_fixups = (uint32) * ((const uint16 *)fixupptr);
    fixupptr += 2;
    /* NE chains address the virtual segment, including its zero-filled tail. */
    const uint32 seg_fixup_limit =
        seg->minimum_allocation ? (uint32)seg->minimum_allocation : NE_SEGMENT_MAX_SIZE;

    for (uint32 i = 0; i < num_fixups; i++)
    {
        const uint8 srctype = *(fixupptr++);
        const uint8 flags = *(fixupptr++);
        uint32 srcchain_offset = (uint32) * ((const uint16 *)fixupptr);
        fixupptr += 2;
        const uint8 *targetptr = fixupptr;
        fixupptr += 4;
        const int additive = (flags & NRADD) != 0;
        // NE names the patched location "source" and its resolved value "target".
        uint32 target = 0;

        switch (flags & NRRTYP)
        {
            case NRRINT:
            {
                const LxExport *lxexp = NULL;
                const uint8 segment = targetptr[0];

                const uint16 offset = *((const uint16 *)&targetptr[2]);
                if (segment == NE_MOVABLE_SEGMENT)
                { /* A movable target is an entry ordinal in this module. */
                    void *addr = getModuleProcAddrByOrdinal(lxmod, offset, &lxexp, 1, 0);
                    if (addr)
                    {
                        target = lx_execution_host_to_far(GProcessState.execution, addr);
                    }
                }
                else
                {
                    target = (lxSelectorToSegment(lxmod->mmaps[segment - 1].alias) << 16) | offset;
                }
            }
            break;

            case NRRORD:
            {
                const uint16 module = ((const uint16 *)targetptr)[0];
                const uint16 ordinal = ((const uint16 *)targetptr)[1];
                const LxExport *lxexp = NULL;
                void *addr = getModuleProcAddrByOrdinal(lxmod->dependencies[module - 1], ordinal,
                                                        &lxexp, 1, 1);
                if (addr)
                {
                    target = lx_execution_host_to_far(GProcessState.execution, addr);
                }
            }
            break;

            case NRRNAM:
            {
                const uint16 module = ((const uint16 *)targetptr)[0];
                const uint16 offset = ((const uint16 *)targetptr)[1];
                const LxExport *lxexp = NULL;
                const uint8 *import_name =
                    (exe + lxmod->header.ne.imported_names_table_offset) + offset;
                char name[MAX_NAME_BUFFER];
                const uint8 namelen = *(import_name++);
                memcpy(name, import_name, namelen);
                name[namelen] = '\0';
                void *addr =
                    getModuleProcAddrByName(lxmod->dependencies[module - 1], name, &lxexp, 1);
                if (addr)
                {
                    target = lx_execution_host_to_far(GProcessState.execution, addr);
                }
            }
            break;

            case NRROSF:
                /* Operating-system (floating-point) fixups are deliberately left unapplied. */
                continue;

            default:
                fprintf(stderr, "unknown fixup target type %u\n", (uint)(flags & NRRTYP));
                break;
        }

        if (additive)
        {
            uint16 *source = (uint16 *)(dst + srcchain_offset);
            switch (srctype & NRSTYP)
            {
                case NRSBYT:
                    *((uint8 *)source) += target & 0xFF;
                    break;
                case NRSSEG:
                    *source += target >> 16;
                    break;
                case NRSPTR:
                    source[0] += target & 0xFFFF;
                    source[1] += target >> 16;
                    break;
                case NRSOFF:
                    *source += target & 0xFFFF;
                    break;
                default:
                    fprintf(stderr, "unknown fixup type %u\n", (uint)(srctype & NRSTYP));
                    break;
            }
        }
        else
        {
            /* Offset zero is a valid first fixup; only subsequent chain links terminate at zero. */
            if (srcchain_offset < seg_fixup_limit)
            {
                do
                {
                    uint16 *source = (uint16 *)(dst + srcchain_offset);
                    const uint32 next_offset = (uint32)*source;
                    switch (srctype & NRSTYP)
                    {
                        case NRSBYT:
                            *((uint8 *)source) = target & 0xFF;
                            break;
                        case NRSSEG:
                            *source = target >> 16;
                            break;
                        case NRSPTR:
                            source[0] = target & 0xFFFF;
                            source[1] = target >> 16;
                            break;
                        case NRSOFF:
                            *source = target & 0xFFFF;
                            break;
                        default:
                            fprintf(stderr, "unknown fixup type %u\n", (uint)(srctype & NRSTYP));
                            break;
                    }
                    srcchain_offset = next_offset;
                } while ((srcchain_offset != 0) && (srcchain_offset != CHAIN_END) &&
                         (srcchain_offset < seg_fixup_limit));
            }
        }
    }

    return 1;
}

LxModule *loadNeModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen,
                       int dependency_tree_depth)
{
    LxModule *retval = NULL;
    FIXME("All of this needs bounds checking and corruption testing");
    const NeHeader *ne = (const NeHeader *)exe;
    const uint16 module_type = ne->module_flags & NENOTP;

    if (ne->module_flags & NEIERR)
    {
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadne_failed;
    }

    const int isDLL = (module_type == NENOTP);

    if (isDLL && !GProcessState.main_module)
    {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadne_failed;
    }
    else if (!isDLL && GProcessState.main_module)
    {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadne_failed;
    }

    retval = (LxModule *)malloc(sizeof(LxModule));
    if (!retval)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadne_failed;
    }
    memset(retval, '\0', sizeof(*retval));
    retval->is_lx = 0;
    retval->refcount = 1;
    retval->handle = lx_handle_create(LX_HANDLE_MODULE, retval, destroyLxModule);
    if (!retval->handle)
    {
        fprintf(stderr, "Cannot allocate module handle\n");
        free(retval);
        return NULL;
    }
    retval->dependencies =
        (LxModule **)malloc(sizeof(LxModule *) * ne->num_module_ref_table_entries);
    retval->mmaps = (LxMmaps *)malloc(sizeof(LxMmaps) * ne->num_segment_table_entries);
    if (!retval->dependencies || !retval->mmaps)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadne_failed;
    }
    memset(retval->dependencies, '\0', sizeof(LxModule *) * ne->num_module_ref_table_entries);
    memset(retval->mmaps, '\0', sizeof(LxMmaps) * ne->num_segment_table_entries);
    retval->num_dependencies = ne->num_module_ref_table_entries;
    retval->num_mmaps = ne->num_segment_table_entries;
    for (int i = 0; i < retval->num_mmaps; i++)
        retval->mmaps[i].alias = LX_NO_ALIAS;

    memcpy(&retval->header.ne, ne, sizeof(*ne));

    if (ne->resident_name_table_offset)
    {
        const uint8 *name_table = exe + ne->resident_name_table_offset;
        const uint8 namelen = *(name_table++);
        char *ptr = retval->name;
        for (uint32 i = 0; i < namelen; i++)
        {
            const char ch = *(name_table++);
            *(ptr++) = ((ch >= 'a') && (ch <= 'z')) ? (ch + ('A' - 'a')) : ch;
        }
        *ptr = '\0';
    }

    if (!isDLL)
    {
        GProcessState.main_module = retval;
        GProcessState.pib.module = retval->handle;
        if (GProcessState.guest_pib)
        {
            LxGuestPib *pib = lx_execution_mapping_host(GProcessState.guest_pib);
            lx_guest_store_u32(&pib->module, retval->handle);
        }
    }

    const char *modname = retval->name;

    if (++dependency_tree_depth > MAX_DEPENDENCY_DEPTH)
    {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
        goto loadne_failed;
    }

    if ((!ne->reg_ss) || (ne->reg_ss > ne->num_segment_table_entries))
    {
        fprintf(stderr, "Bogus reg_ss in module '%s'\n", modname);
        goto loadne_failed;
    }

    FIXME("What do we do if ne->NOAUTODATA doesn't agree with ne->auto_data_segment?");
    const NeSegmentTableEntry *seg = (const NeSegmentTableEntry *)(exe + ne->segment_table_offset);
    const NeSegmentTableEntry *autodataseg =
        ne->auto_data_segment ? (seg + ne->auto_data_segment) : NULL;
    if (autodataseg)
    {
        retval->autodatasize = autodataseg->size;
    }

    for (uint32 i = 0; i < ne->num_segment_table_entries; i++, seg++)
    {
        /* Allocate full 64-KiB NE segments so tiled selectors retain a fixed extent. */
        const uint32 vsize = NE_SEGMENT_MAX_SIZE;
        /* NE segments remain fixed for their mapping lifetime. */
        const int iscode = ((seg->segment_flags & NSTYPE) == NSCODE) ? 1 : 0;
        const int isiterated = (seg->segment_flags & NSITER) ? 1 : 0;
        uint16 selector = NO_SELECTOR;
        void *mmapaddr = lx_execution_alloc_segment(GProcessState.execution, &selector, iscode);

        if (mmapaddr == NULL)
        {
            fprintf(stderr, "lx_execution_alloc_segment(%d) failed\n", iscode);
            goto loadne_failed;
        }
        else if (selector == NO_SELECTOR)
        {
            /* The execution instance retains ownership of this failed allocation. */
            mmapaddr = NULL;
            fprintf(stderr, "Failed to get a selector for a segment\n");
            goto loadne_failed;
        }

        if (getenv("TWOINE_TRACE_SEGMENTS"))
            fprintf(stderr, "2INE NESEG %s #%u addr=%p selector=%04X iscode=%d\n", retval->name,
                    (uint)i + 1, mmapaddr, (uint)selector, iscode);
        uint32 guest_address;
        if (!lx_execution_address(GProcessState.execution, mmapaddr, vsize, 0, &guest_address))
        {
            fprintf(stderr, "NE segment is outside guest memory\n");
            goto loadne_failed;
        }
        retval->mmaps[i].mapping = lx_execution_mapping_at(GProcessState.execution, guest_address);
        retval->mmaps[i].bits = 16;
        retval->mmaps[i].guest_base = guest_address;
        retval->mmaps[i].mapped = mmapaddr;
        retval->mmaps[i].size = vsize;
        retval->mmaps[i].addr = mmapaddr;
        retval->mmaps[i].alias = selector;

        uint8 *dst = (uint8 *)mmapaddr;
        const size_t segfiledataoffset = seg->offset;
        const size_t segfiledatalen = seg->size ? seg->size : NE_SEGMENT_MAX_SIZE;
        assert(segfiledatalen <= vsize);
        const uint8 *src = origexe + (segfiledataoffset << ne->sector_alignment_shift_count);

        if (isiterated)
        {
            if (!lx_exepack_iterated_decompress(dst, vsize, src, segfiledatalen))
            {
                fprintf(stderr, "Failed to decompress iterated segment (corrupt file or bug).\n");
                goto loadne_failed;
            }
        }
        else
        {
            size_t leftover = vsize;
            if (segfiledataoffset)
            {
                memcpy(dst, src, segfiledatalen);

                leftover -= segfiledatalen;
                dst += segfiledatalen;
            }
            if (leftover)
            {
                memset(dst, '\0', leftover);
            }
        }
    }

    if (!isDLL && ne->auto_data_segment && (ne->auto_data_segment <= ne->num_segment_table_entries))
    {
        uint32 data_address;
        if (!lx_execution_address(GProcessState.execution,
                                  retval->mmaps[ne->auto_data_segment - 1].addr, 1, 0,
                                  &data_address))
            abort();
        lx_execution_near_base(GProcessState.execution, data_address);
    }

    retval->eip = (lxSelectorToSegment(retval->mmaps[ne->reg_cs - 1].alias) << 16) | ne->reg_ip;

    if (!isDLL)
    {
        /* "If SS equals the automatic data segment and SP equals
            zero, the stack pointer is set to the top of the automatic data
            segment just below the additional heap area." */
        uint16 sp = ne->reg_sp;
        if ((ne->reg_ss == ne->auto_data_segment) && (sp == 0))
        {
            if (!autodataseg)
            {
                goto loadne_failed;
            }
            /* Reserve stack space below autodata; the declared minimum can understate runtime usage. */
            uint32 stacktop = (uint32)autodataseg->size + ne->stack_size;
            if (stacktop < ((uint32)autodataseg->size + MIN_AUTODATA_STACK))
                stacktop = (uint32)autodataseg->size + MIN_AUTODATA_STACK;
            if (stacktop >= MAX_AUTODATA_STACK_TOP)
                stacktop = MAX_AUTODATA_STACK_TOP;
            sp = (uint16)stacktop;
        }

        retval->esp = (lxSelectorToSegment(retval->mmaps[ne->reg_ss - 1].alias) << 16) |
                      (sp ? sp : SEGMENT_TOP_SP);

        uint16 stacksize = ne->stack_size;
        if (stacksize == 0)
        {
            stacksize = sp;
            if (sp && (ne->reg_ss == ne->auto_data_segment))
            {
                stacksize -= autodataseg->size;
            }
        }
        GProcessState.mainstacksize = stacksize;

        /* Library initialization requires the process stack to be installed. */
        void *topofstack = (void *)(((size_t)retval->mmaps[ne->reg_ss - 1].addr) + sp);
        GProcessState.initOs2Tib(GProcessState.main_tibspace, topofstack, stacksize, 1);
        GProcessState.main_tib_selector = GProcessState.setOs2Tib(GProcessState.main_tibspace);
    }

    if (ne->entry_table_size > 0)
    {
        uint32 total_ordinals = 0;
        const uint8 *entryptr = exe + ne->entry_table_offset;
        while (*entryptr)
        {
            const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
            const uint8 bundletype = *(entryptr++);
            total_ordinals += numentries;
            if (bundletype != NE_BUNDLE_UNUSED)
            {
                entryptr += ((bundletype == NE_BUNDLE_MOVABLE) ? NE_MOVABLE_ENTRY_SIZE
                                                               : NE_FIXED_ENTRY_SIZE) *
                            numentries;
            }
        }

        if (total_ordinals > 0)
        {
            LxExport *lxexp = (LxExport *)calloc(1, sizeof(LxExport) * total_ordinals);
            if (!lxexp)
            {
                fprintf(stderr, "Out of memory!\n");
                goto loadne_failed;
            }
            memset(lxexp, '\0', sizeof(LxExport) * total_ordinals);
            retval->exports = lxexp;

            uint32 ordinal = 1;
            entryptr = exe + ne->entry_table_offset;
            while (*entryptr)
            {
                const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
                const uint8 bundletype = *(entryptr++);

                switch (bundletype)
                {
                    case NE_BUNDLE_UNUSED:
                        ordinal += numentries;
                        break;

                    case NE_BUNDLE_MOVABLE:
                        for (uint8 i = 0; i < numentries; i++)
                        {
                            const uint8 flags = *(entryptr++);
                            FIXME("deal with flags");
                            (void)flags;
                            entryptr += 2; /* INT 3Fh reload thunk */
                            const uint8 segment = (*(entryptr++)) - 1;
                            const uint16 offset = *((const uint16 *)entryptr);
                            entryptr += 2;
                            lxexp->ordinal = ordinal++;
                            lxexp->addr = ((uint8 *)retval->mmaps[segment].addr) + offset;
                            lxexp->object = &retval->mmaps[segment];
                            lxexp++;
                        }
                        break;

                    default:
                        for (uint8 i = 0; i < numentries; i++)
                        {
                            const uint8 segment = bundletype - 1;
                            const uint8 flags = *(entryptr++);
                            FIXME("deal with flags");
                            (void)flags;
                            const uint16 offset = *((const uint16 *)entryptr);
                            entryptr += 2;
                            lxexp->ordinal = ordinal++;
                            lxexp->addr = ((uint8 *)retval->mmaps[segment].addr) + offset;
                            lxexp->object = &retval->mmaps[segment];
                            lxexp++;
                        }
                        break;
                }
            }

            retval->num_exports = (uint32)(lxexp - retval->exports);
        }
    }

    if (ne->resident_name_table_offset)
    {
        if (!loadLxNameTable(retval, exe + ne->resident_name_table_offset))
            goto loadne_failed;
    }

    if (ne->non_resident_name_table_offset && ne->non_resident_name_table_size)
    {
        if (!loadLxNameTable(retval, origexe + ne->non_resident_name_table_offset))
            goto loadne_failed;
    }

    retval->os2path = lxHostPathToGuest(fname);
    if (!retval->os2path)
        goto loadne_failed;
    retval->loading = 1;
    registerLxModule(retval);

    const uint16 *import_modules_table = (const uint16 *)(exe + ne->module_reference_table_offset);
    for (uint32 i = 0; i < ne->num_module_ref_table_entries; i++)
    {
        const uint8 *nameptr = (exe + ne->imported_names_table_offset) + *(import_modules_table++);
        const uint8 namelen = *(nameptr++);
        char name[MAX_NAME_BUFFER];
        memcpy(name, nameptr, namelen);
        name[namelen] = '\0';
        retval->dependencies[i] = loadModuleByModuleNameInternal(name, dependency_tree_depth);
        if (!retval->dependencies[i])
        {
            fprintf(stderr, "Failed to load dependency '%s' for module '%s'\n", name, modname);
            goto loadne_failed;
        }
    }

    seg = (const NeSegmentTableEntry *)(exe + ne->segment_table_offset);
    for (uint32 i = 0; i < ne->num_segment_table_entries; i++, seg++)
    {
        if (!fixupNeSegment(seg, retval, (uint8 *)retval->mmaps[i].addr,
                            ne->sector_alignment_shift_count, origexe, exe, exelen))
        {
            goto loadne_failed;
        }

        const int iscode = ((seg->segment_flags & NSTYPE) == NSCODE) ? 1 : 0;
        /* NSPRELOAD only controls loading time; NSEXRD on data marks it read-only. */
        const int readonly = !iscode && (seg->segment_flags & NSEXRD);
        const int prot =
            (readonly ? 0 : LX_GUEST_WRITE) | (iscode ? LX_GUEST_EXECUTE : 0) | LX_GUEST_READ;
        retval->mmaps[i].prot = prot;

        if (!(retval->mmaps[i].mapping
                  ? lx_execution_protect(GProcessState.execution, retval->mmaps[i].mapping,
                                         (unsigned)prot)
                  : lx_host_vm_protect(retval->mmaps[i].mapped, retval->mmaps[i].size,
                                       (unsigned)prot)))
        {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s) failed (%d): %s\n", retval->mmaps[i].mapped,
                    (uint)retval->mmaps[i].size, (prot & LX_GUEST_READ) ? "R" : "-",
                    (prot & LX_GUEST_WRITE) ? "W" : "-", (prot & LX_GUEST_EXECUTE) ? "X" : "-",
                    errno, strerror(errno));
            goto loadne_failed;
        }
    }

    retval->loading = 0;

    return retval;

loadne_failed:
    if (retval)
    {
        retval->loading = 0;
        retval->load_failed = 1;
    }
    freeLxModule(retval);
    return NULL;
}
