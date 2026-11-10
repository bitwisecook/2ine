/* Public domain. No rights reserved. Wholly AI-authored. */
/* Directory-record cache attached to a read stream for repeated media lookups. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/volume/volume_disk.h"
#include "storage/filesystems/iso9660/iso9660.h"
#include "storage/common/host_stdio.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Cache directory records (including file/cluster offsets), not file contents.
   Only directories needed by a lookup/list are parsed. Ownership follows the
   immutable media reader, so replacing the source discards the entire index. */
typedef struct IndexedEntry
{
    char *name;
    VolumeNode node;
} IndexedEntry;

typedef struct IndexedDirectory
{
    struct IndexedDirectory *next;
    VolumeNode node;
    IndexedEntry *entries;
    size_t count, capacity, bytes;
} IndexedDirectory;

enum
{
    DIRECTORY_BUCKETS = 256,
    /* Larger directories are not indexed; lookups fall back to a scan. */
    MAX_DIRECTORY_ENTRIES = 100000
};

typedef struct DirectoryIndex
{
    VolumeDisk disk;
    IndexedDirectory *directories[DIRECTORY_BUCKETS];
    size_t bytes;
    uint64_t builds, hits;
} DirectoryIndex;

static void indexed_directory_free(IndexedDirectory *directory)
{
    for (size_t i = 0; i < directory->count; i++)
        free(directory->entries[i].name);
    free(directory->entries);
    free(directory);
}

static void directory_index_free(void *opaque)
{
    DirectoryIndex *index = opaque;
    for (size_t i = 0; i < DIRECTORY_BUCKETS; i++)
    {
        IndexedDirectory *directory = index->directories[i];
        while (directory)
        {
            IndexedDirectory *next = directory->next;
            indexed_directory_free(directory);
            directory = next;
        }
    }
    free(index->disk.fat);
    free(index);
}

typedef struct IndexBuild
{
    DirectoryIndex *index;
    IndexedDirectory *directory;
} IndexBuild;

static int indexed_entry(void *opaque, const char *name, const VolumeNode *node)
{
    IndexBuild *build = opaque;
    IndexedDirectory *directory = build->directory;
    size_t length = strlen(name) + 1;
    size_t capacity = directory->capacity;
    if (directory->count == capacity)
        capacity = capacity ? capacity * 2 : 16;
    if (directory->count == MAX_DIRECTORY_ENTRIES ||
        !tb_stream_index_resize(build->index->disk.stream,
                                build->index->bytes + directory->bytes + length +
                                    (capacity - directory->capacity) * sizeof(IndexedEntry)))
        return -1;
    IndexedEntry *entries = realloc(directory->entries, capacity * sizeof *entries);
    if (!entries)
        return -1;
    directory->entries = entries;
    directory->bytes += (capacity - directory->capacity) * sizeof *entries;
    directory->capacity = capacity;
    char *copy = strdup(name);
    if (!copy)
        return -1;
    directory->bytes += length;
    directory->entries[directory->count++] = (IndexedEntry){copy, *node};
    return 1;
}

int volume_entries(VolumeDisk *disk, VolumeNode node, VolumeEntryCallback callback, void *context)
{
    if (!node.directory)
        return volume_failure(ENOTDIR);
    DirectoryIndex *index = disk->index;
    if (!index)
        return volume_read_entries(disk, node, callback, context);
    size_t bucket =
        (size_t)((node.offset / ISO_SECTOR_BYTES) ^ node.cluster ^ node.size) % DIRECTORY_BUCKETS;
    IndexedDirectory *directory = index->directories[bucket];
    while (directory &&
           (directory->node.offset != node.offset || directory->node.cluster != node.cluster ||
            directory->node.size != node.size))
        directory = directory->next;
    if (directory)
        index->hits++;
    else
    {
        directory = calloc(1, sizeof *directory);
        if (!directory)
            return volume_read_entries(disk, node, callback, context);
        directory->node = node;
        directory->bytes = sizeof *directory;
        IndexBuild build = {index, directory};
        if (!volume_read_entries(disk, node, indexed_entry, &build))
        {
            indexed_directory_free(directory);
            tb_stream_index_resize(disk->stream, index->bytes);
            return volume_read_entries(disk, node, callback, context);
        }
        if (!tb_stream_index_resize(disk->stream, index->bytes + directory->bytes))
        {
            indexed_directory_free(directory);
            return volume_read_entries(disk, node, callback, context);
        }
        directory->next = index->directories[bucket];
        index->directories[bucket] = directory;
        index->bytes += directory->bytes;
        index->builds++;
    }
    for (size_t i = 0; i < directory->count; i++)
    {
        int result = callback(context, directory->entries[i].name, &directory->entries[i].node);
        if (result <= 0)
            return result == 0;
    }
    return 1;
}

int volume_open_reader(TbReadStream *stream, VolumeDisk *disk)
{
    DirectoryIndex *index = tb_stream_index(stream);
    if (index)
    {
        *disk = index->disk;
        return 1;
    }
    memset(disk, 0, sizeof *disk);
    disk->stream = stream;
    disk->bytes = tb_stream_size(stream);
    if (!volume_identify(disk))
        return 0;
    index = calloc(1, sizeof *index);
    if (index)
    {
        index->bytes = sizeof *index + disk->fat_size;
        if (tb_stream_set_index(stream, index, index->bytes, directory_index_free))
        {
            disk->index = index;
            index->disk = *disk;
        }
        else
            free(index);
    }
    return 1;
}

void directory_index_stats(TbReadStream *stream, uint64_t *builds, uint64_t *hits)
{
    DirectoryIndex *index = tb_stream_index(stream);
    *builds = index ? index->builds : 0;
    *hits = index ? index->hits : 0;
}
