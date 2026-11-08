/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#include "ball/media.h"
#include "ball/file_time.h"
#include "ball/platform.h"
#include "ball/disk_image.h"
#include "ball/pack.h"
#include "ball/read_stream.h"
#include "ball/vendor/libarchive/archive.h"
#include "ball/vendor/libarchive/archive_entry.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef _WIN32
#define strdup _strdup
#define fseeko _fseeki64
#define ftello _ftelli64
#endif
#define MEDIA_PREFIX "/@2ine-media/"
#define MEDIA_MEMBER_LIMIT (UINT64_C(2) * 1024 * 1024 * 1024)
#define MEDIA_ENTRY_LIMIT 100000u
#include "ball/media_cache.h"

static unsigned u16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t u32(const unsigned char *p)
{
    return u16(p) | (uint32_t)u16(p + 2) << 16;
}

static int bad(int error)
{
    errno = error;
    return 0;
}

int tb_media_path(const char *path)
{
    return path && !strncmp(path, MEDIA_PREFIX, strlen(MEDIA_PREFIX));
}

static char *encode(const char *p)
{
    static const char digits[] = "0123456789abcdef";
    size_t n = strlen(p);
    if (n > (SIZE_MAX - 1) / 2)
        return NULL;
    char *out = malloc(n * 2 + 1);
    if (!out)
        return NULL;
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i] = digits[(unsigned char)p[i] >> 4];
        out[2 * i + 1] = digits[(unsigned char)p[i] & 15];
    }
    out[n * 2] = 0;
    return out;
}

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static char *decode(const char *p, size_t n)
{
    if (n % 2)
    {
        bad(EINVAL);
        return NULL;
    }
    char *out = malloc(n / 2 + 1);
    if (!out)
        return NULL;
    for (size_t i = 0; i < n; i += 2)
    {
        int a = hex(p[i]), b = hex(p[i + 1]);
        if (a < 0 || b < 0 || !(a | b))
        {
            free(out);
            bad(EINVAL);
            return NULL;
        }
        out[i / 2] = (char)((a << 4) | b);
    }
    out[n / 2] = 0;
    return out;
}

static char *append(const char *a, const char *b, const char *suffix)
{
    size_t n = strlen(a) + strlen(b) + strlen(suffix) + 1;
    char *out = malloc(n);
    if (out)
        snprintf(out, n, "%s%s%s", a, b, suffix);
    return out;
}

char *tb_media_root(const char *source)
{
    if (tb_media_path(source))
        return append(source, "", "!");
    char *absolute = tb_canonical(source);
    if (!absolute)
        return NULL;
    char *encoded = encode(absolute);
    free(absolute);
    if (!encoded)
        return NULL;
    char *out = append(MEDIA_PREFIX, encoded, "!");
    free(encoded);
    return out;
}

/* Archive member names are relative and never interpreted by the host filesystem. */
static char *member_name(const char *name, int pack)
{
    if (!name)
        return NULL;
    if (pack)
        while (*name == '/' || *name == '\\')
            name++;
    else if (*name == '/' || *name == '\\')
        return NULL;
    char *out = strdup(name);
    if (!out)
        return NULL;
    for (char *p = out; *p; p++)
        if (*p == '\\')
            *p = '/';
    size_t used = 0;
    for (char *p = out; *p;)
    {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if ((n == 2 && !strncmp(p, "..", 2)) || memchr(p, ':', n))
        {
            free(out);
            return NULL;
        }
        if (n != 1 || *p != '.')
        {
            if (used)
                out[used++] = '/';
            memmove(out + used, p, n);
            used += n;
        }
        p = end ? end + 1 : p + n;
    }
    out[used] = 0;
    return out;
}

static char *last_member(const char *path)
{
    const char *last = strrchr(path, '!');
    return last ? decode(last + 1, strlen(last + 1)) : NULL;
}

char *tb_media_member_name(const char *path)
{
    if (tb_media_path(path))
        return last_member(path);
    const char *base = strrchr(path, '/');
    return strdup(base ? base + 1 : path);
}

char *tb_media_host_source(const char *path)
{
    if (!tb_media_path(path))
        return tb_canonical(path);
    const char *begin = path + strlen(MEDIA_PREFIX), *end = strchr(begin, '!');
    if (!end)
        return NULL;
    char *decoded = decode(begin, (size_t)(end - begin));
    if (!decoded)
        return NULL;
    char *canonical = tb_canonical(decoded);
    free(decoded);
    return canonical;
}

char *tb_media_child(const char *directory, const char *name)
{
    if (!tb_media_path(directory) || strpbrk(name, "/\\:") || !strcmp(name, ".") ||
        !strcmp(name, ".."))
    {
        bad(EINVAL);
        return NULL;
    }
    const char *last = strrchr(directory, '!');
    if (!last)
        return NULL;
    char *member = last_member(directory);
    if (!member)
        return NULL;
    char *full = append(member, *member ? "/" : "", name);
    free(member);
    if (!full)
        return NULL;
    char *encoded = encode(full);
    free(full);
    if (!encoded)
        return NULL;
    size_t n = (size_t)(last - directory) + 1;
    char *out = malloc(n + strlen(encoded) + 1);
    if (out)
    {
        memcpy(out, directory, n);
        strcpy(out + n, encoded);
    }
    free(encoded);
    return out;
}

char *tb_media_parent(const char *path)
{
    if (!tb_media_path(path))
        return NULL;
    const char *last = strrchr(path, '!');
    if (!last)
        return NULL;
    char *member = last_member(path);
    if (!member)
        return NULL;
    char *out = NULL;
    if (*member)
    {
        char *slash = strrchr(member, '/');
        if (slash)
            *slash = 0;
        else
            *member = 0;
        char *encoded = encode(member);
        if (encoded)
        {
            size_t n = (size_t)(last - path) + 1;
            out = malloc(n + strlen(encoded) + 1);
            if (out)
            {
                memcpy(out, path, n);
                strcpy(out + n, encoded);
            }
            free(encoded);
        }
    }
    else
    {
        char *source = strdup(path);
        if (source)
        {
            source[last - path] = 0;
            if (strchr(source + strlen(MEDIA_PREFIX), '!'))
                out = tb_media_parent(source);
            free(source);
        }
    }
    free(member);
    return out;
}

char *tb_media_label(const char *path)
{
    if (!tb_media_path(path))
        return strdup(path);
    const char *p = path + strlen(MEDIA_PREFIX);
    char *out = strdup("");
    if (!out)
        return NULL;
    while (1)
    {
        const char *end = strchr(p, '!');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char *part = decode(p, n);
        if (!part)
        {
            free(out);
            return NULL;
        }
        char *next = append(out, *out ? " ▸ " : "", part);
        free(out);
        free(part);
        out = next;
        if (!out || !end)
            return out;
        p = end + 1;
    }
}

typedef int (*Visit)(void *, const char *, uint64_t, int, void *, uint64_t, uint64_t, int, int64_t);

/* The callback may request one member; extraction never uses archive paths as host paths. */
static int pack_walk(TbReadStream *file, Visit visit, void *context)
{
    if (tb_stream_seek(file, 0, SEEK_END))
        return 0;
    int64_t length = tb_stream_tell(file);
    uint64_t at = 0;
    for (unsigned entries = 0; entries < MEDIA_ENTRY_LIMIT; entries++)
    {
        unsigned char h[41];
        if (at > (uint64_t)length || tb_stream_seek(file, (int64_t)at, SEEK_SET) ||
            tb_stream_read(h, 1, 24, file) != 24 || h[0] != 0xa5 || h[1] != 0x96)
            return bad(EINVAL);
        unsigned kind = u16(h + 2);
        uint64_t end = u32(h + 20);
        if (!end)
            end = (uint64_t)length;
        if (end <= at || end > (uint64_t)length)
            return bad(EINVAL);
        unsigned header = kind == 0xfffd ? 41 : 26;
        if (kind != 0xfffd && kind != 0xfffe)
            return bad(ENOTSUP);
        if (tb_stream_read(h + 24, 1, header - 24, file) != header - 24)
            return bad(EIO);
        if (kind == 0xfffd && memcmp(h + 24, "FTCOMP", 6))
            return bad(EINVAL);
        unsigned n = u16(h + header - 2);
        if (!n || n > 4096 || at + header + n > end)
            return bad(EINVAL);
        char *raw = calloc(1, n + 1);
        if (!raw)
            return 0;
        if (tb_stream_read(raw, 1, n, file) != n)
        {
            free(raw);
            return bad(EIO);
        }
        char *name = member_name(raw, 1);
        free(raw);
        uint64_t data = at + header + n, ea = u32(h + 12), data_end = ea ? ea : end;
        if (data_end < data || data_end > end)
        {
            free(name);
            return bad(EINVAL);
        }
        if (name && *name)
        {
            int result = visit(context, name, u32(h + 16), 0, file, data, data_end - data,
                               kind == 0xfffd ? 2 : 1, tb_dos_time(u16(h + 4), u16(h + 6)));
            free(name);
            if (result != 1)
                return result;
        }
        else
            free(name);
        if (!u32(h + 20))
            return 1;
        at = end;
    }
    return bad(EFBIG);
}

/* WarpIN revisions 3/4: WIArcHeader (0x214), WIPackHeader (48), WIFileHeader (285). */
static int wpi_walk(TbReadStream *file, Visit visit, void *context)
{
    if (tb_stream_seek(file, 0, SEEK_END))
        return 0;
    int64_t length = tb_stream_tell(file);
    if (length < 532)
        return bad(ENOTSUP);
    unsigned char head[572];
    int64_t at = 0;
    int64_t tail = length >= 572 ? ((length - 572) / 512) * 512 : 0;
    if (tb_stream_seek(file, tail, SEEK_SET) || tb_stream_read(head, 1, 532, file) != 532)
        return bad(EIO);
    if (memcmp(head, "\x77\x04\x02\xbe", 4))
    {
        size_t n = (size_t)(length < 200532 ? length : 200532);
        unsigned char *front = malloc(n);
        if (!front)
            return 0;
        tb_stream_seek(file, 0, SEEK_SET);
        if (tb_stream_read(front, 1, n, file) != n)
        {
            free(front);
            return bad(EIO);
        }
        int found = 0;
        for (size_t i = 0; i + 532 <= n; i++)
            if (!memcmp(front + i, "\x77\x04\x02\xbe", 4))
            {
                at = (int64_t)i;
                memcpy(head, front + i, 532);
                found = 1;
                break;
            }
        free(front);
        if (!found)
            return bad(ENOTSUP);
    }
    else
        at = tail;
    unsigned revision = u16(head + 4), packages = u16(head + 522);
    uint64_t data = (uint64_t)at + 532;
    if (revision == 4)
    {
        if (tb_stream_seek(file, at + 532, SEEK_SET) ||
            tb_stream_read(head + 532, 1, 40, file) != 40)
            return bad(EIO);
        unsigned ext = u32(head + 532);
        if (ext < 40)
            return bad(EINVAL);
        data = (uint64_t)u32(head + 536) + 532 + ext;
    }
    else if (revision != 3)
        return bad(ENOTSUP);
    uint64_t script = u16(head + 526), orig = u16(head + 524);
    if (script)
    {
        if (data + script > (uint64_t)length)
            return bad(EINVAL);
        int rc = visit(context, "install.wis", orig, 0, file, data, script, 5, INT64_MIN);
        if (rc != 1)
            return rc;
    }
    data += script + u32(head + 528);
    if (packages > MEDIA_ENTRY_LIMIT || data + (uint64_t)packages * 48 > (uint64_t)length)
        return bad(EINVAL);
    unsigned count = 0;
    for (unsigned p = 0; p < packages; p++)
    {
        unsigned char package[48];
        if (tb_stream_seek(file, (int64_t)(data + (uint64_t)p * 48), SEEK_SET) ||
            tb_stream_read(package, 1, 48, file) != 48)
            return bad(EIO);
        unsigned files = u16(package + 2);
        uint64_t pos = u32(package + 4);
        for (unsigned f = 0; f < files; f++)
        {
            unsigned char h[285];
            if (++count > MEDIA_ENTRY_LIMIT || pos + 285 > (uint64_t)length ||
                tb_stream_seek(file, (int64_t)pos, SEEK_SET) ||
                tb_stream_read(h, 1, 285, file) != 285)
                return bad(EINVAL);
            uint64_t packed = u32(h + 12), size = u32(h + 8);
            unsigned method = u16(h + 4);
            if (u16(h) != 0xf012 || method > 1 || pos + 285 + packed > (uint64_t)length ||
                !memchr(h + 20, 0, 256))
                return bad(EINVAL);
            char *name = member_name((char *)h + 20, 0);
            if (name && *name)
            {
                int rc = visit(context, name, size, 0, file, pos + 285, packed, method ? 5 : 4,
                               INT64_MIN);
                free(name);
                if (rc != 1)
                    return rc;
            }
            else
                free(name);
            pos += 285 + packed;
        }
    }
    return 1;
}

typedef struct ArchiveInput
{
    TbReadStream *file;
    uint64_t base, length, position;
    unsigned char buffer[65536];
} ArchiveInput;

static la_ssize_t archive_input_read(struct archive *a, void *opaque, const void **out)
{
    ArchiveInput *in = opaque;
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
    ArchiveInput *in = opaque;
    uint64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? in->position : in->length;
    uint64_t distance = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : (uint64_t)offset;
    if (offset < 0 ? distance > base : distance > in->length - base)
        return -1;
    in->position = offset < 0 ? base - distance : base + distance;
    return (la_int64_t)in->position;
}

static la_int64_t archive_input_skip(struct archive *a, void *opaque, la_int64_t count)
{
    ArchiveInput *in = opaque;
    uint64_t n = count > 0 ? (uint64_t)count : 0;
    if (n > in->length - in->position)
        n = in->length - in->position;
    return archive_input_seek(a, opaque, (la_int64_t)n, SEEK_CUR) < 0 ? 0 : (la_int64_t)n;
}

#include "ball/legacy_archives.h"
#include "ball/zoo_archive.h"

typedef struct DiskVisit
{
    Visit visit;
    void *context;
    TbReadStream *file;
} DiskVisit;

static int disk_visit(void *opaque, const char *name, uint64_t size, int directory,
                      int64_t modified)
{
    DiskVisit *d = opaque;
    return d->visit(d->context, name, size, directory, d->file, 0, 0, 3, modified);
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
    media_session.stats.directory_index_builds += builds - before_builds;
    media_session.stats.directory_index_hits += hits - before_hits;
}

static int archive_walk(TbReadStream *file, Visit visit, void *context)
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
    if (u32(magic + 20) == ZOO_SIGNATURE)
        return zoo_walk(file, visit, context);
    if (magic[0] == 0x1a)
        return legacy_arc_walk(file, visit, context);
    if (magic[0] == 0x60 && magic[1] == 0xea)
        return legacy_arj_walk(file, visit, context);
    if (magic[0] == 0xa5 && magic[1] == 0x96)
        return pack_walk(file, visit, context);
    if (!memcmp(magic, "\x77\x04\x02\xbe", 4) || !memcmp(magic, "MZ", 2))
    {
        int rc = wpi_walk(file, visit, context);
        if (rc || errno != ENOTSUP)
            return rc;
        if (!memcmp(magic, "MZ", 2))
        {
            rc = legacy_arj_sfx(file, visit, context);
            if (rc || errno != ENOTSUP)
                return rc;
        }
        tb_stream_seek(file, 0, SEEK_SET);
    }
    struct archive *a = archive_read_new();
    if (!a)
        return 0;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);
    tb_stream_seek(file, 0, SEEK_END);
    int64_t length = tb_stream_tell(file);
    tb_stream_seek(file, 0, SEEK_SET);
    ArchiveInput in = {.file = file, .length = length < 0 ? 0 : (uint64_t)length};
    archive_read_set_seek_callback(a, archive_input_seek);
    int ok = archive_read_open2(a, &in, NULL, archive_input_read, archive_input_skip, NULL) ==
             ARCHIVE_OK;
    struct archive_entry *entry;
    unsigned count = 0;
    while (ok)
    {
        int rc = archive_read_next_header(a, &entry);
        if (rc == ARCHIVE_EOF)
            break;
        if (rc != ARCHIVE_OK || ++count > MEDIA_ENTRY_LIMIT ||
            archive_entry_is_encrypted(entry) == 1)
        {
            ok = bad(EINVAL);
            break;
        }
        if (archive_format(a) == ARCHIVE_FORMAT_RAW &&
            archive_filter_code(a, 0) == ARCHIVE_FILTER_NONE)
        {
            ok = bad(ENOTSUP);
            break;
        }
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry))
            continue;
        char *name = member_name(archive_entry_pathname(entry), 0);
        if (!name || !*name)
        {
            free(name);
            continue;
        }
        int directory = archive_entry_filetype(entry) == AE_IFDIR;
        /* A requested ordinary member streams directly into a private snapshot. */
        int result = visit(
            context, name, (uint64_t)archive_entry_size(entry), directory, (void *)a, 0, 0, 0,
            archive_entry_mtime_is_set(entry) ? (int64_t)archive_entry_mtime(entry) : INT64_MIN);
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

#include "ball/media_stream.h"

/* Resolve container boundaries iteratively: nesting consumes no C call stack. */
static TbReadStream *open_parent(const char *path, char **member)
{
    if (!tb_media_path(path))
        return NULL;
    const char *p = path + strlen(MEDIA_PREFIX), *end = strchr(p, '!');
    if (!end)
        return NULL;
    char *host = decode(p, (size_t)(end - p));
    if (!host)
        return NULL;
    select_media_cache(host);
    TbReadStream *file = cached_parent(path, (size_t)(end - path));
    if (!file)
    {
        file = tb_stream_file(tb_fopen(host, "rb"), 1);
        if (file && media_session.current)
            tb_stream_set_budget(file, &media_session.current->budget);
        cache_parent(path, (size_t)(end - path), file);
    }
    free(host);
    if (!file)
        return NULL;
    p = end + 1;
    while ((end = strchr(p, '!')))
    {
        char *name = decode(p, (size_t)(end - p));
        if (!name)
        {
            tb_stream_close(file);
            return NULL;
        }
        TbReadStream *inner = cached_parent(path, (size_t)(end - path));
        if (!inner)
        {
            inner = extract_member(file, name);
            cache_parent(path, (size_t)(end - path), inner);
        }
        free(name);
        tb_stream_close(file);
        if (!inner)
            return NULL;
        file = inner;
        p = end + 1;
    }
    *member = decode(p, strlen(p));
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
        media_session.stats.materialized_files++;
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

static int list_visit(void *opaque, const char *name, uint64_t size, int directory, void *unused,
                      uint64_t off, uint64_t packed, int kind, int64_t modified)
{
    Listing *l = opaque;
    size_t prefix = strlen(l->member);
    if (prefix && !strcmp(name, l->member))
    {
        if (!directory)
            return bad(ENOTDIR);
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
        ok = bad(ENOENT);
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

static int probe_visit(void *opaque, const char *name, uint64_t size, int dir, void *f,
                       uint64_t off, uint64_t packed, int kind, int64_t modified)
{
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
            file = cached_parent(path, strlen(path));
            if (!file)
                file = extract_member(parent, member);
            tb_stream_close(parent);
        }
        free(member);
    }
    else
        file = tb_stream_file(tb_fopen(path, "rb"), 1);
    if (!file)
        return 0;
    int ok = archive_walk(file, probe_visit, NULL);
    if ((ok == 2 || ok == 1) && tb_media_path(path))
        cache_parent(path, strlen(path), file);
    tb_stream_close(file);
    return ok == 2 || ok == 1;
}
