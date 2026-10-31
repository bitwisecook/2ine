/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_FORMAT_NE_H
#define TWOINE_FORMAT_NE_H
#include "formats/types.h"
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
