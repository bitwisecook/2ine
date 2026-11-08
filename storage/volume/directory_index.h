/* Public domain. No rights reserved. Wholly AI-authored. */
/* Cache directory records (including file/cluster offsets), not file contents.
   Only directories needed by a lookup/list are parsed. Ownership follows the
   immutable media reader, so replacing the source discards the entire index. */
typedef struct IndexedEntry
{
    char *name;
    Node node;
} IndexedEntry;

typedef struct IndexedDirectory
{
    struct IndexedDirectory *next;
    Node node;
    IndexedEntry *entries;
    size_t count, capacity, bytes;
} IndexedDirectory;

typedef struct ImageIndex
{
    Disk disk;
    IndexedDirectory *directories[256];
    size_t bytes;
    uint64_t builds, hits;
} ImageIndex;

static void indexed_directory_free(IndexedDirectory *directory)
{
    for (size_t i = 0; i < directory->count; i++)
        free(directory->entries[i].name);
    free(directory->entries);
    free(directory);
}

static void image_index_free(void *opaque)
{
    ImageIndex *index = opaque;
    for (size_t i = 0; i < 256; i++)
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
    ImageIndex *index;
    IndexedDirectory *directory;
} IndexBuild;

static int indexed_entry(void *opaque, const char *name, const Node *node)
{
    IndexBuild *build = opaque;
    IndexedDirectory *directory = build->directory;
    size_t length = strlen(name) + 1;
    size_t capacity = directory->capacity;
    if (directory->count == capacity)
        capacity = capacity ? capacity * 2 : 16;
    if (directory->count == 100000 ||
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

static int entries(Disk *disk, Node node, EntryCallback callback, void *context)
{
    if (!node.directory)
        return failure(ENOTDIR);
    ImageIndex *index = disk->index;
    if (!index)
        return read_entries(disk, node, callback, context);
    size_t bucket = (size_t)((node.offset / SECTOR) ^ node.cluster ^ node.size) % 256;
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
            return read_entries(disk, node, callback, context);
        directory->node = node;
        directory->bytes = sizeof *directory;
        IndexBuild build = {index, directory};
        if (!read_entries(disk, node, indexed_entry, &build))
        {
            indexed_directory_free(directory);
            tb_stream_index_resize(disk->stream, index->bytes);
            return read_entries(disk, node, callback, context);
        }
        if (!tb_stream_index_resize(disk->stream, index->bytes + directory->bytes))
        {
            indexed_directory_free(directory);
            return read_entries(disk, node, callback, context);
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
