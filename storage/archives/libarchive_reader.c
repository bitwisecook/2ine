/* Public domain. No rights reserved. Wholly AI-authored. */
/* libarchive over TbReadStream: input callbacks, directory walks and lazily decoded
   members. No intermediate container is written to disk. */
#define _POSIX_C_SOURCE 200809L
#include "storage/archives/container_internal.h"
#include "storage/common/host_stdio.h"
#include "third_party/libarchive/archive.h"
#include "third_party/libarchive/archive_entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static la_ssize_t archive_input_read(struct archive *a, void *opaque, const void **out)
{
    (void)a;
    TbArchiveInput *in = opaque;
    *out = in->buffer;
    size_t want = in->length - in->position > sizeof in->buffer
                      ? sizeof in->buffer
                      : (size_t)(in->length - in->position);
    if (!tb_stream_read_at(in->file, in->base + in->position, in->buffer, want))
        return -1;
    in->position += want;
    return (la_ssize_t)want;
}

static la_int64_t archive_input_seek(struct archive *a, void *opaque, la_int64_t offset, int whence)
{
    (void)a;
    TbArchiveInput *in = opaque;
    uint64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? in->position : in->length;
    uint64_t distance = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : (uint64_t)offset;
    if (offset < 0 ? distance > base : distance > in->length - base)
        return -1;
    in->position = offset < 0 ? base - distance : base + distance;
    return (la_int64_t)in->position;
}

static la_int64_t archive_input_skip(struct archive *a, void *opaque, la_int64_t count)
{
    TbArchiveInput *in = opaque;
    uint64_t n = count > 0 ? (uint64_t)count : 0;
    if (n > in->length - in->position)
        n = in->length - in->position;
    return archive_input_seek(a, opaque, (la_int64_t)n, SEEK_CUR) < 0 ? 0 : (la_int64_t)n;
}

int tb_libarchive_open(struct archive **archive, TbArchiveInput *input)
{
    struct archive *a = archive_read_new();
    *archive = a;
    if (!a)
        return 0;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);
    archive_read_set_seek_callback(a, archive_input_seek);
    return archive_read_open2(a, input, NULL, archive_input_read, archive_input_skip, NULL) ==
           ARCHIVE_OK;
}

int tb_libarchive_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    tb_stream_seek(file, 0, SEEK_END);
    int64_t length = tb_stream_tell(file);
    tb_stream_seek(file, 0, SEEK_SET);
    TbArchiveInput in = {.file = file, .length = length < 0 ? 0 : (uint64_t)length};
    struct archive *a;
    int ok = tb_libarchive_open(&a, &in);
    if (!a)
        return 0;
    struct archive_entry *entry;
    unsigned count = 0;
    while (ok)
    {
        int rc = archive_read_next_header(a, &entry);
        if (rc == ARCHIVE_EOF)
            break;
        if (rc != ARCHIVE_OK || ++count > TB_MEDIA_ENTRY_LIMIT ||
            archive_entry_is_encrypted(entry) == 1)
        {
            ok = tb_archive_fail(EINVAL);
            break;
        }
        if (archive_format(a) == ARCHIVE_FORMAT_RAW &&
            archive_filter_code(a, 0) == ARCHIVE_FILTER_NONE)
        {
            ok = tb_archive_fail(ENOTSUP);
            break;
        }
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry))
            continue;
        char *name = tb_media_sanitise_member(archive_entry_pathname(entry), 0);
        if (!name || !*name)
        {
            free(name);
            continue;
        }
        int directory = archive_entry_filetype(entry) == AE_IFDIR;
        /* A requested ordinary member streams directly into a private snapshot. */
        int result = visit(context, name, (uint64_t)archive_entry_size(entry), directory, (void *)a,
                           0, 0, TB_ARCHIVE_LIBARCHIVE,
                           archive_entry_mtime_is_set(entry) ? (int64_t)archive_entry_mtime(entry)
                                                             : INT64_MIN);
        free(name);
        if (result != 1)
        {
            ok = result;
            break;
        }
    }
    archive_read_free(a);
    return ok;
}

typedef struct LazyArchive
{
    TbReadStream *parent, *self;
    char *name;
    struct archive *archive;
    TbArchiveInput input;
    unsigned char *cache;
    size_t capacity, used;
    uint64_t start, decoded, expected;
    int raw;
} LazyArchive;

static int lazy_restart(LazyArchive *lazy)
{
    if (lazy->archive)
        archive_read_free(lazy->archive);
    lazy->input = (TbArchiveInput){.file = lazy->parent, .length = tb_stream_size(lazy->parent)};
    if (!tb_libarchive_open(&lazy->archive, &lazy->input))
        return lazy->archive ? tb_archive_fail(EINVAL) : 0;
    struct archive *a = lazy->archive;
    struct archive_entry *entry;
    int found = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        char *name = tb_media_sanitise_member(archive_entry_pathname(entry), 0);
        found = lazy->raw || (name && !strcmp(name, lazy->name));
        free(name);
        if (found)
        {
            if (archive_entry_is_encrypted(entry) == 1 || archive_entry_symlink(entry) ||
                archive_entry_hardlink(entry))
                return tb_archive_fail(EINVAL);
            break;
        }
    }
    lazy->used = 0;
    lazy->start = lazy->decoded = 0;
    return found || tb_archive_fail(ENOENT);
}

static void lazy_close(void *opaque)
{
    LazyArchive *lazy = opaque;
    if (lazy->archive)
        archive_read_free(lazy->archive);
    tb_stream_close(lazy->parent);
    free(lazy->name);
    free(lazy->cache);
    free(lazy);
}

static int lazy_append(LazyArchive *lazy)
{
    unsigned char data[65536];
    la_ssize_t count = archive_read_data(lazy->archive, data, sizeof data);
    if (count <= 0 || (uint64_t)count > lazy->expected - lazy->decoded)
        return tb_archive_fail(EIO);
    size_t n = (size_t)count;
    if (lazy->used + n > lazy->capacity && lazy->capacity < TB_MEDIA_CACHE_BYTES)
    {
        size_t capacity = lazy->capacity ? lazy->capacity * 2 : sizeof data;
        if (capacity > TB_MEDIA_CACHE_BYTES)
            capacity = TB_MEDIA_CACHE_BYTES;
        if (!tb_stream_cache_resize(lazy->self, capacity))
            return 0;
        unsigned char *next = realloc(lazy->cache, capacity);
        if (!next)
        {
            tb_stream_cache_resize(lazy->self, lazy->capacity);
            return 0;
        }
        lazy->cache = next;
        tb_media_cache_peak();
        lazy->capacity = capacity;
    }
    if (lazy->used + n > lazy->capacity)
    {
        size_t discard = lazy->used + n - lazy->capacity;
        memmove(lazy->cache, lazy->cache + discard, lazy->used - discard);
        lazy->start += discard;
        lazy->used -= discard;
    }
    memcpy(lazy->cache + lazy->used, data, n);
    lazy->used += n;
    lazy->decoded += n;
    tb_media_session_stats()->decompressed_bytes += n;
    return 1;
}

static int lazy_read(void *opaque, uint64_t offset, void *data, size_t size)
{
    LazyArchive *lazy = opaque;
    unsigned char *output = data;
    if ((!lazy->archive || offset < lazy->start) && !lazy_restart(lazy))
        return 0;
    while (size)
    {
        while (offset >= lazy->decoded)
            if (!lazy_append(lazy))
                return 0;
        size_t within = (size_t)(offset - lazy->start);
        size_t amount = lazy->used - within;
        if (amount > size)
            amount = size;
        memcpy(output, lazy->cache + within, amount);
        offset += amount;
        output += amount;
        size -= amount;
    }
    return 1;
}

TbReadStream *tb_libarchive_member(TbReadStream *parent, const char *name, uint64_t size, int raw)
{
    LazyArchive *lazy = calloc(1, sizeof *lazy);
    if (!lazy)
        return NULL;
    lazy->parent = tb_stream_retain(parent);
    lazy->name = strdup(name);
    lazy->expected = size;
    lazy->raw = raw;
    TbReadStream *stream = lazy->name ? tb_stream_create(size, lazy, lazy_read, lazy_close) : NULL;
    lazy->self = stream;
    tb_stream_inherit_budget(stream, parent);
    if (!stream)
        lazy_close(lazy);
    return stream;
}
