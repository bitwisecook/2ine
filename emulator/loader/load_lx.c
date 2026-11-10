/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/loader/internal.h"

/* Loads LX modules: object mapping, page decompression, fixups, entry and name tables. */

enum
{
    /* A 16:16 alias selector covers at most one 64 KiB tile. */
    ALIAS16_TILE_SIZE = 0x10000,
    /* Deeper import chains than this are treated as circular. */
    MAX_DEPENDENCY_DEPTH = 32,
    /* Import-module names are length-prefixed with a byte below 128. */
    MAX_MODULE_NAME_LENGTH = 127,
    /* Fixup tracing is limited to this page-offset window to keep the log readable. */
    TRACE_FIXUP_FIRST_OFFSET = 0x700,
    TRACE_FIXUP_END_OFFSET = 0x800,
    /* EMX.DLL object 2 holds its guest signal installer at this offset; NOPs disable it. */
    EMX_SIGNAL_INSTALLER_OBJECT = 1,
    EMX_SIGNAL_INSTALLER_OFFSET = 28596,
    EMX_SIGNAL_INSTALLER_LENGTH = 37,
    X86_NOP = 0x90,
    /* HPI.DLL (Java) object 5 holds code but is not marked executable. */
    HPI_CODE_OBJECT = 4
};

int sanityCheckLxModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(LxHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const LxHeader *lx = (const LxHeader *)exe;
    if ((lx->byte_order != E32LEBO) || (lx->word_order != E32LEWO))
    {
        fprintf(stderr, "Module is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != E32LEVEL)
    {
        fprintf(stderr, "Module is unknown LX version (%u)\n", (uint)lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > E32CPU486)
    {
        fprintf(stderr, "Module needs unknown CPU type (%u)\n", (uint)lx->cpu_type);
        return 0;
    }

    if (lx->os_type != LX_OS_OS2)
    {
        fprintf(stderr, "Module needs unknown OS type (%u)\n", (uint)lx->os_type);
        return 0;
    }

    if (lx->page_size != LX_PAGE_SIZE)
    {
        fprintf(stderr, "Module page size isn't 4096 (%u)\n", (uint)lx->page_size);
        return 0;
    }

    if (lx->module_flags & E32NOLOAD)
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

static void fixupInternalChain(uint8 *page, sint16 offset, uint32 target)
{
    uint8 occupied[LX_PAGE_SIZE] = {0};
    uint32 current = (uint32)offset;
    uint32 base = 0;

    /* Validate the complete page-local chain before replacing its link words. */
    do
    {
        if (current > LX_PAGE_SIZE - sizeof(uint32) || occupied[current] || occupied[current + 1] ||
            occupied[current + 2] || occupied[current + 3])
        {
            fprintf(stderr, "Invalid LX internal fixup chain at page offset 0x%X\n", current);
            lx_host_fatal(LX_FATAL_LOAD_FAILURE, "A module contains invalid LX fixup data", NULL);
        }
        memset(occupied + current, 1, sizeof(uint32));
        uint32 link;
        memcpy(&link, page + current, sizeof(link));
        if (current == (uint32)offset)
            base = target - (link & LX_ICHAIN_TARGET_MASK);
        current = link >> LX_ICHAIN_NEXT_SHIFT;
    } while (current != LX_ICHAIN_END);

    current = (uint32)offset;
    do
    {
        uint32 link;
        memcpy(&link, page + current, sizeof(link));
        const uint32 value = base + (link & LX_ICHAIN_TARGET_MASK);
        memcpy(page + current, &value, sizeof(value));
        current = link >> LX_ICHAIN_NEXT_SHIFT;
    } while (current != LX_ICHAIN_END);
}

void fixupPage(const uint8 *exe, LxModule *lxmod, const LxObjectTableEntry *obj,
               const uint32 pagenum, uint8 *page)
{
    uint32 guest_page;
    if (!lx_execution_address(GProcessState.execution, page, 1, LX_GUEST_READ, &guest_page))
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
        const int internal_chain = (fixupflags & NRICHAIN) != 0;
        if (internal_chain &&
            (((srctype & (NRALIAS | NRSTYP)) != NROFF32) ||
             ((fixupflags & NRRTYP) != NRRINT && (fixupflags & NRRTYP) != NRRENT)))
        {
            fprintf(stderr, "Invalid LX internal fixup chain type\n");
            lx_host_fatal(LX_FATAL_LOAD_FAILURE, "A module contains invalid LX fixup data", NULL);
        }
        uint8 srclist_count = 0;
        sint16 srcoffset = 0;

        uint32 finalval = 0;
        uint16 finalval2 = 0;
        uint32 finalsize = 0;
        int allow_fixup_to_alias = 0;

        switch (srctype & NRSTYP)
        {
            case NRSBYT:
                finalsize = 1;
                break;
            case NRSSEG:
                finalsize = 2;
                allow_fixup_to_alias = 1;
                break;
            case NRSOFF:
                finalsize = 2;
                break;
            case NRSPTR:
                finalsize = 4;
                allow_fixup_to_alias = 1;
                break;
            case NROFF32:
            case NRSOFF32:
                finalsize = 4;
                break;
            case NRPTR48:
                finalval2 = lx_execution_flat_code_selector(GProcessState.execution);
                finalsize = 6;
                allow_fixup_to_alias = 1;
                break;
        }

        const int fixup_to_alias = ((srctype & NRALIAS) != 0);
        if (fixup_to_alias)
        {
            if (!allow_fixup_to_alias)
                fprintf(stderr, "WARNING: Bogus fixup srctype (%u) with fixup-to-alias flag!\n",
                        (uint)(srctype & NRSTYP));
        }

        if (srctype & NRCHAIN)
        {
            srclist_count = *(fixup++);
        }
        else
        {
            srcoffset = *((sint16 *)fixup);
            fixup += 2;
        }

        switch (fixupflags & NRRTYP)
        {
            case NRRINT:
            {
                uint16 objectid = 0;
                if (fixupflags & NR16OBJMOD)
                {
                    objectid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    objectid = (uint16) * (fixup++);
                }

                uint32 targetoffset = 0;

                /* Selector fixups name only the object, never an offset. */
                if ((srctype & NRSTYP) != NRSSEG)
                {
                    if (fixupflags & NR32BITOFF)
                    {
                        targetoffset = *((uint32 *)fixup);
                        fixup += 4;
                    }
                    else
                    {
                        targetoffset = (uint32)(*((uint16 *)fixup));
                        fixup += 2;
                    }
                }

                if (!objectid || objectid > lxmod->num_mmaps)
                {
                    fprintf(stderr, "Module '%s': invalid internal fixup object #%u\n", lxmod->name,
                            (unsigned)objectid);
                    lx_host_fatal(LX_FATAL_LOAD_FAILURE, "A module contains invalid LX fixup data",
                                  NULL);
                }
                const LxObjectTableEntry *objects =
                    (const LxObjectTableEntry *)(exe + lx->object_table_offset);
                const uint32 target_flags = objects[objectid - 1].object_flags;
                /* NOINTFIX images retain preferred linear offsets. A selector-only
                   reference to an unaliased USE32 object must preserve that base. */
                const int prebound_flat_selector =
                    (lx->module_flags & E32NOINTFIX) && !(lx->module_flags & E32NOTP) &&
                    (srctype & NRSTYP) == NRSSEG && (target_flags & OBJBIGDEF) &&
                    !(target_flags & (OBJALIAS16 | OBJRSRC));
                if (!fixup_to_alias || prebound_flat_selector)
                {
                    const uint32 base = lxmod->mmaps[objectid - 1].guest_base;
                    finalval = base + targetoffset;
                    const uint16 flat_selector =
                        (target_flags & OBJEXEC)
                            ? lx_execution_flat_code_selector(GProcessState.execution)
                            : lx_execution_flat_data_selector(GProcessState.execution);
                    if ((srctype & NRSTYP) == NRSSEG)
                        finalval = (target_flags & OBJBIGDEF)
                                       ? flat_selector
                                       : lxSelectorToSegment(lxmod->mmaps[objectid - 1].alias);
                    else if ((srctype & NRSTYP) == NRPTR48)
                        finalval2 = flat_selector;
                }
                else
                {
                    if (lxmod->mmaps[objectid - 1].alias == LX_NO_ALIAS)
                    {
                        fprintf(
                            stderr,
                            "Module '%s' object #%u page #%u offset %d: alias fixup "
                            "type 0x%02X targets object #%u offset 0x%X without a 16:16 alias\n",
                            lxmod->name,
                            (unsigned)(obj -
                                       (const LxObjectTableEntry *)(exe + lx->object_table_offset) +
                                       1),
                            (unsigned)pagenum, (int)srcoffset, (unsigned)srctype,
                            (unsigned)objectid, (unsigned)targetoffset);
                        lx_host_fatal(LX_FATAL_LOAD_FAILURE,
                                      "A module contains invalid LX fixup data", NULL);
                    }
                    else
                    {
                        const uint16 segment =
                            lxSelectorToSegment(lxmod->mmaps[objectid - 1].alias);
                        switch (srctype & NRSTYP)
                        {
                            case NRSSEG:
                                finalval = segment;
                                break;
                            case NRSPTR:
                                finalval = (((uint32)segment) << 16) | targetoffset;
                                break;
                            case NRPTR48:
                                finalval = targetoffset;
                                finalval2 = segment;
                                break;
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case NRRORD:
            {
                uint16 moduleid = 0;
                if (fixupflags & NR16OBJMOD)
                {
                    moduleid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    moduleid = (uint16) * (fixup++);
                }

                uint32 importid = 0;
                if (fixupflags & NR8BITORD)
                {
                    importid = (uint32) * (fixup++);
                }
                else if (fixupflags & NR32BITOFF)
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

                    void *entry = getModuleProcAddrByOrdinal(lxmod->dependencies[moduleid - 1],
                                                             importid, &lxexp, fixup_to_alias, 1);
                    finalval = lxexp && lxexp->indirect == LX_EXPORT_ABSOLUTE
                                   ? (uint32)(uintptr_t)entry
                                   : guestEntryAddress(entry);
                }

                if (fixup_to_alias)
                {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == LX_NO_ALIAS))
                    {
                        fprintf(stderr,
                                "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    }
                    else
                    {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & NRSTYP)
                        {
                            case NRSSEG:
                                finalval = segment;
                                break;
                            case NRSPTR:
                                finalval = (((uint32)segment) << 16) |
                                           (finalval - lxexp->object->guest_base);
                                break;
                            case NRPTR48:
                                finalval -= lxexp->object->guest_base;
                                finalval2 = segment;
                                break;
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case NRRNAM:
            {
                uint16 moduleid = 0;
                if (fixupflags & NR16OBJMOD)
                {
                    moduleid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    moduleid = (uint16) * (fixup++);
                }

                uint32 name_offset = 0;
                if (fixupflags & NR32BITOFF)
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
                    const uint8 namelen = *(import_name++) & LX_NAME_LENGTH_MASK;
                    memcpy(name, import_name, namelen);
                    name[namelen] = '\0';

                    void *entry = getModuleProcAddrByName(lxmod->dependencies[moduleid - 1], name,
                                                          &lxexp, fixup_to_alias);
                    finalval = lxexp && lxexp->indirect == LX_EXPORT_ABSOLUTE
                                   ? (uint32)(uintptr_t)entry
                                   : guestEntryAddress(entry);
                }

                if (fixup_to_alias)
                {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == LX_NO_ALIAS))
                    {
                        fprintf(stderr,
                                "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    }
                    else
                    {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & NRSTYP)
                        {
                            case NRSSEG:
                                finalval = segment;
                                break;
                            case NRSPTR:
                                finalval = (((uint32)segment) << 16) |
                                           (finalval - lxexp->object->guest_base);
                                break;
                            case NRPTR48:
                                finalval -= lxexp->object->guest_base;
                                finalval2 = segment;
                                break;
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case NRRENT:
            {
                FIXME("FIXUP 0x3 WRITE ME");
                assert(!"write me");
                lx_host_fatal(LX_FATAL_LOAD_FAILURE, "A module contains invalid LX fixup data",
                              NULL);
                break;
            }
        }

        if (getenv("TWOINE_TRACE_FIXUPS") && (srcoffset >= TRACE_FIXUP_FIRST_OFFSET) &&
            (srcoffset < TRACE_FIXUP_END_OFFSET))
            fprintf(stderr,
                    "2INE FIXREC: page=%p srctype=%X flags=%X srcoffset=%X final=%X size=%u\n",
                    page, (uint)srctype, (uint)fixupflags, (uint)(uint16)srcoffset, (uint)finalval,
                    (uint)finalsize);

        if (fixupflags & NRADD)
        {
            uint32 additive = 0;
            if (fixupflags & NR32BITADD)
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

        if (srctype & NRCHAIN)
        {
            for (uint8 i = 0; i < srclist_count; i++)
            {
                const sint16 offset = *((sint16 *)fixup);
                fixup += 2;
                if (internal_chain)
                {
                    fixupInternalChain(page, offset, finalval);
                }
                else if ((srctype & NRSTYP) == NRSOFF32)
                { /* Self-relative targets are measured from the end of the 4-byte field. */
                    assert(finalval2 == 0);
                    doFixup(page, offset, finalval - (guest_page + offset + sizeof(uint32)), 0,
                            finalsize);
                }
                else
                {
                    doFixup(page, offset, finalval, finalval2, finalsize);
                }
            }
        }
        else
        {
            if (internal_chain)
            {
                fixupInternalChain(page, srcoffset, finalval);
            }
            else if ((srctype & NRSTYP) == NRSOFF32)
            {
                assert(finalval2 == 0);
                doFixup(page, srcoffset, finalval - (guest_page + srcoffset + sizeof(uint32)), 0,
                        finalsize);
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
        for (uint32 i = 0; i < num_exports; i++, export++)
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

static int loadLxExports(LxModule *module, const uint8 *exe, uint32 length)
{
    const LxHeader *lx = &module->header.lx;
    const uint8 *end = exe + length;
    if (lx->entry_table_offset >= length)
        goto corrupt;
    const uint8 *table = exe + lx->entry_table_offset;
    uint32 count = 0;
    for (const uint8 *p = table;;)
    {
        if (p == end)
            goto corrupt;
        unsigned entries = *p++;
        if (!entries)
            break;
        if (p == end)
            goto corrupt;
        unsigned type = *p++ & ~LX_BUNDLE_TYPEINFO;
        if (type == LX_BUNDLE_EMPTY)
            continue;
        if (type == LX_BUNDLE_GATE16)
        {
            fprintf(stderr, "LX 286 call-gate exports are unsupported\n");
            return 0;
        }
        if (type != LX_BUNDLE_ENTRY16 && type != LX_BUNDLE_ENTRY32 && type != LX_BUNDLE_ENTRYFWD)
            goto corrupt;
        /* A non-empty bundle starts with a 16-bit object number. */
        size_t bytes = sizeof(uint16) + entries * (type == LX_BUNDLE_ENTRY16   ? FIXENT16
                                                   : type == LX_BUNDLE_ENTRY32 ? FIXENT32
                                                                               : FWDENT);
        if ((size_t)(end - p) < bytes)
            goto corrupt;
        p += bytes;
        count += entries;
    }
    if (!count)
        return 1;
    LxExport *exports = calloc(count, sizeof *exports);
    if (!exports)
        return 0;
    module->exports = exports;
    uint32 ordinal = 1;
    const uint8 *p = table;
    while (*p)
    {
        unsigned entries = *p++;
        unsigned type = *p++ & ~LX_BUNDLE_TYPEINFO;
        if (type == LX_BUNDLE_EMPTY)
        {
            ordinal += entries;
            continue;
        }
        unsigned object = lx_guest_load_u16(p);
        p += 2;
        if (type != LX_BUNDLE_ENTRYFWD && (!object || object > module->num_mmaps))
            goto corrupt;
        for (unsigned i = 0; i < entries; i++)
        {
            LxExport *entry = &exports[module->num_exports++];
            entry->ordinal = ordinal++;
            unsigned flags = *p++;
            if (type == LX_BUNDLE_ENTRYFWD)
            {
                unsigned dependency = lx_guest_load_u16(p);
                uint32 target = lx_guest_load_u32(p + 2);
                p += 6;
                if (object || (flags & ~(unsigned)FWD_ORDINAL) || !dependency ||
                    dependency > module->num_dependencies ||
                    lx->import_module_table_offset >= length)
                    goto corrupt;
                LxForwarder *forward = calloc(1, sizeof *forward);
                if (!forward)
                    return 0;
                entry->indirect = LX_EXPORT_FORWARDER;
                entry->addr = forward;
                forward->owner = module;
                forward->module_ordinal = dependency;
                const uint8 *name = exe + lx->import_module_table_offset;
                for (unsigned j = 1; j < dependency; j++)
                {
                    if (name == end || (size_t)(end - name - 1) < *name)
                        goto corrupt;
                    name += 1 + *name;
                }
                if (name == end || !*name || *name >= sizeof forward->module_name ||
                    (size_t)(end - name - 1) < *name)
                    goto corrupt;
                memcpy(forward->module_name, name + 1, *name);
                if (flags & FWD_ORDINAL)
                {
                    if (!target)
                        goto corrupt;
                    forward->ordinal = target;
                }
                else
                {
                    if (lx->import_proc_table_offset >= length ||
                        target >= length - lx->import_proc_table_offset)
                        goto corrupt;
                    name = exe + lx->import_proc_table_offset + target;
                    if (!*name || (size_t)(end - name - 1) < *name)
                        goto corrupt;
                    memcpy(forward->name, name + 1, *name);
                }
            }
            else
            {
                uint32 offset =
                    type == LX_BUNDLE_ENTRY16 ? lx_guest_load_u16(p) : lx_guest_load_u32(p);
                p += type == LX_BUNDLE_ENTRY16 ? sizeof(uint16) : sizeof(uint32);
                if (offset >= module->mmaps[object - 1].size)
                    goto corrupt;
                entry->addr = (uint8 *)module->mmaps[object - 1].addr + offset;
                entry->object = &module->mmaps[object - 1];
            }
        }
    }
    return 1;

corrupt:
    fprintf(stderr, "Invalid LX entry table in %s\n", module->name);
    return 0;
}

LxModule *loadLxModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen,
                       int dependency_tree_depth)
{
    LxModule *retval = NULL;
    const LxHeader *lx = (const LxHeader *)exe;
    const uint32 module_type = lx->module_flags & E32MODMASK;

    if ((module_type != E32MODEXE) && (module_type != E32MODDLL))
    {
        fprintf(stderr, "This is not a standard .exe or .dll (maybe device driver?)\n");
        goto loadlx_failed;
    }
    else if (lx->module_flags & E32NOLOAD)
    {
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadlx_failed;
    }

    const int isDLL = (module_type == E32MODDLL);

    if (lx->eip_object > lx->module_num_objects || lx->esp_object > lx->module_num_objects ||
        lx->auto_ds_object_num > lx->module_num_objects ||
        (!isDLL && (!lx->eip_object || !lx->esp_object)))
    {
        fprintf(stderr, "Invalid LX entry, stack or data object index\n");
        goto loadlx_failed;
    }

    if (isDLL && !GProcessState.main_module)
    {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadlx_failed;
    }
    else if (!isDLL && GProcessState.main_module)
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
        retval->mmaps[i].alias = LX_NO_ALIAS;

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
        goto loadlx_failed;
    }

    int uses_aliases = 0;
    const LxObjectTableEntry *obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
    {
        retval->mmaps[i].bits = (obj->object_flags & OBJBIGDEF) ? 32 : 16;
        const int relocatable = isDLL || (obj->object_flags & OBJRSRC);

        uint32 vsize = obj->virtual_size;
        if ((vsize % lx->page_size) != 0)
            vsize += lx->page_size - (vsize % lx->page_size);

        const int needs_1616_alias = ((obj->object_flags & OBJALIAS16) != 0);
        if (needs_1616_alias)
        {
            if (vsize > ALIAS16_TILE_SIZE)
            {
                fprintf(stderr, "Module '%s' object #%u needs a 16:16 alias, but is > 64k\n",
                        modname, (uint)(i + 1));
                goto loadlx_failed;
            }
            uses_aliases = 1;

            /* USE16 writable objects retain a full tile for DosReallocSeg growth. */
            if (!(obj->object_flags & OBJBIGDEF) && (obj->object_flags & OBJWRITE))
                vsize = ALIAS16_TILE_SIZE;

            /* Over-allocate so the object can be slid to a tile boundary below. */
            if (relocatable)
                vsize += ALIAS16_TILE_SIZE;
        }

        void *base = relocatable ? NULL : (void *)((size_t)obj->reloc_base_addr);
        LxGuestMapping *mapping =
            lx_execution_map(GProcessState.execution, (uint32)(uintptr_t)base, vsize,
                             LX_GUEST_READ | LX_GUEST_WRITE | LX_GUEST_EXECUTE, !relocatable);
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
        if (needs_1616_alias && (guest_mapping_base % ALIAS16_TILE_SIZE) != 0)
        {
            assert(relocatable);
            const uint32 diff = ALIAS16_TILE_SIZE - (guest_mapping_base % ALIAS16_TILE_SIZE);
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
                case LX_PAGE_VALID:
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    memcpy(dst, src, objpage->data_size);
                    if (objpage->data_size < lx->page_size)
                    {
                        memset(dst + objpage->data_size, '\0', lx->page_size - objpage->data_size);
                    }
                    break;

                case LX_PAGE_ITERDATA: /* OS/2 iterated LX pages use data_pages_offset as their data origin. */
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    if (!lx_exepack_iterated_decompress(dst, lx->page_size, src,
                                                        objpage->data_size))
                    {
                        fprintf(
                            stderr,
                            "Failed to decompress iterated object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    }
                    break;

                case LX_PAGE_INVALID:
                case LX_PAGE_ZEROED:
                    memset(dst, '\0', lx->page_size);
                    break;

                case LX_PAGE_ITERDATA2:
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    if (!lx_exepack2_decompress(dst, lx->page_size, src, objpage->data_size))
                    {
                        fprintf(stderr,
                                "Failed to decompress object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    }
                    break;

                case LX_PAGE_ITERDATA3:
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
        const LxObjectTableEntry *entry =
            (const LxObjectTableEntry *)(exe + lx->object_table_offset) + lx->eip_object - 1;
        retval->entry16 = !(entry->object_flags & OBJBIGDEF);
        if (lx->eip >= retval->mmaps[lx->eip_object - 1].size || (retval->entry16 && isDLL))
        {
            fprintf(stderr, "Invalid LX entry offset or unsupported USE16 library entry\n");
            goto loadlx_failed;
        }
    }

    if (!isDLL)
    {
        const LxMmaps *lxmmap = &retval->mmaps[lx->esp_object - 1];
        if (lx->esp > lxmmap->size)
        {
            fprintf(stderr, "Invalid LX initial stack offset\n");
            goto loadlx_failed;
        }
        const uint32 stackbase = lxmmap->guest_base;
        const uint32 stacksize = (uint32)lxmmap->size;
        retval->esp = lx->esp + stackbase;
        GProcessState.mainstacksize = stacksize;

        /* Library initialization requires the process stack to be installed. */
        GProcessState.initOs2Tib(GProcessState.main_tibspace, (uint8 *)lxmmap->addr + lx->esp,
                                 stacksize, 1);
        GProcessState.main_tib_selector = GProcessState.setOs2Tib(GProcessState.main_tibspace);
        lx_execution_stack_segments(GProcessState.execution, stackbase, stacksize, 0);
    }

    /* Install stack tiles before object aliases so startup selector numbers stay stable. */
    if (uses_aliases)
    {
        obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
        for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
        {
            if ((obj->object_flags & OBJALIAS16) == 0)
                continue;
            uint16 offset = 0;
            const int iscode = (obj->object_flags & OBJEXEC) ? 1 : 0;
            if (!lx_execution_find_selector(GProcessState.execution, retval->mmaps[i].guest_base,
                                            &retval->mmaps[i].alias, &offset, iscode))
            {
                fprintf(stderr,
                        "Ran out of LDT entries getting a 16:16 alias for '%s' object #%u!\n",
                        modname, (uint)(i + 1));
                goto loadlx_failed;
            }
            assert(offset == 0);

            if (obj->object_flags & OBJEXEC)
            {
                lx_execution_prepare_code(GProcessState.execution,
                                          (uint8 *)(size_t)retval->mmaps[i].mapped,
                                          retval->mmaps[i].size);
            }
        }
    }

    if (!loadLxExports(retval, exe, exelen))
        goto loadlx_failed;

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

    /* Publish exports before traversing dependencies, including circular imports. */
    retval->os2path = lxHostPathToGuest(fname);
    if (!retval->os2path)
        goto loadlx_failed;
    retval->loading = 1;
    registerLxModule(retval);

    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++)
    {
        const uint8 namelen = *(import_modules_table++);
        if (namelen > MAX_MODULE_NAME_LENGTH)
        {
            fprintf(stderr, "Import module %u name is > 127 chars (%u). Corrupt file or bug!\n",
                    (uint)(i + 1), (uint)namelen);
            goto loadlx_failed;
        }
        char name[MAX_MODULE_NAME_LENGTH + 1];
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

    if (!resolveLxForwarders(retval))
        goto loadlx_failed;

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

        if ((i == EMX_SIGNAL_INSTALLER_OBJECT) && (strcasecmp(modname, "EMX") == 0))
        {
            /* Suppress the guest signal installer; signal delivery belongs to the host worker. */
            uint8 *ptr = ((uint8 *)retval->mmaps[EMX_SIGNAL_INSTALLER_OBJECT].addr) +
                         EMX_SIGNAL_INSTALLER_OFFSET;
            for (uint32 j = 0; j < EMX_SIGNAL_INSTALLER_LENGTH; j++)
                *(ptr++) = X86_NOP;
        }

        uint32 object_flags = obj->object_flags;
        if ((i == HPI_CODE_OBJECT) && (strcasecmp(modname, "HPI") == 0))
        {
            fprintf(stderr, "fixed page permissions to workaround bug in Java DLL\n");
            object_flags |= OBJEXEC;
        }

        /* 386 readable pages are executable; some LX runtimes put callable code in READ-only objects. */
        int prot = ((object_flags & OBJREAD) ? LX_GUEST_READ : 0) |
                   ((object_flags & OBJWRITE) ? LX_GUEST_WRITE : 0) |
                   ((object_flags & OBJEXEC) ? LX_GUEST_EXECUTE : 0);
        if (prot & LX_GUEST_READ)
            prot |= LX_GUEST_EXECUTE;

        retval->mmaps[i].prot = prot;

        if (!(retval->mmaps[i].mapping
                  ? lx_execution_protect(GProcessState.execution, retval->mmaps[i].mapping,
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

    retval->loading = 0;

    return retval;

loadlx_failed:
    if (retval)
    {
        retval->loading = 0;
        retval->load_failed = 1;
    }
    freeLxModule(retval);
    return NULL;
}
