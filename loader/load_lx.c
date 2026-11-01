/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
int sanityCheckLxModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(LxHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const LxHeader *lx = (const LxHeader *)exe;
    if ((lx->byte_order != 0) || (lx->word_order != 0))
    {
        fprintf(stderr, "Module is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != 0)
    {
        fprintf(stderr, "Module is unknown LX version (%u)\n", (uint)lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > 3)
    { // 1==286, 2==386, 3==486
        fprintf(stderr, "Module needs unknown CPU type (%u)\n", (uint)lx->cpu_type);
        return 0;
    }

    if (lx->os_type != 1)
    { // 1==OS/2, others: dos4, windows, win386, unknown.
        fprintf(stderr, "Module needs unknown OS type (%u)\n", (uint)lx->os_type);
        return 0;
    }

    if (lx->page_size != 4096)
    {
        fprintf(stderr, "Module page size isn't 4096 (%u)\n", (uint)lx->page_size);
        return 0;
    }

    if (lx->module_flags & 0x2000)
    {
        fprintf(stderr, "Module is flagged as not-loadable\n");
        return 0;
    }

    return 1;
}

void doFixup(uint8 *page, const sint16 offset, const uint32 finalval, const uint16 finalval2,
             const uint32 finalsize)
{
#if 0
    if (finalsize == 6) {
        printf("fixing up %p to 0x%X:0x%X (6 bytes)...\n", page + offset, (uint) finalval2, (uint) finalval);
    } else {
        printf("fixing up %p to 0x%X (%u bytes)...\n", page + offset, (uint) finalval, (uint) finalsize);
    }
    fflush(stdout);
#endif

    switch (finalsize)
    {
        case 1:
        {
            uint8 *dst = (uint8 *)(page + offset);
            *dst = (uint8)finalval;
        }
        break;
        case 2:
        {
            uint16 *dst = (uint16 *)(page + offset);
            *dst = (uint16)finalval;
        }
        break;
        case 4:
        {
            uint32 *dst = (uint32 *)(page + offset);
            *dst = (uint32)finalval;
        }
        break;
        case 6:
        {
            uint32 *dst1 = (uint32 *)(page + offset);
            *dst1 = (uint32)finalval;
            uint16 *dst2 = (uint16 *)(page + offset + 4);
            *dst2 = (uint16)finalval2;
            break;
        }
        default:
            fprintf(stderr, "BUG! Unexpected fixup final size of %u\n", (uint)finalsize);
            exit(1);
    }
}

void fixupPage(const uint8 *exe, LxModule *lxmod, const LxObjectTableEntry *obj,
               const uint32 pagenum, uint8 *page)
{
    uint32 guest_page;
    if (!lx_execution_address(GLoaderState.execution, page, 1, LX_GUEST_READ, &guest_page))
        abort();

    assert(lxmod->is_lx);
    const LxHeader *lx = &lxmod->header.lx;

    const uint32 *fixuppage = (((const uint32 *)(exe + lx->fixup_page_table_offset)) +
                               ((obj->page_table_index - 1) + pagenum));
    const uint32 fixupoffset = *fixuppage;
    const uint32 fixuplen = fixuppage[1] - fixuppage[0];
    const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
    const uint8 *fixupend = fixup + fixuplen;

    while (fixup < fixupend)
    {

        const uint8 srctype = *(fixup++);
        const uint8 fixupflags = *(fixup++);
        uint8 srclist_count = 0;
        sint16 srcoffset = 0;

        uint32 finalval = 0;
        uint16 finalval2 = 0;
        uint32 finalsize = 0;
        int allow_fixup_to_alias = 0;

        switch (srctype & 0xF)
        {
            case 0x0: // byte fixup
                finalsize = 1;
                break;
            case 0x2: // 16-bit selector fixup
                finalsize = 2;
                allow_fixup_to_alias = 1;
                break;
            case 0x5: // 16-bit offset fixup
                finalsize = 2;
                break;
            case 0x3: // 16:16 pointer fixup
                finalsize = 4;
                allow_fixup_to_alias = 1;
                break;
            case 0x7: // 32-bit offset fixup
            case 0x8: // 32-bit self-relative offset fixup
                finalsize = 4;
                break;
            case 0x6: // 16:32 pointer fixup
                finalval2 = lx_execution_flat_code_selector(GLoaderState.execution);
                finalsize = 6;
                allow_fixup_to_alias = 1;
                break;
        }

        const int fixup_to_alias = ((srctype & 0x10) != 0);
        if (fixup_to_alias)
        { // fixup to alias flag.
            if (!allow_fixup_to_alias)
                fprintf(stderr, "WARNING: Bogus fixup srctype (%u) with fixup-to-alias flag!\n",
                        (uint)(srctype & 0xF));
        }

        if (srctype & 0x20)
        { // contains a source list
            srclist_count = *(fixup++);
        }
        else
        {
            srcoffset = *((sint16 *)fixup);
            fixup += 2;
        }

        switch (fixupflags & 0x3)
        {
            case 0x0:
            { // Internal fixup record
                uint16 objectid = 0;
                if (fixupflags & 0x40)
                {
                    objectid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    objectid = (uint16) * (fixup++);
                }

                uint32 targetoffset = 0;

                if ((srctype & 0xF) != 2)
                { // not used for 16-bit selector fixups.
                    if (fixupflags & 0x10)
                    { // 32-bit target offset
                        targetoffset = *((uint32 *)fixup);
                        fixup += 4;
                    }
                    else
                    { // 16-bit target offset
                        targetoffset = (uint32)(*((uint16 *)fixup));
                        fixup += 2;
                    }
                }

                if (!fixup_to_alias)
                {
                    const uint32 base = lxmod->mmaps[objectid - 1].guest_base;
                    finalval = base + targetoffset;
                }
                else
                {
                    if (lxmod->mmaps[objectid - 1].alias == 0xFFFF)
                    {
                        fprintf(
                            stderr,
                            "uhoh, need a 16-bit alias fixup, but object has no 16:16 alias!\n");
                    }
                    else
                    {
                        const uint16 segment =
                            lxSelectorToSegment(lxmod->mmaps[objectid - 1].alias);
                        switch (srctype & 0xF)
                        {
                            case 0x2:
                                finalval = segment;
                                break; // 16-bit selector fixup?
                            case 0x3:
                                finalval = (((uint32)segment) << 16) | targetoffset;
                                break; // 16:16 pointer fixup
                            case 0x6:
                                finalval = targetoffset;
                                finalval2 = segment;
                                break; // 16:32 pointer fixup
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case 0x1:
            {                        // Import by ordinal fixup record
                uint16 moduleid = 0; // module ordinal
                if (fixupflags & 0x40)
                {
                    moduleid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    moduleid = (uint16) * (fixup++);
                }

                uint32 importid = 0; // import ordinal
                if (fixupflags & 0x80)
                {
                    importid = (uint32) * (fixup++);
                }
                else if (fixupflags & 0x10)
                {
                    importid = *((uint32 *)fixup);
                    fixup += 4;
                }
                else
                {
                    importid = (uint32) * ((uint16 *)fixup);
                    fixup += 2;
                }

                const LxExport *lxexp = NULL;
                if (getenv("TWOINE_TRACE_FIXUPS"))
                    fprintf(stderr, "2INE FIXORD: mod=%u ord=%u srcoff=%X\n", (uint)moduleid,
                            (uint)importid, (uint)(uint16)srcoffset);
                if (moduleid == 0)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                }
                else if (moduleid > lx->num_import_mod_entries)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n",
                            (uint)moduleid, (uint)lx->num_import_mod_entries);
                }
                else
                {

                    finalval = guestEntryAddress(getModuleProcAddrByOrdinal(
                        lxmod->dependencies[moduleid - 1], importid, &lxexp, fixup_to_alias, 1));
                }

                if (fixup_to_alias)
                {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == 0xFFFF))
                    {
                        fprintf(stderr,
                                "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    }
                    else
                    {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & 0xF)
                        {
                            case 0x2:
                                finalval = segment;
                                break;
                            case 0x3:
                                finalval = (((uint32)segment) << 16) |
                                           (finalval - lxexp->object->guest_base);
                                break; // 16:16 pointer fixup
                            case 0x6:
                                finalval -= lxexp->object->guest_base;
                                finalval2 = segment;
                                break; // 16:32 pointer fixup
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case 0x2:
            {                        // Import by name fixup record
                uint16 moduleid = 0; // module ordinal
                if (fixupflags & 0x40)
                {
                    moduleid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    moduleid = (uint16) * (fixup++);
                }

                uint32 name_offset = 0;
                if (fixupflags & 0x10)
                {
                    name_offset = *((uint32 *)fixup);
                    fixup += 4;
                }
                else
                {
                    name_offset = *((uint16 *)fixup);
                    fixup += 2;
                }

                const LxExport *lxexp = NULL;
                if (moduleid == 0)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                }
                else if (moduleid > lx->num_import_mod_entries)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n",
                            (uint)moduleid, (uint)lx->num_import_mod_entries);
                }
                else
                {
                    const uint8 *import_name = (exe + lx->import_proc_table_offset) + name_offset;
                    char name[128];
                    const uint8 namelen = *(import_name++) & 0x7F; // the top bit is reserved.
                    memcpy(name, import_name, namelen);
                    name[namelen] = '\0';

                    finalval = guestEntryAddress(getModuleProcAddrByName(
                        lxmod->dependencies[moduleid - 1], name, &lxexp, fixup_to_alias));
                }

                if (fixup_to_alias)
                {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == 0xFFFF))
                    {
                        fprintf(stderr,
                                "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    }
                    else
                    {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & 0xF)
                        {
                            case 0x2:
                                finalval = segment;
                                break;
                            case 0x3:
                                finalval = (((uint32)segment) << 16) |
                                           (finalval - lxexp->object->guest_base);
                                break; // 16:16 pointer fixup
                            case 0x6:
                                finalval -= lxexp->object->guest_base;
                                finalval2 = segment;
                                break; // 16:32 pointer fixup
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case 0x3:
            { // Internal entry table fixup record
                FIXME("FIXUP 0x3 WRITE ME");
                assert(!"write me");
                lx_host_process_exit(1);
                break;
            }
        }

        if (getenv("TWOINE_TRACE_FIXUPS") && (srcoffset >= 0x700) && (srcoffset < 0x800))
            fprintf(stderr,
                    "2INE FIXREC: page=%p srctype=%X flags=%X srcoffset=%X final=%X size=%u\n",
                    page, (uint)srctype, (uint)fixupflags, (uint)(uint16)srcoffset, (uint)finalval,
                    (uint)finalsize);

        if (fixupflags & 0x4)
        { // Has additive.
            uint32 additive = 0;
            if (fixupflags & 0x20)
            {
                additive = *((uint32 *)fixup);
                fixup += 4;
            }
            else
            {
                additive = (uint32) * ((uint16 *)fixup);
                fixup += 2;
            }
            finalval += additive;
        }

        if (srctype & 0x20)
        { // source list
            for (uint8 i = 0; i < srclist_count; i++)
            {
                const sint16 offset = *((sint16 *)fixup);
                fixup += 2;
                if ((srctype & 0xF) == 0x08)
                { // self-relative fixup?
                    assert(finalval2 == 0);
                    doFixup(page, offset, finalval - (guest_page + offset + 4), 0, finalsize);
                }
                else
                {
                    doFixup(page, offset, finalval, finalval2, finalsize);
                }
            }
        }
        else
        {
            if ((srctype & 0xF) == 0x08)
            { // self-relative fixup?
                assert(finalval2 == 0);
                doFixup(page, srcoffset, finalval - (guest_page + srcoffset + 4), 0, finalsize);
            }
            else
            {
                doFixup(page, srcoffset, finalval, finalval2, finalsize);
            }
        }
    }
}

int loadLxNameTable(LxModule *lxmod, const uint8 *name_table)
{
    for (uint32 i = 0; *name_table; i++)
    {
        const uint8 namelen = *(name_table++);
        const uint8 *nameptr = name_table;
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *)name_table);
        name_table += 2;

        if (ordinal == 0)
            continue; // skip this one.

        LxExport *export = (LxExport *)lxmod->exports;
        const uint32 num_exports = lxmod->num_exports;
        for (uint32 i = 0; i < num_exports; i++, export ++)
        {
            if (export->ordinal == ordinal)
            {
                char *name = (char *)malloc(namelen + 1);
                if (!name)
                {
                    fprintf(stderr, "Out of memory!\n");
                    return 0;
                }
                memcpy(name, nameptr, namelen);
                name[namelen] = '\0';
                export->name = name;
                break;
            }
        }
    }

    return 1;
}

LxModule *loadLxModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen,
                       int dependency_tree_depth)
{
    LxModule *retval = NULL;
    const LxHeader *lx = (const LxHeader *)exe;
    const uint32 module_type = lx->module_flags & 0x00038000;

    if ((module_type != 0x0000) && (module_type != 0x8000))
    {
        fprintf(stderr, "This is not a standard .exe or .dll (maybe device driver?)\n");
        goto loadlx_failed;
    }
    else if (lx->module_flags & 0x2000)
    {
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadlx_failed;
    }

    const int isDLL = (module_type == 0x8000);

    if (isDLL && !GLoaderState.main_module)
    {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadlx_failed;
    }
    else if (!isDLL && GLoaderState.main_module)
    {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadlx_failed;
    }

    retval = (LxModule *)malloc(sizeof(LxModule));
    if (!retval)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    }
    memset(retval, '\0', sizeof(*retval));
    retval->is_lx = 1;
    retval->refcount = 1;
    retval->handle = lx_handle_create(LX_HANDLE_MODULE, retval, destroyLxModule);
    if (!retval->handle)
    {
        fprintf(stderr, "Cannot allocate module handle\n");
        goto loadlx_failed;
    }
    retval->dependencies = (LxModule **)malloc(sizeof(LxModule *) * lx->num_import_mod_entries);
    retval->mmaps = (LxMmaps *)malloc(sizeof(LxMmaps) * lx->module_num_objects);
    if (!retval->dependencies || !retval->mmaps)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    }
    memset(retval->dependencies, '\0', sizeof(LxModule *) * lx->num_import_mod_entries);
    memset(retval->mmaps, '\0', sizeof(LxMmaps) * lx->module_num_objects);
    retval->num_dependencies = lx->num_import_mod_entries;
    retval->num_mmaps = lx->module_num_objects;
    for (int i = 0; i < retval->num_mmaps; i++)
        retval->mmaps[i].alias = 0xFFFF;

    memcpy(&retval->header.lx, lx, sizeof(*lx));

    if (lx->resident_name_table_offset)
    {
        const uint8 *name_table = exe + lx->resident_name_table_offset;
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
        GLoaderState.main_module = retval;
        GLoaderState.pib.pib_hmte = retval->handle;
        if (GLoaderState.guest_pib)
        {
            LxGuestPib *pib = lx_execution_mapping_host(GLoaderState.guest_pib);
            lx_guest_store_u32(&pib->module, retval->handle);
        }
    }

    const char *modname = retval->name;

    if (++dependency_tree_depth > 32)
    {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
        goto loadlx_failed;
    }

    int uses_aliases = 0;
    const LxObjectTableEntry *obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
    {
        const int relocatable = isDLL || (obj->object_flags & 0x8);

        uint32 vsize = obj->virtual_size;
        if ((vsize % lx->page_size) != 0)
            vsize += lx->page_size - (vsize % lx->page_size);

        const int needs_1616_alias =
            ((obj->object_flags & 0x1000) != 0); // Object needs a 16:16 alias.
        if (needs_1616_alias)
        {
            if (vsize > 0x10000)
            {
                fprintf(stderr, "Module '%s' object #%u needs a 16:16 alias, but is > 64k\n",
                        modname, (uint)(i + 1));
                goto loadlx_failed;
            }
            uses_aliases = 1;

            if (relocatable)
                vsize += 0x10000;
        }

        void *base = relocatable ? NULL : (void *)((size_t)obj->reloc_base_addr);
        LxGuestMapping *mapping =
            lx_execution_map(GLoaderState.execution, (uint32)(uintptr_t)base, vsize,
                             LX_GUEST_READ | LX_GUEST_WRITE, !relocatable);
        void *mmapaddr = mapping ? lx_execution_mapping_host(mapping) : NULL;

        if (!mmapaddr)
        {
            fprintf(stderr, "mmap(%p, %u, RW-, ANON|PRIVATE%s, -1, 0) failed (%d): %s\n", base,
                    (uint)vsize, isDLL ? "" : "|FIXED", errno, strerror(errno));
            goto loadlx_failed;
        }

        retval->mmaps[i].mapping = mapping;
        retval->mmaps[i].mapped = mmapaddr;
        retval->mmaps[i].size = vsize;

        const uint32 guest_mapping_base = lx_execution_mapping_address(mapping);
        if (needs_1616_alias && (guest_mapping_base % 0x10000) != 0)
        {
            assert(relocatable);
            const uint32 diff = 0x10000 - (guest_mapping_base % 0x10000);
            vsize -= diff;
            mmapaddr = (uint8 *)mmapaddr + diff;
        }

        FIXME("Can we just unmap the adjusted pieces?");
        retval->mmaps[i].addr = mmapaddr;
        retval->mmaps[i].guest_base =
            lx_execution_mapping_address(mapping) +
            (uint32)((uint8 *)mmapaddr - (uint8 *)lx_execution_mapping_host(mapping));

        const LxObjectPageTableEntry *objpage =
            ((const LxObjectPageTableEntry *)(exe + lx->object_page_table_offset));
        objpage += obj->page_table_index - 1;

        uint8 *dst = (uint8 *)mmapaddr;
        const uint32 numPageTableEntries = obj->num_page_table_entries;
        for (uint32 pagenum = 0; pagenum < numPageTableEntries; pagenum++, objpage++)
        {
            const uint8 *src;
            switch (objpage->flags)
            {
                case 0x00:
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    memcpy(dst, src, objpage->data_size);
                    if (objpage->data_size < lx->page_size)
                    {
                        memset(dst + objpage->data_size, '\0', lx->page_size - objpage->data_size);
                    }
                    break;

                case 0x01: /* OS/2 iterated LX pages use data_pages_offset as their data origin. */
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressIterated(dst, lx->page_size, src, objpage->data_size))
                    {
                        fprintf(
                            stderr,
                            "Failed to decompress iterated object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    }
                    break;

                case 0x02: // INVALID, just zero it out.
                case 0x03: // ZEROFILL, just zero it out.
                    memset(dst, '\0', lx->page_size);
                    break;

                case 0x05:
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressExePack2(dst, lx->page_size, src, objpage->data_size))
                    {
                        fprintf(stderr,
                                "Failed to decompress object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    }
                    break;

                case 0x08:
                default:
                    fprintf(stderr, "Don't know how to load an object page of type %u!\n",
                            (uint)objpage->flags);
                    goto loadlx_failed;
            }

            dst += lx->page_size;
        }

        const uint32 remain = vsize - ((uint32)(dst - ((uint8 *)mmapaddr)));
        if (remain)
            memset(dst, '\0', remain);
    }

    retval->eip = lx->eip;
    if (lx->eip_object != 0)
    {
        const uint32 base = retval->mmaps[lx->eip_object - 1].guest_base;
        retval->eip += base;
    }

    if (!isDLL)
    {
        assert(lx->esp_object != 0);
        const LxMmaps *lxmmap = &retval->mmaps[lx->esp_object - 1];
        const uint32 stackbase = lxmmap->guest_base;
        const uint32 stacksize = (uint32)lxmmap->size;
        retval->esp = lx->esp + stackbase;
        GLoaderState.mainstacksize = stacksize;

        /* Library initialization requires the process stack to be installed. */
        GLoaderState.initOs2Tib(GLoaderState.main_tibspace, (uint8 *)lxmmap->addr + lx->esp,
                                stacksize, 1);
        GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);
        initOs2StackSegments(stackbase, stacksize, 0);
    }

    /* Install stack tiles before object aliases so startup selector numbers stay stable. */
    if (uses_aliases)
    {
        obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
        for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
        {
            if ((obj->object_flags & 0x1000) == 0)
                continue;
            uint16 offset = 0;
            const int iscode = (obj->object_flags & 0x4) ? 1 : 0;
            if (!lxFindSelector(retval->mmaps[i].guest_base, &retval->mmaps[i].alias, &offset,
                                iscode))
            {
                fprintf(stderr,
                        "Ran out of LDT entries getting a 16:16 alias for '%s' object #%u!\n",
                        modname, (uint)(i + 1));
                goto loadlx_failed;
            }
            assert(offset == 0);

            if (obj->object_flags & 0x4)
            { // 16:16 segment with code.
                fixupLinearCodeSegmentReferences((uint8 *)(size_t)retval->mmaps[i].mapped,
                                                 retval->mmaps[i].size);
            }
        }
    }

    uint32 total_ordinals = 0;
    const uint8 *entryptr = exe + lx->entry_table_offset;
    while (*entryptr)
    {
        const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
        const uint8 bundletype = *(entryptr++) & ~0x80;
        if (bundletype != 0x00)
        {
            total_ordinals += numentries;
            entryptr += 2 + ((bundletype == 0x01) ? 3 : 5) * numentries;
        }
    }

    LxExport *lxexp = (LxExport *)calloc(1, sizeof(LxExport) * total_ordinals);
    if (!lxexp)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    }
    memset(lxexp, '\0', sizeof(LxExport) * total_ordinals);
    retval->exports = lxexp;

    uint32 ordinal = 1;
    entryptr = exe + lx->entry_table_offset;
    while (*entryptr)
    {
        const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
        const uint8 bundletype = *(entryptr++) & ~0x80;
        uint16 objidx = 0;

        switch (bundletype)
        {
            case 0x00:
                ordinal += numentries;
                break;

            case 0x01:
                objidx = *((const uint16 *)entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    entryptr++;
                    lxexp->ordinal = ordinal++;
                    lxexp->addr =
                        ((uint8 *)retval->mmaps[objidx].addr) + *((const uint16 *)entryptr);
                    lxexp->object = &retval->mmaps[objidx];
                    lxexp++;
                    entryptr += 2;
                }
                break;

            case 0x03:
                objidx = *((const uint16 *)entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    entryptr++;
                    lxexp->ordinal = ordinal++;
                    lxexp->addr =
                        ((uint8 *)retval->mmaps[objidx].addr) + *((const uint32 *)entryptr);
                    lxexp->object = &retval->mmaps[objidx];
                    lxexp++;
                    entryptr += 4;
                }
                break;

            case 0x02:
            case 0x04:
                fprintf(stderr, "WRITE ME %s:%d\n", __FILE__, __LINE__);
                goto loadlx_failed;

            default:
                fprintf(stderr, "UNKNOWN ENTRY TYPE (%u)\n\n", (uint)bundletype);
                goto loadlx_failed;
        }
    }

    retval->num_exports = (uint32)(lxexp - retval->exports);

    if (lx->resident_name_table_offset)
    {
        if (!loadLxNameTable(retval, exe + lx->resident_name_table_offset))
            goto loadlx_failed;
    }

    if (lx->non_resident_name_table_offset && lx->non_resident_name_table_len)
    {
        if (!loadLxNameTable(retval, origexe + lx->non_resident_name_table_offset))
            goto loadlx_failed;
    }

    if (lx->num_resource_table_entries)
    {
        const uint32 offset = lx->resource_table_offset;
        const uint32 count = lx->num_resource_table_entries;
        if (offset > exelen || count > (exelen - offset) / sizeof(LxResourceTableEntry))
        {
            fprintf(stderr, "Resource table exceeds module bounds\n");
            goto loadlx_failed;
        }
        retval->resources = malloc(count * sizeof *retval->resources);
        if (!retval->resources)
            goto loadlx_failed;
        memcpy(retval->resources, exe + offset, count * sizeof *retval->resources);
        retval->num_resources = count;
        for (uint32 i = 0; i < count; i++)
        {
            const LxResourceTableEntry *r = &retval->resources[i];
            if (!r->object || r->object > retval->num_mmaps ||
                r->offset > retval->mmaps[r->object - 1].size ||
                r->resource_size > retval->mmaps[r->object - 1].size - r->offset)
            {
                fprintf(stderr, "Resource extent exceeds module object\n");
                goto loadlx_failed;
            }
        }
    }

    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++)
    {
        const uint8 namelen = *(import_modules_table++);
        if (namelen > 127)
        {
            fprintf(stderr, "Import module %u name is > 127 chars (%u). Corrupt file or bug!\n",
                    (uint)(i + 1), (uint)namelen);
            goto loadlx_failed;
        }
        char name[128];
        memcpy(name, import_modules_table, namelen);
        import_modules_table += namelen;
        name[namelen] = '\0';
        retval->dependencies[i] = loadModuleByModuleNameInternal(name, dependency_tree_depth);
        if (!retval->dependencies[i])
        {
            fprintf(stderr, "Failed to load dependency '%s' for module '%s'\n", name, modname);
            goto loadlx_failed;
        }
    }

    obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
    {
        uint8 *dst = (uint8 *)retval->mmaps[i].addr;
        const uint32 numPageTableEntries = obj->num_page_table_entries;
        for (uint32 pagenum = 0; pagenum < numPageTableEntries; pagenum++)
        {
            fixupPage(exe, retval, obj, pagenum, dst);
            dst += lx->page_size;
        }

        if ((i == 1) && (strcasecmp(modname, "EMX") == 0))
        {
            /* Suppress the guest signal installer; signal delivery belongs to the host worker. */
            uint8 *ptr = ((uint8 *)retval->mmaps[1].addr) + 28596;
            for (uint32 j = 0; j < 37; j++)
                *(ptr++) = 0x90;
        }

        uint32 object_flags = obj->object_flags;
        if ((i == 4) && (strcasecmp(modname, "HPI") == 0))
        {
            fprintf(stderr, "fixed page permissions to workaround bug in Java DLL\n");
            object_flags |= 0x4;
        }

        /* 386 readable pages are executable; some LX runtimes put callable code in READ-only objects. */
        int prot = ((object_flags & 0x1) ? LX_GUEST_READ : 0) |
                   ((object_flags & 0x2) ? LX_GUEST_WRITE : 0) |
                   ((object_flags & 0x4) ? LX_GUEST_EXECUTE : 0);
        if (prot & LX_GUEST_READ)
            prot |= LX_GUEST_EXECUTE;

        retval->mmaps[i].prot = prot;

        if (!(retval->mmaps[i].mapping
                  ? lx_execution_protect(GLoaderState.execution, retval->mmaps[i].mapping,
                                         (unsigned)prot)
                  : lx_host_vm_protect(retval->mmaps[i].mapped, retval->mmaps[i].size,
                                       (unsigned)prot)))
        {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s, ANON|PRIVATE|FIXED, -1, 0) failed (%d): %s\n",
                    retval->mmaps[i].addr, (uint)retval->mmaps[i].size,
                    (prot & LX_GUEST_READ) ? "R" : "-", (prot & LX_GUEST_WRITE) ? "W" : "-",
                    (prot & LX_GUEST_EXECUTE) ? "X" : "-", errno, strerror(errno));
            goto loadlx_failed;
        }
    }

    dumpLoadedModule(retval);

    retval->os2path = makeOS2Path(fname);
    if (!retval->os2path)
        goto loadlx_failed;

    if (!isDLL)
    {
        retval->initialized = 1;
    }
    else
    {

        assert(GLoaderState.main_module != NULL);
        assert(GLoaderState.main_module != retval);
        runLxLibraryInit(retval);

        retval->initialized = 1;

        if (GLoaderState.loaded_modules)
        {
            retval->next = GLoaderState.loaded_modules;
            GLoaderState.loaded_modules->prev = retval;
        }
        GLoaderState.loaded_modules = retval;
    }

    return retval;

loadlx_failed:
    freeLxModule(retval);
    return NULL;
}
