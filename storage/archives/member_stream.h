/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private lazy archive members. No intermediate container is written to disk. */
enum
{
    MEDIA_CACHE_BYTES = 1024 * 1024
};

typedef struct LazyArchive
{
    TbReadStream *parent, *self;
    char *name;
    struct archive *archive;
    ArchiveInput input;
    unsigned char *cache;
    size_t capacity, used;
    uint64_t start, decoded, expected;
    int raw;
} LazyArchive;

static int lazy_restart(LazyArchive *lazy)
{
    if (lazy->archive)
        archive_read_free(lazy->archive);
    lazy->archive = archive_read_new();
    if (!lazy->archive)
        return 0;
    struct archive *a = lazy->archive;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);
    lazy->input = (ArchiveInput){.file = lazy->parent, .length = tb_stream_size(lazy->parent)};
    archive_read_set_seek_callback(a, archive_input_seek);
    if (archive_read_open2(a, &lazy->input, NULL, archive_input_read, archive_input_skip, NULL) !=
        ARCHIVE_OK)
        return bad(EINVAL);
    struct archive_entry *entry;
    int found = 0;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        char *name = member_name(archive_entry_pathname(entry), 0);
        found = lazy->raw || (name && !strcmp(name, lazy->name));
        free(name);
        if (found)
        {
            if (archive_entry_is_encrypted(entry) == 1 || archive_entry_symlink(entry) ||
                archive_entry_hardlink(entry))
                return bad(EINVAL);
            break;
        }
    }
    lazy->used = 0;
    lazy->start = lazy->decoded = 0;
    return found || bad(ENOENT);
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
        return bad(EIO);
    size_t n = (size_t)count;
    if (lazy->used + n > lazy->capacity && lazy->capacity < MEDIA_CACHE_BYTES)
    {
        size_t capacity = lazy->capacity ? lazy->capacity * 2 : sizeof data;
        if (capacity > MEDIA_CACHE_BYTES)
            capacity = MEDIA_CACHE_BYTES;
        if (!tb_stream_cache_resize(lazy->self, capacity))
            return 0;
        unsigned char *next = realloc(lazy->cache, capacity);
        if (!next)
        {
            tb_stream_cache_resize(lazy->self, lazy->capacity);
            return 0;
        }
        lazy->cache = next;
        media_cache_peak();
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
    media_session.stats.decompressed_bytes += n;
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

static TbReadStream *lazy_member(TbReadStream *parent, const char *name, uint64_t size, int raw)
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

/* ZIP_STORED members can be addressed directly in the parent reader. Local
   header spans are checked independently of central-directory size metadata. */
static TbReadStream *stored_zip_member(TbReadStream *parent, struct archive *archive, uint64_t size)
{
    if ((archive_format(archive) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_ZIP)
        return NULL;
    la_int64_t position = archive_read_header_position(archive);
    unsigned char h[30];
    if (position < 0 || !tb_stream_read_at(parent, (uint64_t)position, h, sizeof h) ||
        memcmp(h, "PK\3\4", 4) || u16(h + 8) != 0 || (u16(h + 6) & 1))
        return NULL;
    uint64_t start = (uint64_t)position + sizeof h + u16(h + 26) + u16(h + 28);
    TbReadStream *stream = tb_stream_slice(parent, start, size);
    if (stream)
        media_session.stats.direct_slices++;
    return stream;
}

#include "storage/archives/zip/zip_legacy.h"
#include "storage/archives/zip/zip_seek.h"

typedef struct ExtractStream
{
    const char *name;
    TbReadStream *parent, *out;
} ExtractStream;

static int stream_extract_visit(void *opaque, const char *name, uint64_t size, int directory,
                                void *input, uint64_t offset, uint64_t packed, int kind,
                                int64_t modified)
{
    ExtractStream *e = opaque;
    if (strcmp(e->name, name))
        return 1;
    if (directory || size > MEDIA_MEMBER_LIMIT)
        return bad(directory ? EISDIR : EFBIG);
    if (kind == 3)
        e->out = tb_image_reader_member(input, name);
    else if (kind == 4)
        e->out = packed == size ? tb_stream_slice(input, offset, size) : NULL;
    else if (kind == 5)
    {
        TbReadStream *slice = tb_stream_slice(input, offset, packed);
        if (slice)
        {
            e->out = lazy_member(slice, name, size, 1);
            tb_stream_close(slice);
        }
    }
    else if (kind >= 96)
        e->out = zoo_member(input, offset, packed, size, kind);
    else if (kind >= 32)
        e->out = legacy_archive_member(input, offset, packed, size, kind);
    else if (kind)
    {
        unsigned char marker[8];
        /* FTCOMP's stored form is a four-byte marker followed by raw bytes. */
        if (kind == 2 && packed == size + 4 &&
            tb_stream_read_at(input, offset, marker, size >= 4 ? 8 : 4) &&
            (size < 4 || (memcmp(marker + 4, "fT19", 4) && memcmp(marker + 4, "fT21", 4))))
            e->out = tb_stream_slice(input, offset + 4, size);
        else
        {
            if (packed > 16u * 1024u * 1024u || size > 16u * 1024u * 1024u)
                return bad(EFBIG);
            unsigned char *data = malloc(packed ? (size_t)packed : 1), *output = NULL;
            int ok = data && tb_stream_read_at(input, offset, data, (size_t)packed) &&
                     tb_pack_decode(data, (size_t)packed, (size_t)size, kind, &output);
            free(data);
            if (ok)
                e->out = tb_stream_memory(output, (size_t)size);
        }
    }
    else
    {
        int legacy = 0;
        e->out = legacy_zip_member(e->parent, input, name, size, &legacy);
        if (legacy)
            return e->out ? 2 : 0;
        int indexed = 0;
        e->out = indexed_zip_member(e->parent, input, name, size, &indexed);
        if (indexed)
            return e->out ? 2 : 0;
        e->out = stored_zip_member(e->parent, input, size);
        if (!e->out)
            e->out = lazy_member(e->parent, name, size, 0);
    }
    return e->out ? 2 : 0;
}

static TbReadStream *extract_member(TbReadStream *file, const char *member)
{
    if (image_reader(file))
    {
        uint64_t builds, hits;
        tb_image_reader_cache_stats(file, &builds, &hits);
        TbReadStream *result = tb_image_reader_member(file, member);
        image_stats(file, builds, hits);
        return result;
    }
    ExtractStream e = {.name = member, .parent = file};
    int result = archive_walk(file, stream_extract_visit, &e);
    if (result != 2)
    {
        tb_stream_close(e.out);
        if (result == 1)
            bad(ENOENT);
        return NULL;
    }
    return e.out;
}
