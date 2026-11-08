/* Public domain. No rights reserved. Wholly AI-authored. */
/* HPFS v2 volume state and the routines shared by the HPFS source files. All sector
   numbers are relative to the selected volume. */
#ifndef TB_STORAGE_HPFS_INTERNAL_H
#define TB_STORAGE_HPFS_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define HPFS_SUPERBLOCK_SIGNATURE 0xf995e849u
#define HPFS_SPAREBLOCK_SIGNATURE 0xf9911849u
#define HPFS_FNODE_SIGNATURE 0xf7e40aaeu
#define HPFS_ANODE_SIGNATURE 0x37e40aaeu
#define HPFS_DNODE_SIGNATURE 0x77e40aaeu
/* Upper bound on tree nodes and extents per file, so a corrupt tree cannot exhaust memory. */
#define HPFS_NODE_LIMIT 1048576u

/* A sector written by an operation, held until hpfs_commit writes it to the image. */
typedef struct HpfsPatch
{
    uint32_t sector;
    unsigned char data[512];
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
    unsigned char case_map[256], boot[512], super[512], spare[512];
    unsigned char *bitmaps, dir_bitmap[2048];
    uint32_t *bitmap_locations;
    int writable;
    HpfsPatch *changes[1024];
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
    unsigned char data[512];
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
