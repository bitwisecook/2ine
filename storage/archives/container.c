/* Public domain. No rights reserved. Wholly AI-authored. */
/* Container format detection and dispatch, nested-container resolution, member
   extraction, listing and probing for the tb_media API. */
#define _POSIX_C_SOURCE 200809L
#include "storage/container.h"
#include "storage/archives/container_internal.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "storage/volume.h"
#include "storage/read_stream.h"
#include "formats/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *source_open(const char *path)
{
    return tb_image_path(path) ? tb_image_fopen(path) : tb_host_fopen(path, "rb");
}

typedef struct DiskVisit
{
    TbArchiveVisit visit;
    void *context;
    TbReadStream *file;
} DiskVisit;

static int disk_visit(void *opaque, const char *name, uint64_t size, int directory,
                      int64_t modified)
{
    DiskVisit *d = opaque;
    return d->visit(d->context, name, size, directory, d->file, 0, 0, TB_ARCHIVE_DISK_IMAGE,
                    modified);
}

static int image_reader(TbReadStream *file)
{
    unsigned char magic[512] = {0}, iso[7] = {0};
    tb_stream_read_at(file, 0, magic,
                      tb_stream_size(file) < sizeof magic ? (size_t)tb_stream_size(file)
                                                          : sizeof magic);
    if (tb_stream_size(file) >= 16 * 2048 + sizeof iso)
        tb_stream_read_at(file, 16 * 2048, iso, sizeof iso);
    return !memcmp(iso + 1, "CD001", 5) ||
           (magic[510] == 0x55 && magic[511] == 0xaa && (magic[0] == 0xeb || magic[0] == 0xe9));
}

static void image_stats(TbReadStream *file, uint64_t before_builds, uint64_t before_hits)
{
    uint64_t builds, hits;
    tb_image_reader_cache_stats(file, &builds, &hits);
    tb_media_session_stats()->directory_index_builds += builds - before_builds;
    tb_media_session_stats()->directory_index_hits += hits - before_hits;
}

static int archive_walk(TbReadStream *file, TbArchiveVisit visit, void *context)
{
    unsigned char magic[512] = {0};
    tb_stream_seek(file, 0, SEEK_SET);
    tb_stream_read(magic, 1, sizeof magic, file);
    tb_stream_seek(file, 0, SEEK_SET);
    unsigned char iso[7] = {0};
    if (!tb_stream_seek(file, 16 * 2048, SEEK_SET))
        tb_stream_read(iso, 1, sizeof iso, file);
    tb_stream_seek(file, 0, SEEK_SET);
    if (!memcmp(iso + 1, "CD001", 5) ||
        (magic[510] == 0x55 && magic[511] == 0xaa && (magic[0] == 0xeb || magic[0] == 0xe9)))
    {
        DiskVisit d = {visit, context, file};
        return tb_image_reader_walk(file, disk_visit, &d);
    }
    if (tb_le32(magic + 20) == TB_ZOO_SIGNATURE)
        return tb_zoo_walk(file, visit, context);
    if (magic[0] == 0x1a)
        return tb_arc_walk(file, visit, context);
    if (magic[0] == 0x60 && magic[1] == 0xea)
        return tb_arj_walk(file, visit, context);
    if (magic[0] == 0xa5 && magic[1] == 0x96)
        return tb_ibm_pack_walk(file, visit, context);
    if (!memcmp(magic, "\x77\x04\x02\xbe", 4) || !memcmp(magic, "MZ", 2))
    {
        int rc = tb_warpin_walk(file, visit, context);
        if (rc || errno != ENOTSUP)
            return rc;
        if (!memcmp(magic, "MZ", 2))
        {
            rc = tb_arj_sfx_walk(file, visit, context);
            if (rc || errno != ENOTSUP)
                return rc;
        }
        tb_stream_seek(file, 0, SEEK_SET);
    }
    return tb_libarchive_walk(file, visit, context);
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
    TbArchiveExtract e = {.name = member, .parent = file};
    int result = archive_walk(file, tb_archive_extract_visit, &e);
    if (result != 2)
    {
        tb_stream_close(e.out);
        if (result == 1)
            tb_archive_fail(ENOENT);
        return NULL;
    }
    return e.out;
}

/* Resolve container boundaries iteratively: nesting consumes no C call stack. */
static TbReadStream *open_parent(const char *path, char **member)
{
    if (!tb_media_path(path))
        return NULL;
    const char *p = path + strlen(TB_MEDIA_PREFIX), *end = strchr(p, '!');
    if (!end)
        return NULL;
    char *host = tb_media_decode_part(p, (size_t)(end - p));
    if (!host)
        return NULL;
    tb_media_cache_select(host);
    TbReadStream *file = tb_media_cache_lookup(path, (size_t)(end - path));
    if (!file)
    {
        file = tb_stream_file(source_open(host), 1);
        TbReadBudget *budget = tb_media_cache_budget();
        if (file && budget)
            tb_stream_set_budget(file, budget);
        tb_media_cache_store(path, (size_t)(end - path), file);
    }
    free(host);
    if (!file)
        return NULL;
    p = end + 1;
    while ((end = strchr(p, '!')))
    {
        char *name = tb_media_decode_part(p, (size_t)(end - p));
        if (!name)
        {
            tb_stream_close(file);
            return NULL;
        }
        TbReadStream *inner = tb_media_cache_lookup(path, (size_t)(end - path));
        if (!inner)
        {
            inner = extract_member(file, name);
            tb_media_cache_store(path, (size_t)(end - path), inner);
        }
        free(name);
        tb_stream_close(file);
        if (!inner)
            return NULL;
        file = inner;
        p = end + 1;
    }
    *member = tb_media_decode_part(p, strlen(p));
    if (!*member)
    {
        tb_stream_close(file);
        return NULL;
    }
    return file;
}

FILE *tb_media_fopen(const char *path)
{
    char *member = NULL;
    TbReadStream *parent = open_parent(path, &member);
    if (!parent)
        return NULL;
    TbReadStream *stream = extract_member(parent, member);
    free(member);
    tb_stream_close(parent);
    if (!stream)
        return NULL;
    FILE *output = tmpfile();
    if (output)
        tb_media_session_stats()->materialized_files++;
    unsigned char buffer[65536];
    uint64_t offset = 0, length = tb_stream_size(stream);
    while (output && offset < length)
    {
        size_t amount = length - offset > sizeof buffer ? sizeof buffer : (size_t)(length - offset);
        if (!tb_stream_read_at(stream, offset, buffer, amount) ||
            fwrite(buffer, 1, amount, output) != amount)
        {
            fclose(output);
            output = NULL;
            break;
        }
        offset += amount;
    }
    tb_stream_close(stream);
    if (output && (fflush(output) || fseeko(output, 0, SEEK_SET)))
    {
        fclose(output);
        output = NULL;
    }
    return output;
}

typedef struct Listing
{
    char *member;
    TbMediaEntry *items;
    size_t count;
    int found;
} Listing;

static int list_visit(void *opaque, const char *name, uint64_t size, int directory, void *input,
                      uint64_t offset, uint64_t packed, TbArchiveKind kind, int64_t modified)
{
    (void)input;
    (void)offset;
    (void)packed;
    (void)kind;
    Listing *l = opaque;
    size_t prefix = strlen(l->member);
    if (prefix && !strcmp(name, l->member))
    {
        if (!directory)
            return tb_archive_fail(ENOTDIR);
        l->found = 1;
        return 1;
    }
    if (prefix && (strncmp(name, l->member, prefix) || name[prefix] != '/'))
        return 1;
    const char *tail = name + prefix + (prefix ? 1 : 0);
    if (!*tail)
        return 1;
    l->found = 1;
    const char *slash = strchr(tail, '/');
    size_t n = slash ? (size_t)(slash - tail) : strlen(tail);
    for (size_t i = 0; i < l->count; i++)
        if (strlen(l->items[i].name) == n && !strncmp(l->items[i].name, tail, n))
            return 1;
    TbMediaEntry *next = realloc(l->items, (l->count + 1) * sizeof *next);
    if (!next)
        return 0;
    l->items = next;
    char *copy = malloc(n + 1);
    if (!copy)
        return 0;
    memcpy(copy, tail, n);
    copy[n] = 0;
    l->items[l->count++] = (TbMediaEntry){.name = copy,
                                          .size = slash ? 0 : size,
                                          .directory = slash || directory,
                                          .modified = slash ? INT64_MIN : modified};
    return 1;
}

TbMediaEntry *tb_media_entries(const char *path, size_t *count)
{
    *count = (size_t)-1;
    Listing l = {0};
    TbReadStream *file = open_parent(path, &l.member);
    if (!file)
        return NULL;
    int ok;
    if (image_reader(file))
    {
        uint64_t builds, hits;
        tb_image_reader_cache_stats(file, &builds, &hits);
        DiskVisit visit = {list_visit, &l, file};
        ok = tb_image_reader_list(file, l.member, disk_visit, &visit);
        image_stats(file, builds, hits);
        if (ok)
            l.found = 1;
    }
    else
        ok = archive_walk(file, list_visit, &l);
    if (ok && *l.member && !l.found)
        ok = tb_archive_fail(ENOENT);
    free(l.member);
    tb_stream_close(file);
    if (!ok)
    {
        tb_media_entries_free(l.items, l.count);
        return NULL;
    }
    *count = l.count;
    return l.items;
}

void tb_media_entries_free(TbMediaEntry *entries, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(entries[i].name);
    free(entries);
}

static int probe_visit(void *opaque, const char *name, uint64_t size, int directory, void *input,
                       uint64_t offset, uint64_t packed, TbArchiveKind kind, int64_t modified)
{
    (void)opaque;
    (void)name;
    (void)size;
    (void)directory;
    (void)input;
    (void)offset;
    (void)packed;
    (void)kind;
    (void)modified;
    return 2;
}

int tb_media_container(const char *path)
{
    TbReadStream *file = NULL;
    if (tb_media_path(path))
    {
        char *member = NULL;
        TbReadStream *parent = open_parent(path, &member);
        if (parent)
        {
            file = tb_media_cache_lookup(path, strlen(path));
            if (!file)
                file = extract_member(parent, member);
            tb_stream_close(parent);
        }
        free(member);
    }
    else
        file = tb_stream_file(source_open(path), 1);
    if (!file)
        return 0;
    int ok = archive_walk(file, probe_visit, NULL);
    if ((ok == 2 || ok == 1) && tb_media_path(path))
        tb_media_cache_store(path, strlen(path), file);
    tb_stream_close(file);
    return ok == 2 || ok == 1;
}
