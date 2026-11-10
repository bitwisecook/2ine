/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* lx_dump: prints the headers, objects, fixups and tables of an LX or NE executable. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "formats/executable/lx.h"
#include "formats/executable/ne.h"
#include "emulator/diagnostics.h"

/* Executable format values, by their OS/2 Toolkit names (exe386.h and
   newexe.h) where the Toolkit has one. */
enum
{
    /* DOS stub: e_lfanew, the offset of the new-format header. */
    DOS_HEADER_NEW_EXE_OFFSET = 0x3C,
    EXE_HEADER_MINIMUM = 62,

    /* LX header. */
    OBJPAGELEN = 4096,
    /* LX module flags. */
    E32LIBINIT = 0x0004,
    E32NOEXTFIX = 0x0020,
    E32NOPMW = 0x0100,
    E32PMW = 0x0200,
    E32PMAPI = 0x0300,
    E32MODPROTDLL = 0x18000,
    E32MODPDEV = 0x20000,
    E32MODVDEV = 0x28000,
    E32LIBTERM = 0x40000000,
    /* LX object flags. */
    OBJDISCARD = 0x0010,
    OBJSHARED = 0x0020,
    OBJPRELOAD = 0x0040,
    OBJINVALID = 0x0080,
    OBJPERM = 0x0100,
    OBJRESIDENT = 0x0200,
    OBJCONTIG = 0x0300,
    OBJDYNAMIC = 0x0400,
    OBJRESERVED = 0x0800,
    OBJCONFORM = 0x4000,
    OBJIOPL = 0x8000,
    /* LX object page flags. */
    VALID = 0x0000,
    ITERDATA = 0x0001,
    INVALID = 0x0002,
    ZEROED = 0x0003,
    RANGE = 0x0004,
    ITERDATA2 = 0x0005,
    /* Length-prefixed names keep their length in the low seven bits. */
    NAME_LENGTH_MASK = 0x7F,
    /* LX entry table bundles. */
    EMPTY = 0x00,
    ENTRY16 = 0x01,
    GATE16 = 0x02,
    ENTRY32 = 0x03,
    ENTRYFWD = 0x04,
    TYPEINFO = 0x80,
    E32EXPORT = 0x01,
    E32SHARED = 0x02,
    E32PARAMS = 0xf8,
    E32PARAMS_SHIFT = 3,

    /* NE header. */
    NESOLO = 0x0001,
    NEINST = 0x0002,
    NEPPLI = 0x0004,
    NENOTMPSAFE = 0x4000,
    NE_APPTYPE_SHIFT = 8,
    NE_APPTYPE_BITS = 0x3,
    NELONGNAMES = 0x01,
    NEWINISPROT = 0x02,
    NEWINGETPROPFON = 0x04,
    NE_GANGLOAD_AREA = 0x08,
    /* NE segment table: a zero size or allocation means 64 KiB. */
    NE_SEGMENT_64K = 0x10000,
    NE_SEGMENT_TYPE_BITS = 0x3,
    NSDATA = 0x0001,
    NE_SEGMENT_REALMODE = 0x0004,
    NSMOVE = 0x0010,
    NSSHARED = 0x0020,
    NE_SEGMENT_DEBUGINFO = 0x0200,
    NE_SEGMENT_DISCARD_PRIORITY = 0xF000,
    NE_DISCARD_PRIORITY_SHIFT = 13,
    NE_DISCARD_PRIORITY_BITS = 0x7,
    /* An internal fixup to segment 0xFF names a movable entry by ordinal. */
    /* Resource type and name ids with the high bit set are integers. */
    NE_RESOURCE_INTEGER_ID = 0x8000,
};

static int sanityCheckLxExe(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(LxHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const LxHeader *lx = (const LxHeader *)exe;
    if ((lx->byte_order != E32LEBO) || (lx->word_order != E32LEWO))
    {
        fprintf(stderr, "Program is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != E32LEVEL)
    {
        fprintf(stderr, "Program is unknown LX EXE version (%u)\n", (uint)lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > E32CPU486)
    {
        fprintf(stderr, "Program needs unknown CPU type (%u)\n", (uint)lx->cpu_type);
        return 0;
    }

    if (lx->os_type != NE_OS2)
    {
        fprintf(stderr, "Program needs unknown OS type (%u)\n", (uint)lx->os_type);
        return 0;
    }

    if (lx->page_size != OBJPAGELEN)
    {
        fprintf(stderr, "Program page size isn't 4096 (%u)\n", (uint)lx->page_size);
        return 0;
    }

    return 1;
}

static int sanityCheckNeExe(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(NeHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const NeHeader *ne = (const NeHeader *)exe;
    if (ne->exe_type > NE_OS2)
    {
        fprintf(stderr, "Not an OS/2 NE EXE file (exe_type is %d, not 1)\n", (int)ne->exe_type);
        return 0;
    }

    return 1;
}

static int sanityCheckExe(uint8 **_exe, uint32 *_exelen, int *_is_lx)
{
    if (*_exelen < EXE_HEADER_MINIMUM)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *)(*_exe + DOS_HEADER_NEW_EXE_OFFSET));

    *_exe += header_offset;
    *_exelen -= header_offset;

    const uint8 *magic = *_exe;

    if ((magic[0] == 'L') && (magic[1] == 'X'))
    {
        *_is_lx = 1;
        return sanityCheckLxExe(*_exe, *_exelen);
    }
    else if ((magic[0] == 'N') && (magic[1] == 'E'))
    {
        *_is_lx = 0;
        return sanityCheckNeExe(*_exe, *_exelen);
    }

    fprintf(stderr, "not an OS/2 EXE\n");
    return 0;
}

static int parseLxExe(const uint8 *origexe, const uint8 *exe)
{
    const LxHeader *lx = (const LxHeader *)exe;
    printf("LX (32-bit) executable.\n");
    printf("module version: %u\n", (uint)lx->module_version);

    printf("module flags:");
    if (lx->module_flags & E32LIBINIT)
        printf(" LIBINIT");
    if (lx->module_flags & E32NOINTFIX)
        printf(" INTERNALFIXUPS");
    if (lx->module_flags & E32NOEXTFIX)
        printf(" EXTERNALFIXUPS");
    if (lx->module_flags & E32NOPMW)
        printf(" PMINCOMPAT");
    if (lx->module_flags & E32PMW)
        printf(" PMCOMPAT");
    if (lx->module_flags & E32PMAPI)
        printf(" USESPM");
    if (lx->module_flags & E32NOLOAD)
        printf(" NOTLOADABLE");
    if (lx->module_flags & E32MODDLL)
        printf(" LIBRARYMODULE");
    if (lx->module_flags & E32MODPROTDLL)
        printf(" PROTMEMLIBRARYMODULE");
    if (lx->module_flags & E32MODPDEV)
        printf(" PHYSDRIVERMODULE");
    if (lx->module_flags & E32MODVDEV)
        printf(" VIRTDRIVERMODULE");
    if (lx->module_flags & E32LIBTERM)
        printf(" LIBTERM");
    printf("\n");

    printf("Number of pages in module: %u\n", (uint)lx->module_num_pages);
    printf("EIP Object number: %u\n", (uint)lx->eip_object);
    printf("EIP: 0x%X\n", (uint)lx->eip);
    printf("ESP Object number: %u\n", (uint)lx->esp_object);
    printf("ESP: 0x%X\n", (uint)lx->esp);
    printf("Page size: %u\n", (uint)lx->page_size);
    printf("Page offset shift: %u\n", (uint)lx->page_offset_shift);
    printf("Fixup section size: %u\n", (uint)lx->fixup_section_size);
    printf("Fixup section checksum: 0x%X\n", (uint)lx->fixup_section_checksum);
    printf("Loader section size: %u\n", (uint)lx->loader_section_size);
    printf("Loader section checksum: 0x%X\n", (uint)lx->loader_section_checksum);
    printf("Object table offset: %u\n", (uint)lx->object_table_offset);
    printf("Number of objects in module: %u\n", (uint)lx->module_num_objects);
    printf("Object page table offset: %u\n", (uint)lx->object_page_table_offset);
    printf("Object iterated pages offset: %u\n", (uint)lx->object_iter_pages_offset);
    printf("Resource table offset: %u\n", (uint)lx->resource_table_offset);
    printf("Number of resource table entries: %u\n", (uint)lx->num_resource_table_entries);
    printf("Resident name table offset: %u\n", (uint)lx->resident_name_table_offset);
    printf("Entry table offset: %u\n", (uint)lx->entry_table_offset);
    printf("Module directives offset: %u\n", (uint)lx->module_directives_offset);
    printf("Number of module directives: %u\n", (uint)lx->num_module_directives);
    printf("Fixup page table offset: %u\n", (uint)lx->fixup_page_table_offset);
    printf("Fixup record table offset: %u\n", (uint)lx->fixup_record_table_offset);
    printf("Import module table offset: %u\n", (uint)lx->import_module_table_offset);
    printf("Number of inport module entries: %u\n", (uint)lx->num_import_mod_entries);
    printf("Import procedure name table offset: %u\n", (uint)lx->import_proc_table_offset);
    printf("Per-page checksum offset: %u\n", (uint)lx->per_page_checksum_offset);
    printf("Data pages offset: %u\n", (uint)lx->data_pages_offset);
    printf("Number of preload pages: %u\n", (uint)lx->num_preload_pages);
    printf("Non-resident name table offset: %u\n", (uint)lx->non_resident_name_table_offset);
    printf("Non-resident name table length: %u\n", (uint)lx->non_resident_name_table_len);
    printf("Non-resident name table checksum: 0x%X\n", (uint)lx->non_resident_name_table_checksum);
    printf("Auto data segment object number: %u\n", (uint)lx->auto_ds_object_num);
    printf("Debug info offset: %u\n", (uint)lx->debug_info_offset);
    printf("Debug info length: %u\n", (uint)lx->debug_info_len);
    printf("Number of instance pages in preload section: %u\n", (uint)lx->num_instance_preload);
    printf("Number of instance pages in demand section: %u\n", (uint)lx->num_instance_demand);
    printf("Heap size: %u\n", (uint)lx->heapsize);

    /* OS/2 LX objects with 16:16 aliases are limited to 64 KiB. */
    if ((lx->object_iter_pages_offset != 0) &&
        (lx->object_iter_pages_offset != lx->data_pages_offset))
    {
        fprintf(stderr, "Object iterator pages offset must be 0 or equal to Data pages offset\n");
    }

    printf("\n");
    printf("Object table (%u entries):\n", (uint)lx->module_num_objects);
    for (uint32 i = 0; i < lx->module_num_objects; i++)
    {
        const LxObjectTableEntry *obj =
            ((const LxObjectTableEntry *)(exe + lx->object_table_offset)) + i;
        printf("Object #%u:\n", (uint)i + 1);
        printf("Virtual size: %u\n", (uint)obj->virtual_size);
        printf("Relocation base address: 0x%X\n", (uint)obj->reloc_base_addr);
        printf("Object flags:");
        if (obj->object_flags & OBJREAD)
            printf(" READ");
        if (obj->object_flags & OBJWRITE)
            printf(" WRITE");
        if (obj->object_flags & OBJEXEC)
            printf(" EXEC");
        if (obj->object_flags & OBJRSRC)
            printf(" RESOURCE");
        if (obj->object_flags & OBJDISCARD)
            printf(" DISCARD");
        if (obj->object_flags & OBJSHARED)
            printf(" SHARED");
        if (obj->object_flags & OBJPRELOAD)
            printf(" PRELOAD");
        if (obj->object_flags & OBJINVALID)
            printf(" INVALID");
        if (obj->object_flags & OBJPERM)
            printf(" ZEROFILL");
        if (obj->object_flags & OBJRESIDENT)
            printf(" RESIDENT");
        if (obj->object_flags & OBJCONTIG)
            printf(" RESIDENT+CONTIG");
        if (obj->object_flags & OBJDYNAMIC)
            printf(" RESIDENT+LONGLOCK");
        if (obj->object_flags & OBJRESERVED)
            printf(" SYSRESERVED");
        if (obj->object_flags & OBJALIAS16)
            printf(" 16:16");
        if (obj->object_flags & OBJBIGDEF)
            printf(" BIG");
        if (obj->object_flags & OBJCONFORM)
            printf(" CONFORM");
        if (obj->object_flags & OBJIOPL)
            printf(" IOPL");
        printf("\n");
        printf("Page table index: %u\n", (uint)obj->page_table_index);
        printf("Number of page table entries: %u\n", (uint)obj->num_page_table_entries);
        printf("System-reserved field: %u\n", (uint)obj->reserved);

        printf("Object pages:\n");
        const LxObjectPageTableEntry *objpage =
            ((const LxObjectPageTableEntry *)(exe + lx->object_page_table_offset)) +
            (obj->page_table_index - 1);
        const uint32 *fixuppage =
            (((const uint32 *)(exe + lx->fixup_page_table_offset)) + (obj->page_table_index - 1));
        for (uint32 i = 0; i < obj->num_page_table_entries; i++, objpage++, fixuppage++)
        {
            printf("Object Page #%u:\n", (uint)(i + obj->page_table_index));
            printf("Page data offset: 0x%X\n", (uint)objpage->page_data_offset);
            printf("Page data size: %u\n", (uint)objpage->data_size);
            printf("Page flags: (%u)", (uint)objpage->flags);
            if (objpage->flags == VALID)
                printf(" PHYSICAL");
            else if (objpage->flags == ITERDATA)
                printf(" ITERATED");
            else if (objpage->flags == INVALID)
                printf(" INVALID");
            else if (objpage->flags == ZEROED)
                printf(" ZEROFILL");
            else if (objpage->flags == RANGE)
                printf(" RANGE");
            else if (objpage->flags == ITERDATA2)
                printf(" COMPRESSED");
            else
                printf(" UNKNOWN");
            printf("\n");
            const uint32 fixupoffset = *fixuppage;
            const uint32 fixuplen = fixuppage[1] - fixuppage[0];
            printf("Page's fixup record offset: %u\n", (uint)fixupoffset);
            printf("Page's fixup record size: %u\n", (uint)fixuplen);
            printf("Fixup records:\n");
            const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
            const uint8 *fixupend = fixup + fixuplen;
            for (uint32 i = 0; fixup < fixupend; i++)
            {
                printf("Fixup Record #%u:\n", (uint)(i + 1));
                printf("Source type: ");
                const uint8 srctype = *(fixup++);
                if (srctype & NRALIAS)
                    printf("[FIXUPTOALIAS] ");
                if (srctype & NRCHAIN)
                    printf("[SOURCELIST] ");
                switch (srctype & NRSTYP)
                {
                    case NRSBYT:
                        printf("Byte fixup");
                        break;
                    case NRSSEG:
                        printf("16-bit selector fixup");
                        break;
                    case NRSPTR:
                        printf("16:16 pointer fixup");
                        break;
                    case NRSOFF:
                        printf("16-bit offset fixup");
                        break;
                    case NRPTR48:
                        printf("16:32 pointer fixup");
                        break;
                    case NROFF32:
                        printf("32-bit offset fixup");
                        break;
                    case NRSOFF32:
                        printf("32-bit self-relative offset fixup");
                        break;
                    default:
                        printf("(undefined fixup)");
                        break;
                }
                printf("\n");

                const uint8 fixupflags = *(fixup++);
                printf("Target flags:");
                switch (fixupflags & NRRTYP)
                {
                    case NRRINT:
                        printf(" INTERNAL");
                        break;
                    case NRRORD:
                        printf(" IMPORTBYORDINAL");
                        break;
                    case NRRNAM:
                        printf(" IMPORTBYNAME");
                        break;
                    case NRRENT:
                        printf(" INTERNALVIAENTRY");
                        break;
                }

                if (fixupflags & NRADD)
                    printf(" ADDITIVE");
                if (fixupflags & NRICHAIN)
                    printf(" INTERNALCHAINING");
                if (fixupflags & NR32BITOFF)
                    printf(" 32BITTARGETOFFSET");
                if (fixupflags & NR32BITADD)
                    printf(" 32BITADDITIVE");
                if (fixupflags & NR16OBJMOD)
                    printf(" 16BITORDINAL");
                if (fixupflags & NR8BITORD)
                    printf(" 8BITORDINAL");
                printf("\n");

                uint8 srclist_count = 0;
                if (srctype & NRCHAIN)
                { // source list
                    srclist_count = *(fixup++);
                    printf("Source offset list count: %u\n", (uint)srclist_count);
                }
                else
                {
                    const sint16 srcoffset = *((sint16 *)fixup);
                    fixup += 2;
                    printf("Source offset: %d\n", (int)srcoffset);
                }
                printf("\n");

                switch (fixupflags & NRRTYP)
                {
                    case NRRINT:
                        printf("Internal fixup record:\n");
                        if (fixupflags & NR16OBJMOD)
                        {
                            const uint16 val = *((uint16 *)fixup);
                            fixup += 2;
                            printf("Object: %u\n", (uint)val);
                        }
                        else
                        {
                            const uint8 val = *(fixup++);
                            printf("Object: %u\n", (uint)val);
                        }

                        printf("Target offset: ");
                        if ((srctype & NRSTYP) == NRSSEG)
                        { // 16-bit selector fixup
                            printf("[not used for 16-bit selector fixups]\n");
                        }
                        else if (fixupflags & NR32BITOFF)
                        { // 32-bit target offset
                            const uint32 val = *((uint32 *)fixup);
                            fixup += 4;
                            printf("%u\n", (uint)val);
                        }
                        else
                        { // 16-bit target offset
                            const uint16 val = *((uint16 *)fixup);
                            fixup += 2;
                            printf("%u\n", (uint)val);
                        }
                        break;

                    case NRRORD:
                        printf("Import by ordinal fixup record:\n");
                        if (fixupflags & NR16OBJMOD)
                        {
                            const uint16 val = *((uint16 *)fixup);
                            fixup += 2;
                            printf("Module ordinal: %u\n", (uint)val);
                        }
                        else
                        {
                            const uint8 val = *(fixup++);
                            printf("Module ordinal: %u\n", (uint)val);
                        }

                        if (fixupflags & NR8BITORD)
                        {
                            const uint8 val = *(fixup++);
                            printf("Import ordinal: %u\n", (uint)val);
                        }
                        else if (fixupflags & NR32BITOFF)
                        {
                            const uint32 val = *((uint32 *)fixup);
                            fixup += 4;
                            printf("Import ordinal: %u\n", (uint)val);
                        }
                        else
                        {
                            const uint16 val = *((uint16 *)fixup);
                            fixup += 2;
                            printf("Import ordinal: %u\n", (uint)val);
                        }

                        uint32 additive = 0;
                        if (fixupflags & NRADD)
                        { // Has additive.
                            if (fixupflags & NR32BITADD)
                            {
                                additive = *((uint32 *)fixup);
                                fixup += 4;
                            }
                            else
                            {
                                additive = *((uint16 *)fixup);
                                fixup += 2;
                            }
                        }
                        printf("Additive: %u\n", (uint)additive);
                        break;

                    case NRRNAM:
                    {
                        printf("Import by name fixup record:\n");
                        if (fixupflags & NR16OBJMOD)
                        {
                            const uint16 val = *((uint16 *)fixup);
                            fixup += 2;
                            printf("Module ordinal: %u\n", (uint)val);
                        }
                        else
                        {
                            const uint8 val = *(fixup++);
                            printf("Module ordinal: %u\n", (uint)val);
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

                        const uint8 *import_name =
                            (exe + lx->import_proc_table_offset) + name_offset;
                        char name[128];
                        const uint8 namelen = *(import_name++) & NAME_LENGTH_MASK;
                        memcpy(name, import_name, namelen);
                        name[namelen] = '\0';
                        printf("Name offset: %u ('%s')\n", (uint)name_offset, name);

                        uint32 additive = 0;
                        if (fixupflags & NRADD)
                        { // Has additive.
                            if (fixupflags & NR32BITADD)
                            {
                                additive = *((uint32 *)fixup);
                                fixup += 4;
                            }
                            else
                            {
                                additive = *((uint16 *)fixup);
                                fixup += 2;
                            }
                        }
                        printf("Additive: %u\n", (uint)additive);

                        break;
                    }

                    case NRRENT:
                        printf("Internal entry table fixup record:\n");
                        printf("WRITE ME\n");
                        exit(1);
                        break;
                }

                if (srctype & NRCHAIN)
                { // source list
                    printf("Source offset list:");
                    for (uint8 i = 0; i < srclist_count; i++)
                    {
                        const sint16 val = *((sint16 *)fixup);
                        fixup += 2;
                        printf(" %d", (int)val);
                    }
                }

                printf("\n\n");
            }

            printf("\n");
        }
    }

    printf("\n");
    printf("Resource table (%u entries):\n", (uint)lx->num_resource_table_entries);
    for (uint32 i = 0; i < lx->num_resource_table_entries; i++)
    {
        const LxResourceTableEntry *rsrc =
            ((const LxResourceTableEntry *)(exe + lx->resource_table_offset)) + i;
        printf("%u:\n", (uint)i);
        printf("Type ID: %u\n", (uint)rsrc->type_id);
        printf("Name ID: %u\n", (uint)rsrc->name_id);
        printf("Resource size: %u\n", (uint)rsrc->resource_size);
        printf("Object: %u\n", (uint)rsrc->object);
        printf("Offset: 0x%X\n", (uint)rsrc->offset);
        printf("\n");
    }

    printf("\n");

    printf("Entry table:\n");
    int bundleid = 1;
    int ordinal = 1;
    const uint8 *entryptr = exe + lx->entry_table_offset;
    while (*entryptr)
    {
        const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
        const uint8 bundletype = (*entryptr) & ~TYPEINFO;
        const uint8 paramtypes = (*(entryptr++) & TYPEINFO) ? 1 : 0;

        printf("Bundle %d (%u entries): ", bundleid, (uint)numentries);
        bundleid++;

        if (paramtypes)
            printf("[PARAMTYPES] ");

        switch (bundletype)
        {
            case EMPTY:
                printf("UNUSED\n");
                printf(" %u unused entries.\n\n", (uint)numentries);
                ordinal += numentries;
                break;

            case ENTRY16:
                printf("16BIT\n");
                printf(" Object number: %u\n", (uint) * ((const uint16 *)entryptr));
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    if (*entryptr & E32EXPORT)
                        printf(" EXPORTED");
                    printf("\n");
                    printf(" Parameter word count: %u\n",
                           (uint)((*entryptr & E32PARAMS) >> E32PARAMS_SHIFT));
                    entryptr++;
                    printf(" Offset: %u\n", (uint) * ((const uint16 *)entryptr));
                    entryptr += 2;
                    printf("\n");
                }
                break;

            case GATE16:
                printf("286CALLGATE\n");
                printf(" Object number: %u\n", (uint) * ((const uint16 *)entryptr));
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    if (*entryptr & E32EXPORT)
                        printf(" EXPORTED");
                    printf("\n");
                    printf(" Parameter word count: %u\n",
                           (uint)((*entryptr & E32PARAMS) >> E32PARAMS_SHIFT));
                    entryptr++;
                    printf(" Offset: %u\n", (uint) * ((const uint16 *)entryptr));
                    entryptr += 2;
                    printf(" Callgate selector: %u\n", (uint) * ((const uint16 *)entryptr));
                    entryptr += 2;
                    printf("\n");
                }
                break;

            case ENTRY32:
                printf("32BIT\n");
                printf(" Object number: %u\n", (uint) * ((const uint16 *)entryptr));
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    if (*entryptr & E32EXPORT)
                        printf(" EXPORTED");
                    printf("\n");
                    printf(" Parameter word count: %u\n",
                           (uint)((*entryptr & E32PARAMS) >> E32PARAMS_SHIFT));
                    entryptr++;
                    printf(" Offset: %u\n", (uint) * ((const uint32 *)entryptr));
                    entryptr += 4;
                    printf("\n");
                }
                break;

            case ENTRYFWD:
                printf("FORWARDER\n");
                break;
                printf(" Reserved field: %u\n", (uint) * ((const uint16 *)entryptr));
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    const int isordinal = (*entryptr & E32EXPORT);
                    if (isordinal)
                        printf(" IMPORTBYORDINAL");
                    printf("\n");
                    printf(" Reserved for future use: %u\n",
                           (uint)((*entryptr & E32PARAMS) >> E32PARAMS_SHIFT));
                    entryptr++;
                    printf(" Module ordinal number: %u\n", (uint) * ((const uint16 *)entryptr));
                    entryptr += 2;
                    if (isordinal)
                    {
                        printf(" Import ordinal number: %u\n", (uint) * ((const uint32 *)entryptr));
                        entryptr += 4;
                    }
                    else
                    {
                        printf(" Import name offset: %u\n", (uint) * ((const uint32 *)entryptr));
                        entryptr += 4;
                    }
                    printf("\n");
                }
                break;

            default:
                printf("UNKNOWN (%u)\n\n", (uint)bundletype);
                break;
        }
    }
    printf("\n");

    printf("Module directives (%u entries):\n", (uint)lx->num_module_directives);
    const uint8 *dirptr = exe + lx->module_directives_offset;
    for (uint32 i = 0; i < lx->num_module_directives; i++)
    {
        printf("%u:\n", (uint)i + 1);
        printf("Directive ID: %u\n", (uint) * ((const uint16 *)dirptr));
        dirptr += 2;
        printf("Data size: %u\n", (uint) * ((const uint16 *)dirptr));
        dirptr += 2;
        printf("Data offset: %u\n", (uint) * ((const uint32 *)dirptr));
        dirptr += 4;
        printf("\n");
    }
    printf("\n");

    if (lx->per_page_checksum_offset == 0)
    {
        printf("No per-page checksums available.\n");
    }
    else
    {
        printf("!!! FIXME: look at per-page checksums!\n");
    }
    printf("\n");

    printf("Import modules (%u entries):\n", (uint)lx->num_import_mod_entries);
    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++)
    {
        char name[128];
        const uint8 namelen = *(import_modules_table++);

        memcpy(name, import_modules_table, namelen);
        import_modules_table += namelen;
        name[namelen] = '\0';
        printf("%u: %s\n", (uint)i + 1, name);
    }

    const uint8 *name_table;

    printf("Resident name table:\n");
    name_table = exe + lx->resident_name_table_offset;
    for (uint32 i = 0; *name_table; i++)
    {
        const uint8 namelen = *(name_table++);
        char name[256];
        memcpy(name, name_table, namelen);
        name[namelen] = '\0';
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *)name_table);
        name_table += 2;
        printf("%u: '%s' (ordinal %u)\n", (uint)i, name, (uint)ordinal);
    }

    printf("Non-resident name table:\n");
    name_table = origexe + lx->non_resident_name_table_offset;
    const uint8 *end_of_name_table = name_table + lx->non_resident_name_table_len;
    for (uint32 i = 0; (name_table < end_of_name_table) && *name_table; i++)
    {
        const uint8 namelen = *(name_table++);
        char name[256];
        memcpy(name, name_table, namelen);
        name[namelen] = '\0';
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *)name_table);
        name_table += 2;
        printf("%u: '%s' (ordinal %u)\n", (uint)i, name, (uint)ordinal);
    }

    return 1;
}

static int parseNeExe(const uint8 *origexe, const uint8 *exe)
{
    const NeHeader *ne = (const NeHeader *)exe;
    printf("NE (16-bit) executable.\n");
    printf("Linker version: %u\n", (uint)ne->linker_version);
    printf("Linker revision: %u\n", (uint)ne->linker_revision);
    printf("Entry table offset: %u\n", (uint)ne->entry_table_offset);
    printf("Entry table size: %u\n", (uint)ne->entry_table_size);
    printf("CRC32: 0x%X\n", (uint)ne->crc32);
    printf("Module flags:");
    if (ne->module_flags == 0)
        printf(" NOAUTODATA");
    if (ne->module_flags & NESOLO)
        printf(" SINGLEDATA");
    if (ne->module_flags & NEINST)
        printf(" MULTIPLEDATA");
    if (ne->module_flags & NEPPLI)
        printf(" FAMILYAPI");
    if (ne->module_flags & NEIERR)
        printf(" NOTLOADABLE");
    if (ne->module_flags & NENOTMPSAFE)
        printf(" NONCONFORMING");
    if (ne->module_flags & NENOTP)
        printf(" LIBRARYMODULE");
    printf("\n");
    printf("Application type: ");
    switch ((ne->module_flags >> NE_APPTYPE_SHIFT) & NE_APPTYPE_BITS)
    {
        case 0x0:
            printf("CONSOLE");
            break;
        case 0x1:
            printf("FULLSCREEN");
            break;
        case 0x2:
            printf("WINPMCOMPAT");
            break;
        case 0x3:
            printf("WINPMUSES");
            break;
    }
    printf("\n");
    printf("Automatic data segment: %u\n", (uint)ne->auto_data_segment);
    printf("Dynamic heap size: %u\n", (uint)ne->dynamic_heap_size);
    printf("Stack size: %u\n", (uint)ne->stack_size);
    printf("Initial code address: %X:%X\n", (uint)ne->reg_cs, (uint)ne->reg_ip);
    printf("Initial stack address: %X:%X\n", (uint)ne->reg_ss, (uint)ne->reg_sp);
    printf("Number of segment table entries: %u\n", (uint)ne->num_segment_table_entries);
    printf("Number of module reference table entries: %u\n",
           (uint)ne->num_module_ref_table_entries);
    printf("Non-resident name table size: %u\n", (uint)ne->non_resident_name_table_size);
    printf("Segment table offset: %u\n", (uint)ne->segment_table_offset);
    printf("Resource table offset: %u\n", (uint)ne->resource_table_offset);
    printf("Resident name table offset: %u\n", (uint)ne->resident_name_table_offset);
    printf("Module reference table offset: %u\n", (uint)ne->module_reference_table_offset);
    printf("Imported names table offset: %u\n", (uint)ne->imported_names_table_offset);
    printf("Non-resident name table offset: %u\n", (uint)ne->non_resident_name_table_offset);
    printf("Number of movable entries: %u\n", (uint)ne->num_movable_entries);
    printf("Sector alignment shift count: %u\n", (uint)ne->sector_alignment_shift_count);
    printf("Number of resource entries: %u\n", (uint)ne->num_resource_entries);
    printf("Executable type: %u\n", (uint)ne->exe_type);

    printf("OS/2 flags:");
    if (ne->os2_exe_flags == 0)
    {
        printf(" (none)");
    }
    else
    {
        if (ne->os2_exe_flags & NELONGNAMES)
            printf(" LONGFILENAMES");
        if (ne->os2_exe_flags & NEWINISPROT)
            printf(" PROTECTEDMODE");
        if (ne->os2_exe_flags & NEWINGETPROPFON)
            printf(" PROPORTIONALFONTS");
        if (ne->os2_exe_flags & NE_GANGLOAD_AREA)
            printf(" GANGLOADAREA");
    }
    printf("\n");
    printf("\n");

    const uint32 sector_shift = ne->sector_alignment_shift_count;

    if (ne->num_segment_table_entries > 0)
    {
        const uint32 total = (uint32)ne->num_segment_table_entries;
        printf("Segment table (%u entries):\n", (uint)total);
        const NeSegmentTableEntry *seg =
            (const NeSegmentTableEntry *)(exe + ne->segment_table_offset);
        for (uint32 i = 0; i < total; i++, seg++)
        {
            printf(" %u:\n", (uint)(i + 1));
            printf("  Logical-sector offset: %u", (uint)seg->offset);
            if (seg->offset == 0)
            {
                printf(" (no file data)\n");
            }
            else
            {
                printf(" (byte position %u)\n", (uint)(((uint32)seg->offset) << sector_shift));
            }
            printf("  Size: %u\n", (seg->size == 0) ? (uint)NE_SEGMENT_64K : (uint)seg->size);
            printf("  Segment flags:");
            switch (seg->segment_flags & NE_SEGMENT_TYPE_BITS)
            {
                case NSCODE:
                    printf(" CODE");
                    break;
                case NSDATA:
                    printf(" DATA");
                    break;
                default:
                    printf(" [UNKNOWN SEGMENT TYPE]");
                    break;
            }
            if (seg->segment_flags & NE_SEGMENT_REALMODE)
                printf(" REALMODE");
            if (seg->segment_flags & NSITER)
                printf(" ITERATED");
            if (seg->segment_flags & NSMOVE)
                printf(" MOVABLE");
            if (seg->segment_flags & NSSHARED)
                printf(" SHAREABLE");
            if (seg->segment_flags & NSPRELOAD)
                printf(" PRELOAD");
            if (seg->segment_flags & NSEXRD)
                printf(" READONLY");
            if (seg->segment_flags & NSRELOC)
                printf(" RELOCINFO");
            if (seg->segment_flags & NE_SEGMENT_DEBUGINFO)
                printf(" DEBUGINFO");
            if (seg->segment_flags & NE_SEGMENT_DISCARD_PRIORITY)
                printf(" DISCARD");
            printf("\n");

            printf("  Discard priority: %u\n",
                   (uint)((seg->segment_flags >> NE_DISCARD_PRIORITY_SHIFT) &
                          NE_DISCARD_PRIORITY_BITS));
            printf("  Minimum allocation: %u\n", (seg->minimum_allocation == 0)
                                                     ? (uint)NE_SEGMENT_64K
                                                     : (uint)seg->minimum_allocation);

            if (seg->segment_flags & NSRELOC)
            { // has relocations (fixups)
                const uint8 *segptr = origexe + (((uint32)seg->offset) << sector_shift);
                const uint8 *fixupptr = segptr + (seg->size ? seg->size : NE_SEGMENT_64K - 1);
                const uint32 num_fixups = (uint32) * ((const uint16 *)fixupptr);
                fixupptr += 2;
                printf("  Fixup records (%u entries):\n", (uint)num_fixups);
                for (uint32 j = 0; j < num_fixups; j++)
                {
                    const uint8 srctype = *(fixupptr++);
                    const uint8 flags = *(fixupptr++);
                    uint32 srcchain_offset = (uint32) * ((const uint16 *)fixupptr);
                    fixupptr += 2;
                    const uint8 *targetptr = fixupptr;
                    fixupptr += 4;
                    const int additive = (flags & NRADD) != 0;
                    printf("   %u:\n", (uint)j);
                    printf("    Source type: ");
                    switch (srctype & NRSTYP)
                    {
                        case NRSBYT:
                            printf("LOBYTE");
                            break;
                        case NRSSEG:
                            printf("SEGMENT");
                            break;
                        case NRSPTR:
                            printf("FAR_ADDR");
                            break;
                        case NRSOFF:
                            printf("OFFSET");
                            break;
                        default:
                            printf("[unknown type]");
                            break;
                    }
                    printf("\n");
                    printf("    Fixup type:");
                    if (additive)
                        printf(" ADDITIVE");
                    switch (flags & NRRTYP)
                    {
                        case NRRINT:
                        {
                            const uint8 segment = targetptr[0];

                            const uint16 offset = *((const uint16 *)&targetptr[2]);
                            printf(" INTERNALREF (");
                            if (segment == NE_MOVABLE_SEGMENT)
                                printf("movable segment, ordinal %u)", (uint)offset);
                            else
                                printf("segment %u, offset %u)", (uint)segment, (uint)offset);
                        }
                        break;

                        case NRRORD:
                        {
                            const uint16 module = ((const uint16 *)targetptr)[0];
                            const uint16 ordinal = ((const uint16 *)targetptr)[1];
                            printf(" IMPORTORDINAL (module %u, ordinal %u)", (uint)module,
                                   (uint)ordinal);
                        }
                        break;

                        case NRRNAM:
                        {
                            const uint16 module = ((const uint16 *)targetptr)[0];
                            const uint16 offset = ((const uint16 *)targetptr)[1];
                            printf(" IMPORTNAME (module %u, name offset %u)", (uint)module,
                                   (uint)offset);
                        }
                        break;

                        case NRROSF:
                        {
                            const uint16 type = ((const uint16 *)targetptr)[0];
                            printf(" OSFIXUP (type %u)", (uint)type);
                        }
                        break;
                    }
                    printf("\n");

                    if (additive)
                    {
                        printf("    Additive source: %u", (uint)srcchain_offset);
                    }
                    else
                    {
                        printf("    Source chain:");
                        do
                        {
                            printf(" %X", (uint)srcchain_offset);
                            srcchain_offset = *((const uint16 *)(segptr + srcchain_offset));
                        } while (srcchain_offset < seg->size);
                    }
                    printf("\n");
                }
            }
        }
        printf("\n");
    }

    if (ne->num_resource_entries > 0)
    {
        const uint8 *ptr = exe + ne->resource_table_offset;
        const uint32 total = (uint32)ne->num_resource_entries;
        printf("Resources (%u entries):\n", (uint)total);
        int idx = 0;
        while (*((const uint16 *)ptr))
        {
            const uint16 typeid = *((const uint16 *)ptr);
            ptr += 2;
            const uint32 num_resources = (uint32) * ((const uint16 *)ptr);
            ptr += 2;
            ptr += 4;
            for (uint32 j = 0; j < num_resources; j++)
            {
                idx++;
                printf(" %d:", idx);
                printf("  Type ID: ");
                if (typeid & NE_RESOURCE_INTEGER_ID)
                {
                    printf("%u\n", (uint)(typeid & ~NE_RESOURCE_INTEGER_ID));
                }
                else
                {
                    const uint8 *str = (exe + ne->resource_table_offset) + typeid;
                    const uint32 len = (uint32) * (str++);
                    printf("\"");
                    for (uint32 k = 0; k < len; k++, str++)
                    {
                        printf("%c", (int)*str);
                    }
                    printf("\"\n");
                }

                const uint16 offset = *((const uint16 *)ptr);
                ptr += 2;
                const uint16 size = *((const uint16 *)ptr);
                ptr += 2;
                const uint16 flags = *((const uint16 *)ptr);
                ptr += 2;
                const uint16 resourceid = *((const uint16 *)ptr);
                ptr += 2;
                ptr += 4;
                printf("  Offset: %u\n", (uint)offset);
                printf("  Size: %u\n", (uint)size);
                printf("  Flags:");
                if (flags & NSMOVE)
                    printf(" MOVABLE");
                if (flags & NSSHARED)
                    printf(" PURE");
                if (flags & NSPRELOAD)
                    printf(" PRELOAD");
                printf("\n");
                printf("  Resource ID: ");
                if (resourceid & NE_RESOURCE_INTEGER_ID)
                {
                    printf("%u\n", (uint)(resourceid & ~NE_RESOURCE_INTEGER_ID));
                }
                else
                {
                    const uint8 *str = (exe + ne->resource_table_offset) + resourceid;
                    const uint32 len = (uint32) * (str++);
                    printf("\"");
                    for (uint32 k = 0; k < len; k++, str++)
                    {
                        printf("%c", (int)*str);
                    }
                    printf("\"\n");
                }
            }
        }
        printf("\n");
    }

    if (ne->resident_name_table_offset > 0)
    {
        const uint8 *name_table = exe + ne->resident_name_table_offset;
        if (*name_table)
        {
            printf("Resident name table:\n");
            for (uint32 i = 0; *name_table; i++)
            {
                const uint8 namelen = *(name_table++);
                char name[256];
                memcpy(name, name_table, namelen);
                name[namelen] = '\0';
                name_table += namelen;
                const uint16 ordinal = *((const uint16 *)name_table);
                name_table += 2;
                printf(" %u: '%s' (ordinal %u)\n", (uint)i, name, (uint)ordinal);
            }
            printf("\n");
        }
    }

    if (ne->non_resident_name_table_offset > 0)
    {
        const uint8 *name_table = origexe + ne->non_resident_name_table_offset;
        const uint8 *end_of_name_table = name_table + ne->non_resident_name_table_size;
        if ((name_table < end_of_name_table) && *name_table)
        {
            printf("Non-resident name table:\n");
            for (uint32 i = 0; (name_table < end_of_name_table) && *name_table; i++)
            {
                const uint8 namelen = *(name_table++);
                char name[256];
                memcpy(name, name_table, namelen);
                name[namelen] = '\0';
                name_table += namelen;
                const uint16 ordinal = *((const uint16 *)name_table);
                name_table += 2;
                printf(" %u: '%s' (ordinal %u)\n", (uint)i, name, (uint)ordinal);
            }
            printf("\n");
        }
    }

    if (ne->num_module_ref_table_entries > 0)
    {
        const uint32 total = (uint32)ne->num_module_ref_table_entries;
        printf("Module reference table (%u entries):\n", (uint)total);
        const uint16 *ptr = (const uint16 *)(exe + ne->module_reference_table_offset);
        for (uint32 i = 0; i < total; i++, ptr++)
        {
            const uint8 *name = (exe + ne->imported_names_table_offset) + *ptr;
            const uint32 name_string_len = (uint32)*name;
            name++;
            printf(" %u: ", (uint)i + 1);
            for (uint32 j = 0; j < name_string_len; j++, name++)
            {
                printf("%c", (int)*name);
            }
            printf("\n");
        }
        printf("\n");
    }

    if (ne->entry_table_size > 0)
    {
        const uint8 *ptr = exe + ne->entry_table_offset;
        if (*ptr)
        {
            printf("Entry table:\n");
            uint32 ordinal = 1;
            while (1)
            {
                const uint8 bundled = *(ptr++);
                if (!bundled)
                {
                    break;
                }

                const uint8 bundletype = *(ptr++);
                switch (bundletype)
                {
                    case EMPTY: // unused entries, to skip ordinals.
                        ordinal += bundled;
                        break;

                    case NE_MOVABLE_SEGMENT: // Moveable segment entries.
                        for (uint32 i = 0; i < bundled; i++, ordinal++)
                        {
                            const uint8 flags = *(ptr++);
                            const uint16 int3f = *((const uint16 *)ptr);
                            ptr += 2;
                            const uint8 segment = *(ptr++);
                            const uint16 offset = *((const uint16 *)ptr);
                            ptr += 2;
                            printf(" %u:\n", (uint)ordinal);
                            printf("  Flags: MOVABLE");
                            if (flags & E32EXPORT)
                                printf(" EXPORTED");
                            if (flags & E32SHARED)
                                printf(" GLOBAL");
                            printf("\n");
                            printf("  Int3f: %X\n", (uint)int3f);
                            printf("  Segment: %u\n", (uint)segment);
                            printf("  Offset: %u\n", (uint)offset);
                        }
                        break;

                    default:
                        for (uint32 i = 0; i < bundled; i++, ordinal++)
                        {
                            const uint8 flags = *(ptr++);
                            const uint16 offset = *((const uint16 *)ptr);
                            ptr += 2;
                            printf(" %u:\n", (uint)ordinal);
                            printf("  Flags: FIXED");
                            if (flags & E32EXPORT)
                                printf(" EXPORTED");
                            if (flags & E32SHARED)
                                printf(" GLOBAL");
                            printf("\n");
                            printf("  Segment: %u\n", (uint)bundletype);
                            printf("  Offset: %u\n", (uint)offset);
                        }
                        break;
                }
            }
            printf("\n");
        }
    }

    return 1;
}

static void parseExe(const char *exefname, uint8 *exe, uint32 exelen)
{
    int is_lx = 0;

    printf("%s\n", exefname);

    const uint8 *origexe = exe;
    if (!sanityCheckExe(&exe, &exelen, &is_lx))
        return;

    if (is_lx)
    {
        parseLxExe(origexe, exe);
    }
    else
    {
        parseNeExe(origexe, exe);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "USAGE: %s <program.exe>\n", argv[0]);
        return 1;
    }

    const char *exefname = argv[1];
    FILE *io = fopen(exefname, "rb");
    if (!io)
    {
        fprintf(stderr, "can't open '%s: %s'\n", exefname, strerror(errno));
        return 2;
    }

    if (fseek(io, 0, SEEK_END) < 0)
    {
        fprintf(stderr, "can't seek in '%s': %s\n", exefname, strerror(errno));
        return 3;
    }

    const uint32 exelen = ftell(io);
    uint8 *exe = (uint8 *)malloc(exelen);
    if (!exe)
    {
        fprintf(stderr, "Out of memory\n");
        return 4;
    }

    rewind(io);
    if (fread(exe, exelen, 1, io) != 1)
    {
        fprintf(stderr, "read failure on '%s': %s\n", exefname, strerror(errno));
        return 5;
    }

    fclose(io);

    parseExe(exefname, exe, exelen);

    free(exe);

    return 0;
}
