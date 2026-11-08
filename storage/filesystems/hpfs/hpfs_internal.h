/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS v2 volume state and the routines shared by the HPFS source files. All sector
   numbers are relative to the selected volume. */
#ifndef TB_STORAGE_HPFS_INTERNAL_H
#define TB_STORAGE_HPFS_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Signatures at the start of each structure; the superblock and spare block have two. */
#define HPFS_SUPERBLOCK_SIGNATURE 0xf995e849u
#define HPFS_SUPERBLOCK_SIGNATURE2 0xfa53e9c5u
#define HPFS_SPAREBLOCK_SIGNATURE 0xf9911849u
#define HPFS_SPAREBLOCK_SIGNATURE2 0xfa5229c5u
#define HPFS_FNODE_SIGNATURE 0xf7e40aaeu
#define HPFS_ANODE_SIGNATURE 0x37e40aaeu
#define HPFS_DNODE_SIGNATURE 0x77e40aaeu
#define HPFS_CODEPAGE_INFO_SIGNATURE 0x494521f7u
#define HPFS_CODEPAGE_DATA_SIGNATURE 0x894521f7u
/* Upper bound on tree nodes and extents per file, so a corrupt tree cannot exhaust memory. */
#define HPFS_NODE_LIMIT 1048576u
/* Largest HPFS file size, 2 GB less one byte. */
#define HPFS_MAX_FILE_BYTES 0x7fffffffu

/* Volume geometry. */
enum
{
    HPFS_SECTOR_BYTES = 512,
    HPFS_SUPERBLOCK_SECTOR = 16,
    HPFS_SPAREBLOCK_SECTOR = 17,
    HPFS_FIRST_DATA_SECTOR = 18, /* boot area, superblock and spare block come first */
    HPFS_BAND_SECTORS = 16384,   /* sectors covered by one band's free-space bitmap */
    HPFS_BITMAP_BYTES = 2048,    /* one band bitmap, or the directory band bitmap */
    HPFS_BITMAP_SECTORS = 4,
    HPFS_BITMAP_LIST_ENTRIES = HPFS_SECTOR_BYTES / 4, /* band bitmap sectors per list sector */

    HPFS_DNODE_BYTES = 2048,
    HPFS_DNODE_SECTORS = 4,
    HPFS_DIR_BAND_MAX_SECTORS = 65536,
    HPFS_MAX_CODEPAGES = 128,

    HPFS_STAGED_BUCKETS = 1024,
    HPFS_MAX_TREE_DEPTH = 32 /* deeper dnode or anode trees are treated as corrupt */

};

/* Superblock versions read and written: version 2, functional version 2 (volumes up to
   4 GB); later functional versions are read only. */
#define HPFS_VERSION 2
#define HPFS_FUNCTIONAL_VERSION 2
/* Smallest volume accepted, in sectors (1 MB). */
#define HPFS_MIN_SECTORS 2048u
/* Code page of a volume without code-page tables, and of new volumes. */
#define HPFS_DEFAULT_CODEPAGE 437

/* Superblock (sector 16) fields. */
enum
{
    HPFS_SUPER_SIGNATURE2 = 4,
    HPFS_SUPER_VERSION = 8,
    HPFS_SUPER_FUNCTIONAL_VERSION = 9, /* 2 for volumes up to 4 GB */
    HPFS_SUPER_ROOT = 12,
    HPFS_SUPER_SECTORS = 16,
    HPFS_SUPER_BITMAP_LIST = 24,
    HPFS_SUPER_BAD_LIST = 32,
    HPFS_SUPER_DIR_BAND_SECTORS = 48,
    HPFS_SUPER_DIR_BAND_START = 52,
    HPFS_SUPER_DIR_BAND_END = 56,
    HPFS_SUPER_DIR_BAND_BITMAP = 60,
    HPFS_SUPER_USER_TABLE = 96
};

/* Spare block (sector 17) fields. */
enum
{
    HPFS_SPARE_SIGNATURE2 = 4,
    HPFS_SPARE_FLAGS = 8,
    HPFS_SPARE_HOTFIX_LIST = 12,
    HPFS_SPARE_HOTFIX_USED = 16,
    HPFS_SPARE_HOTFIX_TOTAL = 20,
    HPFS_SPARE_DNODES = 24,
    HPFS_SPARE_FREE_DNODES = 28,
    HPFS_SPARE_CODEPAGE_INFO = 32,
    HPFS_SPARE_CODEPAGES = 36,
    HPFS_SPARE_SUPER_CHECKSUM = 40,
    HPFS_SPARE_CHECKSUM = 44,
    HPFS_SPARE_DNODE_LIST = 108
};

/* Spare block flags. */
enum
{
    HPFS_SPARE_DIRTY = 0x01,
    /* Dirty, spare dnodes used, hotfixes used, bad sectors, bad bitmap: the volume
       needs the original checker before it can be written. */
    HPFS_SPARE_NEEDS_CHECK = 0x1f
};

/* Boot sector fields read for volume information. */
enum
{
    HPFS_BOOT_SECTOR_BYTES = 11,
    HPFS_BOOT_SERIAL = 39,
    HPFS_BOOT_LABEL = 43,
    HPFS_BOOT_LABEL_BYTES = 11
};

/* Allocation tree header, held in an fnode or anode, followed by its entries. */
enum
{
    HPFS_TREE_FLAGS = 0,
    HPFS_TREE_FREE = 4,
    HPFS_TREE_USED = 5,
    HPFS_TREE_FIRST_FREE = 6,
    HPFS_TREE_ENTRIES = 8,
    HPFS_TREE_INTERNAL = 0x80,     /* flag: entries point to anodes */
    HPFS_TREE_PARENT_FNODE = 0x20, /* flag: the node's parent is an fnode */
    HPFS_LEAF_BYTES = 12,          /* logical sector, sector count, first sector */
    HPFS_LEAF_LOGICAL = 0,
    HPFS_LEAF_COUNT = 4,
    HPFS_LEAF_SECTOR = 8,
    HPFS_BRANCH_BYTES = 8, /* logical end of the subtree, anode sector */
    HPFS_BRANCH_END = 0,
    HPFS_BRANCH_SECTOR = 4
};

/* Fnode fields. */
enum
{
    HPFS_FNODE_NAME_LENGTH = 12,
    HPFS_FNODE_NAME = 13,
    HPFS_FNODE_NAME_BYTES = 15, /* the first 15 bytes of the name, for recovery tools */
    HPFS_FNODE_PARENT = 28,
    HPFS_FNODE_ACL = 32,
    HPFS_FNODE_ACL_INTERNAL_SIZE = 40,
    HPFS_FNODE_EAS = 44,
    HPFS_FNODE_EA_SECTOR = 48,
    HPFS_FNODE_EA_INTERNAL_SIZE = 52,
    HPFS_FNODE_EA_IN_ANODE = 54,
    HPFS_FNODE_FLAGS = 55,
    HPFS_FNODE_TREE = 56,
    HPFS_FNODE_TREE_BYTES = 104,
    HPFS_FNODE_LEAVES = 8,
    HPFS_FNODE_BRANCHES = 12,
    HPFS_FNODE_FILE_SIZE = 160,
    HPFS_FNODE_NEEDED_EAS = 164,
    HPFS_FNODE_INTERNAL_OFFSET = 184, /* start of the internal ACL, then internal EAs */
    HPFS_FNODE_FREE_AREA = 196,
    HPFS_FNODE_DIRECTORY = 0x01 /* HPFS_FNODE_FLAGS bit */
};

/* External ACL or EA run, at HPFS_FNODE_ACL or HPFS_FNODE_EAS. */
enum
{
    HPFS_RUN_SIZE = 0,
    HPFS_RUN_SECTOR = 4,
    HPFS_RUN_IN_ANODE = 10 /* nonzero: HPFS_RUN_SECTOR is an anode, not the first sector */
};

/* Anode fields. */
enum
{
    HPFS_ANODE_SELF = 4,
    HPFS_ANODE_PARENT = 8,
    HPFS_ANODE_TREE = 12,
    HPFS_ANODE_LEAVES = 40,
    HPFS_ANODE_BRANCHES = 60
};

/* Entries that fit in an anode's tree, which depends on whether they are leaves. */
static inline unsigned hpfs_anode_capacity(const unsigned char *anode)
{
    return anode[HPFS_ANODE_TREE + HPFS_TREE_FLAGS] & HPFS_TREE_INTERNAL ? HPFS_ANODE_BRANCHES
                                                                         : HPFS_ANODE_LEAVES;
}

/* Dnode fields. */
enum
{
    HPFS_DNODE_USED = 4, /* offset of the first free byte */
    HPFS_DNODE_TOP = 8,  /* 1 in the root dnode of a directory */
    HPFS_DNODE_PARENT = 12,
    HPFS_DNODE_SELF = 16,
    HPFS_DNODE_ENTRIES = 20
};

/* Directory entry fields. A down pointer, when present, occupies the last four bytes. */
enum
{
    HPFS_DIRENT_SIZE = 0,
    HPFS_DIRENT_FLAGS = 2,
    HPFS_DIRENT_ATTRIBUTES = 3,
    HPFS_DIRENT_FNODE = 4,
    HPFS_DIRENT_MODIFIED = 8,
    HPFS_DIRENT_FILE_SIZE = 12,
    HPFS_DIRENT_ACCESSED = 16,
    HPFS_DIRENT_CREATED = 20,
    HPFS_DIRENT_EA_SIZE = 24,
    HPFS_DIRENT_CODEPAGE = 29,
    HPFS_DIRENT_NAME_LENGTH = 30,
    HPFS_DIRENT_NAME = 31,
    HPFS_DIRENT_MIN_BYTES = 32,
    HPFS_DIRENT_DOWN_BYTES = 4,
    HPFS_DIRENT_CODEPAGE_INDEX = 0x7f, /* HPFS_DIRENT_CODEPAGE index bits */
    HPFS_DIRENT_CODEPAGE_HIGH = 0x80   /* not an index bit; names using it are unsupported */
};

/* Directory entry flags. */
enum
{
    HPFS_DIRENT_SPECIAL = 0x01, /* the ".." entry at the start of every directory */
    HPFS_DIRENT_DOWN = 0x04,
    HPFS_DIRENT_END = 0x08,
    HPFS_DIRENT_HAS_EAS = 0x10,
    HPFS_DIRENT_NEEDS_EAS = 0x80
};

/* Directory entry attributes: the FAT attribute byte plus a non-8.3 name flag. */
enum
{
    HPFS_ATTR_READONLY = 0x01,
    HPFS_ATTR_HIDDEN = 0x02,
    HPFS_ATTR_SYSTEM = 0x04,
    HPFS_ATTR_DIRECTORY = 0x10,
    HPFS_ATTR_ARCHIVED = 0x20,
    HPFS_ATTR_LONG_NAME = 0x40,
    HPFS_ATTR_CHANGEABLE = HPFS_ATTR_HIDDEN | HPFS_ATTR_SYSTEM | HPFS_ATTR_ARCHIVED
};

/* Code-page information sector and code-page data sector fields. */
enum
{
    HPFS_CPINFO_COUNT = 4,
    HPFS_CPINFO_FIRST_INDEX = 8,
    HPFS_CPINFO_NEXT = 12,
    HPFS_CPINFO_ENTRIES = 16,
    HPFS_CPINFO_ENTRY_BYTES = 16,
    HPFS_CPINFO_MAX_ENTRIES = 31,
    HPFS_CPINFO_COUNTRY = 0, /* entry fields */
    HPFS_CPINFO_CODEPAGE = 2,
    HPFS_CPINFO_CHECKSUM = 4,
    HPFS_CPINFO_DATA = 8,
    HPFS_CPINFO_INDEX = 12,
    HPFS_CPDATA_COUNT = 4,
    HPFS_CPDATA_FIRST_INDEX = 6,
    HPFS_CPDATA_CHECKSUMS = 8,
    HPFS_CPDATA_OFFSETS = 20,
    HPFS_CPDATA_MAX_TABLES = 3,
    HPFS_CPTABLE_COUNTRY = 0, /* table fields, at an HPFS_CPDATA_OFFSETS offset */
    HPFS_CPTABLE_CODEPAGE = 2,
    HPFS_CPTABLE_DBCS_RANGES = 4,
    HPFS_CPTABLE_CASE_MAP = 6, /* upper-case forms of characters 128-255 */
    HPFS_CPTABLE_BYTES = 136
};

/* Extended attribute record header and the name's terminating NUL. */
#define HPFS_EA_FIXED_BYTES 5u

/* Extended attribute record (FEA without the list's length prefix). */

enum
{
    HPFS_EA_FLAGS = 0,
    HPFS_EA_NAME_LENGTH = 1,
    HPFS_EA_VALUE_LENGTH = 2,
    HPFS_EA_NAME = 4,
    HPFS_EA_NEEDED = 0x80, /* HPFS_EA_FLAGS: the file needs this EA */
    HPFS_EA_METHOD = 0x7f, /* HPFS_EA_FLAGS: where the value is stored */
    HPFS_EA_INLINE = 0,
    HPFS_EA_EXTERNAL = 1, /* value points (HPFS_EA_POINTER_*) to contiguous sectors */
    HPFS_EA_TREE = 3,     /* value points (HPFS_EA_POINTER_*) to an anode */
    HPFS_EA_POINTER_BYTES = 8,
    HPFS_EA_POINTER_SIZE = 0,
    HPFS_EA_POINTER_SECTOR = 4,
    HPFS_EA_LIST_MAX = 65535 /* OS/2 FEA2 list limit */
};

/* A sector written by an operation, held until hpfs_commit writes it to the image. */
typedef struct HpfsPatch
{
    uint32_t sector;
    unsigned char data[HPFS_SECTOR_BYTES];
    struct HpfsPatch *next;
} HpfsPatch;

/* A code page from the volume's code-page table, with its case-folding map. */
typedef struct HpfsCodepage
{
    unsigned codepage;
    unsigned char map[256];
} HpfsCodepage;

/* An open HPFS volume. changes hashes staged sectors by sector number. */
typedef struct HpfsVolume
{
    FILE *file;
    uint64_t bytes;
    uint32_t base, sectors, root, bands, bitmap_list;
    uint32_t dir_start, dir_count, dir_map, cursor;
    unsigned codepage;
    HpfsCodepage *codepages;
    unsigned codepage_count;
    unsigned char case_map[256], boot[HPFS_SECTOR_BYTES], super[HPFS_SECTOR_BYTES],
        spare[HPFS_SECTOR_BYTES];
    unsigned char *bitmaps, dir_bitmap[HPFS_BITMAP_BYTES];
    uint32_t *bitmap_locations;
    int writable;
    HpfsPatch *changes[HPFS_STAGED_BUCKETS];
} HpfsVolume;

/* A copy of one directory entry record, without its down pointer, and the fnode of the
   directory holding it. */
typedef struct HpfsEntry
{
    unsigned char *record;
    size_t size;
    uint32_t parent;
} HpfsEntry;

/* A directory's entries in name order and the dnodes that held them. */
typedef struct HpfsDirectory
{
    HpfsEntry *entries;
    size_t count;
    uint32_t *nodes;
    size_t node_count;
} HpfsDirectory;

typedef struct HpfsExtent
{
    uint32_t logical, count, sector;
} HpfsExtent;

/* A file's fnode and its extents in logical order, with the anodes of its tree. */
typedef struct HpfsFile
{
    uint32_t sector;
    unsigned char data[HPFS_SECTOR_BYTES];
    HpfsExtent *extents;
    size_t count;
    uint32_t *nodes;
    size_t node_count;
} HpfsFile;

/* io.c. Routines returning int return 1 on success and 0 with errno set on failure. */
int hpfs_fail(int error);
int hpfs_read_sectors(HpfsVolume *d, uint32_t sector, void *bytes, unsigned count);
int hpfs_stage_sectors(HpfsVolume *d, uint32_t sector, const void *bytes, unsigned count);
/* Appends value to a node list, failing if it is already present (a cycle). */
int hpfs_add_unique_node(uint32_t **nodes, size_t *count, uint32_t value);
int hpfs_open(const char *path, int write, const char *expected, HpfsVolume *d);
void hpfs_close(HpfsVolume *d);

/* alloc.c */
int hpfs_load_bitmaps(HpfsVolume *d);
int hpfs_sector_is_free(HpfsVolume *d, uint32_t sector);
int hpfs_mark_sectors(HpfsVolume *d, uint32_t sector, uint32_t count, int free);
/* Returns the first sector of count free sectors, or 0 with errno ENOSPC. */
uint32_t hpfs_allocate(HpfsVolume *d, unsigned count, unsigned alignment);
uint32_t hpfs_allocate_dnode(HpfsVolume *d);
int hpfs_free_dnode(HpfsVolume *d, uint32_t sector);
uint32_t hpfs_checksum(const unsigned char *bytes, size_t n);
int hpfs_commit(HpfsVolume *d);

/* directory.c */
void hpfs_directory_free(HpfsDirectory *list);
int hpfs_directory_read(HpfsVolume *d, uint32_t fnode, HpfsDirectory *list);
char *hpfs_name_utf8(HpfsVolume *d, const unsigned char *entry);
char *hpfs_member_path(const char *path);
/* Reads the directory holding path into parent and sets index to its entry. With
   allow_missing, a missing final component succeeds with index SIZE_MAX. */
int hpfs_locate(HpfsVolume *d, const char *path, HpfsDirectory *parent, size_t *index,
                int allow_missing);
int hpfs_directory_save(HpfsVolume *d, uint32_t fnode, HpfsDirectory *list);
int hpfs_entry_insert(HpfsVolume *d, HpfsDirectory *list, HpfsEntry entry);
void hpfs_entry_remove(HpfsDirectory *list, size_t index);
int hpfs_entry_named(HpfsVolume *d, const char *path, const unsigned char *previous,
                     uint32_t parent, HpfsEntry *entry);

/* file.c */
void hpfs_file_free(HpfsFile *file);
int hpfs_extent_tree(HpfsVolume *d, HpfsFile *file, const unsigned char *header, unsigned capacity,
                     unsigned depth);
int hpfs_file_load(HpfsVolume *d, uint32_t sector, HpfsFile *file);
/* Returns the volume sector holding a logical file sector, or 0 with errno EIO. */
uint32_t hpfs_file_sector(HpfsFile *file, uint32_t logical);
int hpfs_file_copy(HpfsVolume *d, HpfsFile *file, FILE *output);
int hpfs_file_resize(HpfsVolume *d, HpfsFile *file, uint64_t bytes);

/* operations.c */
int hpfs_create_fnode(HpfsVolume *d, HpfsEntry *entry, int directory);
int hpfs_release_external(HpfsVolume *d, const unsigned char *fnode, unsigned offset);

/* ea.c */
int hpfs_ea_values_free(HpfsVolume *d, const unsigned char *fnode);
#endif
