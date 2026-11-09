/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_FORMAT_LX_H
#define TWOINE_FORMAT_LX_H
#include "formats/executable/relocations.h"
#include "formats/executable/types.h"

/* LX field values; names and values follow the Toolkit's exe386.h. */
enum
{
    E32LEBO = 0x00, /* little-endian byte order */
    E32LEWO = 0x00, /* little-endian word order */
    E32LEVEL = 0,
    E32CPU286 = 0x001,
    E32CPU386 = 0x002,
    E32CPU486 = 0x003,
    LX_OS_OS2 = 1, /* e32_os; the other values are DOS 4, Windows and Win386 */
    LX_PAGE_SIZE = 4096,

    /* e32_mflags */
    E32NOINTFIX = 0x0010,
    E32NOLOAD = 0x2000,
    E32NOTP = 0x8000,
    E32MODEXE = 0x00000,
    E32MODDLL = 0x08000,
    E32MODMASK = 0x38000,

    /* o32_flags */
    OBJREAD = 0x0001,
    OBJWRITE = 0x0002,
    OBJEXEC = 0x0004,
    OBJRSRC = 0x0008,
    OBJALIAS16 = 0x1000,
    OBJBIGDEF = 0x2000,

    /* o32_pageflags; the Toolkit spells these VALID, ITERDATA, INVALID, ZEROED, ITERDATA2 */
    LX_PAGE_VALID = 0x00,
    LX_PAGE_ITERDATA = 0x01,
    LX_PAGE_INVALID = 0x02,
    LX_PAGE_ZEROED = 0x03,
    LX_PAGE_ITERDATA2 = 0x05,
    LX_PAGE_ITERDATA3 = 0x08, /* EXEPACK:2 compressed page; the Toolkit header predates it */

    /* Fixup record bits exe386.h adds to relocations.h */
    NRALIAS = 0x10,
    NRCHAIN = 0x20,
    NRRENT = 0x03,
    NR32BITOFF = 0x10,
    NR32BITADD = 0x20,
    NR16OBJMOD = 0x40,
    NR8BITORD = 0x80,

    /* Entry table bundle types (b32_type); the Toolkit spells these EMPTY, ENTRY16, GATE16,
       ENTRY32, ENTRYFWD and TYPEINFO */
    LX_BUNDLE_EMPTY = 0x00,
    LX_BUNDLE_ENTRY16 = 0x01,
    LX_BUNDLE_GATE16 = 0x02,
    LX_BUNDLE_ENTRY32 = 0x03,
    LX_BUNDLE_ENTRYFWD = 0x04,
    LX_BUNDLE_TYPEINFO = 0x80,
    FIXENT16 = 3, /* bytes per 16-bit entry */
    FIXENT32 = 5, /* bytes per 32-bit entry */
    FWDENT = 7,   /* bytes per forwarder entry */
    /* Forwarder entry flag: the target is an ordinal, not a procedure-name offset. */
    FWD_ORDINAL = 0x01,

    /* An internal chaining fixup's 32-bit link word: next offset in the top 12 bits, target
       delta in the low 20; LX_ICHAIN_END ends the chain. */
    LX_ICHAIN_TARGET_MASK = 0xFFFFF,
    LX_ICHAIN_NEXT_SHIFT = 20,
    LX_ICHAIN_END = 0xFFF,

    /* Names in LX name tables are length-prefixed; the top length bit is reserved. */
    LX_NAME_LENGTH_MASK = 0x7F
};

#pragma pack(push, 1)

typedef struct LxHeader
{
    uint8 magic_l;
    uint8 magic_x;
    uint8 byte_order;
    uint8 word_order;
    uint32 lx_version;
    uint16 cpu_type;
    uint16 os_type;
    uint32 module_version;
    uint32 module_flags;
    uint32 module_num_pages;
    uint32 eip_object;
    uint32 eip;
    uint32 esp_object;
    uint32 esp;
    uint32 page_size;
    uint32 page_offset_shift;
    uint32 fixup_section_size;
    uint32 fixup_section_checksum;
    uint32 loader_section_size;
    uint32 loader_section_checksum;
    uint32 object_table_offset;
    uint32 module_num_objects;
    uint32 object_page_table_offset;
    uint32 object_iter_pages_offset;
    uint32 resource_table_offset;
    uint32 num_resource_table_entries;
    uint32 resident_name_table_offset;
    uint32 entry_table_offset;
    uint32 module_directives_offset;
    uint32 num_module_directives;
    uint32 fixup_page_table_offset;
    uint32 fixup_record_table_offset;
    uint32 import_module_table_offset;
    uint32 num_import_mod_entries;
    uint32 import_proc_table_offset;
    uint32 per_page_checksum_offset;
    uint32 data_pages_offset;
    uint32 num_preload_pages;
    uint32 non_resident_name_table_offset;
    uint32 non_resident_name_table_len;
    uint32 non_resident_name_table_checksum;
    uint32 auto_ds_object_num;
    uint32 debug_info_offset;
    uint32 debug_info_len;
    uint32 num_instance_preload;
    uint32 num_instance_demand;
    uint32 heapsize;
} LxHeader;

typedef struct LxObjectTableEntry
{
    uint32 virtual_size;
    uint32 reloc_base_addr;
    uint32 object_flags;
    uint32 page_table_index;
    uint32 num_page_table_entries;
    uint32 reserved;
} LxObjectTableEntry;

typedef struct LxObjectPageTableEntry
{
    uint32 page_data_offset;
    uint16 data_size;
    uint16 flags;
} LxObjectPageTableEntry;

typedef struct LxResourceTableEntry
{
    uint16 type_id;
    uint16 name_id;
    uint32 resource_size;
    uint16 object;
    uint32 offset;
} LxResourceTableEntry;

#pragma pack(pop)
#endif
