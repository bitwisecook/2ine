/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_FORMAT_NE_H
#define TWOINE_FORMAT_NE_H
#include "formats/executable/relocations.h"
#include "formats/executable/types.h"

/* NE field values; names and values follow the Toolkit's newexe.h. */
enum
{
    NE_OS2 = 0x1, /* ne_exetyp */

    /* ne_flags */
    NEIERR = 0x2000, /* errors in image: not loadable */
    NENOTP = 0x8000, /* library module */

    /* ns_flags */
    NSTYPE = 0x0007,
    NSCODE = 0x0000,
    NSITER = 0x0008,
    NSPRELOAD = 0x0040,
    NSEXRD = 0x0080, /* execute-only code, or read-only data */
    NSRELOC = 0x0100,

    /* Relocation target type that newexe.h adds to relocations.h */
    NRROSF = 0x03, /* operating-system (floating-point) fixup */

    /* Entry table bundle indicators; any other value is a fixed-segment number. */
    NE_BUNDLE_UNUSED = 0x00,
    NE_BUNDLE_MOVABLE = 0xFF,
    NE_FIXED_ENTRY_SIZE = 3,   /* flags, offset */
    NE_MOVABLE_ENTRY_SIZE = 6, /* flags, INT 3Fh, segment, offset */

    /* An internal reference to this segment number names an entry ordinal instead. */
    NE_MOVABLE_SEGMENT = 0xFF,

    /* A segment length of zero means 64 KiB. */
    NE_SEGMENT_MAX_SIZE = 0x10000
};

#pragma pack(push, 1)

typedef struct NeHeader
{
    uint8 magic_n;
    uint8 magic_e;
    uint8 linker_version;
    uint8 linker_revision;
    uint16 entry_table_offset;
    uint16 entry_table_size;
    uint32 crc32;
    uint16 module_flags;
    uint16 auto_data_segment;
    uint16 dynamic_heap_size;
    uint16 stack_size;
    uint16 reg_ip;
    uint16 reg_cs;
    uint16 reg_sp;
    uint16 reg_ss;
    uint16 num_segment_table_entries;
    uint16 num_module_ref_table_entries;
    uint16 non_resident_name_table_size;
    uint16 segment_table_offset;
    uint16 resource_table_offset;
    uint16 resident_name_table_offset;
    uint16 module_reference_table_offset;
    uint16 imported_names_table_offset;
    uint32 non_resident_name_table_offset;
    uint16 num_movable_entries;
    uint16 sector_alignment_shift_count;
    uint16 num_resource_entries;
    uint8 exe_type;
    uint8 os2_exe_flags;
    uint8 reserved[8];
} NeHeader;

typedef struct NeSegmentTableEntry
{
    uint16 offset;
    uint16 size;
    uint16 segment_flags;
    uint16 minimum_allocation;
} NeSegmentTableEntry;

#pragma pack(pop)
#endif
