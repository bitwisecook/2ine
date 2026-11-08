/* Public domain. No rights reserved. Wholly AI-authored. */
/* Walks, lists and opens members of ISO 9660 and FAT12/16 images held in files or read
   streams, such as images inside archive containers. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/volume/volume_disk.h"
#include "storage/filesystems/fat/fat12_16.h"
#include "storage/common/host_stdio.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int stream_disk(FILE *file, VolumeDisk *disk)
{
    memset(disk, 0, sizeof *disk);
    disk->file = file;
    if (fseeko(file, 0, SEEK_END))
        return 0;
    int64_t size = ftello(file);
    if (size < 0)
        return 0;
    disk->bytes = (uint64_t)size;
    return volume_identify(disk);
}

FILE *tb_image_stream_fopen(FILE *file, const char *member)
{
    VolumeDisk disk;
    if (!stream_disk(file, &disk))
    {
        free(disk.fat);
        return NULL;
    }
    FILE *out = volume_snapshot(&disk, member);
    free(disk.fat);
    return out;
}

typedef struct StreamWalk
{
    TbImageVisitor visitor;
    void *context;
    const char *parent;
    char **directories;
    size_t count, visited;
    int result;
} StreamWalk;

static int stream_entry(void *opaque, const char *name, const VolumeNode *node)
{
    StreamWalk *walk = opaque;
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return 1;
    if (++walk->visited > 100000u)
    {
        errno = EFBIG;
        return -1;
    }
    size_t n = strlen(walk->parent) + strlen(name) + 2;
    char *path = malloc(n);
    if (!path)
        return -1;
    snprintf(path, n, "%s%s%s", walk->parent, *walk->parent ? "/" : "", name);
    int result = walk->visitor(walk->context, path, node->size, node->directory, node->modified);
    if (result != 1)
        walk->result = result;
    if (result == 1 && node->directory)
    {
        if (walk->count >= 100000u)
        {
            free(path);
            errno = EFBIG;
            return -1;
        }
        char **next = realloc(walk->directories, (walk->count + 1) * sizeof *next);
        if (!next)
        {
            free(path);
            return -1;
        }
        walk->directories = next;
        walk->directories[walk->count++] = path;
    }
    else
        free(path);
    return result == 1 ? 1 : result == 2 ? 0 : -1;
}

static int disk_walk(VolumeDisk *disk, TbImageVisitor visitor, void *context)
{
    StreamWalk walk = {.visitor = visitor, .context = context, .parent = ""};
    int result = volume_entries(disk, disk->root, stream_entry, &walk);
    for (size_t i = 0; result == 1 && i < walk.count; i++)
    {
        walk.parent = walk.directories[i];
        VolumeNode node;
        result = volume_lookup(disk, walk.parent, &node)
                     ? volume_entries(disk, node, stream_entry, &walk)
                     : 0;
    }
    for (size_t i = 0; i < walk.count; i++)
        free(walk.directories[i]);
    free(walk.directories);
    return walk.result ? walk.result : result;
}

int tb_image_stream_walk(FILE *file, TbImageVisitor visitor, void *context)
{
    VolumeDisk disk;
    if (!stream_disk(file, &disk))
    {
        free(disk.fat);
        return 0;
    }
    int result = disk_walk(&disk, visitor, context);
    free(disk.fat);
    return result;
}

int tb_image_reader_walk(TbReadStream *stream, TbImageVisitor visitor, void *context)
{
    VolumeDisk disk;
    if (!volume_open_reader(stream, &disk))
    {
        if (!disk.index)
            free(disk.fat);
        return 0;
    }
    int result = disk_walk(&disk, visitor, context);
    if (!disk.index)
        free(disk.fat);
    return result;
}

int tb_image_reader_list(TbReadStream *stream, const char *member, TbImageVisitor visitor,
                         void *context)
{
    VolumeDisk disk;
    VolumeNode node;
    if (!volume_open_reader(stream, &disk))
    {
        free(disk.fat);
        return 0;
    }
    StreamWalk walk = {.visitor = visitor, .context = context, .parent = member};
    int result =
        volume_lookup(&disk, member, &node) ? volume_entries(&disk, node, stream_entry, &walk) : 0;
    for (size_t i = 0; i < walk.count; i++)
        free(walk.directories[i]);
    free(walk.directories);
    if (!disk.index)
        free(disk.fat);
    return walk.result ? walk.result : result;
}

typedef struct ClusterStream
{
    TbReadStream *parent;
    uint64_t data_offset;
    uint32_t cluster_bytes;
    uint32_t *clusters;
} ClusterStream;

static void cluster_close(void *context)
{
    ClusterStream *stream = context;
    tb_stream_close(stream->parent);
    free(stream->clusters);
    free(stream);
}

static int cluster_read(void *context, uint64_t offset, void *data, size_t size)
{
    ClusterStream *stream = context;
    unsigned char *output = data;
    while (size)
    {
        uint64_t index = offset / stream->cluster_bytes;
        size_t within = (size_t)(offset % stream->cluster_bytes);
        size_t amount = stream->cluster_bytes - within;
        if (amount > size)
            amount = size;
        uint64_t source = stream->data_offset +
                          (uint64_t)(stream->clusters[index] - 2) * stream->cluster_bytes + within;
        if (!tb_stream_read_at(stream->parent, source, output, amount))
            return 0;
        offset += amount;
        output += amount;
        size -= amount;
    }
    return 1;
}

TbReadStream *tb_image_reader_member(TbReadStream *stream, const char *member)
{
    VolumeDisk disk;
    VolumeNode node;
    TbReadStream *result = NULL;
    if (!volume_open_reader(stream, &disk) || !volume_lookup(&disk, member, &node))
        goto done;
    if (node.directory)
    {
        errno = EISDIR;
        goto done;
    }
    if (!disk.fat_bits)
    {
        result = tb_stream_slice(stream, node.offset, node.size);
        goto done;
    }
    size_t count = (size_t)((node.size + disk.cluster_bytes - 1) / disk.cluster_bytes);
    if (count > disk.clusters)
    {
        errno = EIO;
        goto done;
    }
    ClusterStream *context = calloc(1, sizeof *context);
    unsigned char *visited = calloc((disk.clusters + 9u) / 8u, 1);
    if (!context || !visited)
    {
        free(context);
        free(visited);
        goto done;
    }
    context->parent = tb_stream_retain(stream);
    context->data_offset = disk.data_offset;
    context->cluster_bytes = disk.cluster_bytes;
    context->clusters = calloc(count ? count : 1, sizeof *context->clusters);
    int valid = context->clusters != NULL;
    uint32_t cluster = node.cluster;
    for (size_t i = 0; valid && i < count; i++)
    {
        if (cluster < 2 || cluster >= disk.clusters + 2 ||
            cluster >= (disk.fat_bits == 12 ? 0xff0u : 0xfff0u) ||
            (visited[cluster / 8] & (1u << (cluster % 8))))
        {
            valid = volume_failure(EIO);
            break;
        }
        visited[cluster / 8] |= (unsigned char)(1u << (cluster % 8));
        context->clusters[i] = cluster;
        if (i + 1 < count)
            cluster = fat_next_cluster(&disk, cluster);
    }
    free(visited);
    if (valid)
        result = tb_stream_create(node.size, context, cluster_read, cluster_close);
    if (!result)
        cluster_close(context);
done:
    if (!disk.index)
        free(disk.fat);
    return result;
}

void tb_image_reader_cache_stats(TbReadStream *stream, uint64_t *builds, uint64_t *hits)
{
    directory_index_stats(stream, builds, hits);
}
