/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#include "media.h"
#include "platform.h"
#include "disk_image.h"
#include "pack.h"
#include "vendor/libarchive/archive.h"
#include "vendor/libarchive/archive_entry.h"
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
typedef int (*Visit)(void *, const char *, uint64_t, int, FILE *, uint64_t, uint64_t, int);
/* The callback may request one member; extraction never uses archive paths as host paths. */
static int pack_walk(FILE *file, Visit visit, void *context)
{
    if (fseeko(file, 0, SEEK_END))
        return 0;
    int64_t length = ftello(file);
    uint64_t at = 0;
    for (unsigned entries = 0; entries < MEDIA_ENTRY_LIMIT; entries++)
    {
        unsigned char h[41];
        if (at > (uint64_t)length || fseeko(file, (int64_t)at, SEEK_SET) ||
            fread(h, 1, 24, file) != 24 || h[0] != 0xa5 || h[1] != 0x96)
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
        if (fread(h + 24, 1, header - 24, file) != header - 24)
            return bad(EIO);
        if (kind == 0xfffd && memcmp(h + 24, "FTCOMP", 6))
            return bad(EINVAL);
        unsigned n = u16(h + header - 2);
        if (!n || n > 4096 || at + header + n > end)
            return bad(EINVAL);
        char *raw = calloc(1, n + 1);
        if (!raw)
            return 0;
        if (fread(raw, 1, n, file) != n)
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
                               kind == 0xfffd ? 2 : 1);
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
static int wpi_walk(FILE *file, Visit visit, void *context)
{
    if (fseeko(file, 0, SEEK_END))
        return 0;
    int64_t length = ftello(file);
    if (length < 532)
        return bad(EINVAL);
    unsigned char head[572];
    int64_t at = 0;
    int64_t tail = length >= 572 ? ((length - 572) / 512) * 512 : 0;
    if (fseeko(file, tail, SEEK_SET) || fread(head, 1, 532, file) != 532)
        return bad(EIO);
    if (memcmp(head, "\x77\x04\x02\xbe", 4))
    {
        size_t n = (size_t)(length < 200532 ? length : 200532);
        unsigned char *front = malloc(n);
        if (!front)
            return 0;
        rewind(file);
        if (fread(front, 1, n, file) != n)
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
        if (fseeko(file, at + 532, SEEK_SET) || fread(head + 532, 1, 40, file) != 40)
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
        int rc = visit(context, "install.wis", orig, 0, file, data, script, 5);
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
        if (fseeko(file, (int64_t)(data + (uint64_t)p * 48), SEEK_SET) ||
            fread(package, 1, 48, file) != 48)
            return bad(EIO);
        unsigned files = u16(package + 2);
        uint64_t pos = u32(package + 4);
        for (unsigned f = 0; f < files; f++)
        {
            unsigned char h[285];
            if (++count > MEDIA_ENTRY_LIMIT || pos + 285 > (uint64_t)length ||
                fseeko(file, (int64_t)pos, SEEK_SET) || fread(h, 1, 285, file) != 285)
                return bad(EINVAL);
            uint64_t packed = u32(h + 12), size = u32(h + 8);
            unsigned method = u16(h + 4);
            if (u16(h) != 0xf012 || method > 1 || pos + 285 + packed > (uint64_t)length ||
                !memchr(h + 20, 0, 256))
                return bad(EINVAL);
            char *name = member_name((char *)h + 20, 0);
            if (name && *name)
            {
                int rc = visit(context, name, size, 0, file, pos + 285, packed, method ? 5 : 4);
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
    FILE *file;
    uint64_t base, length, position;
    unsigned char buffer[65536];
} ArchiveInput;
static la_ssize_t archive_input_read(struct archive *a, void *opaque, const void **out)
{
    ArchiveInput *in = opaque;
    *out = in->buffer;
    size_t want =
        (in->length - in->position > sizeof in->buffer ? sizeof in->buffer
                                                       : (size_t)(in->length - in->position));
    size_t n = fread(in->buffer, 1, want, in->file);
    in->position += n;
    return n ? (la_ssize_t)n : ferror(in->file) ? -1 : 0;
}
static la_int64_t archive_input_seek(struct archive *a, void *opaque, la_int64_t offset, int whence)
{
    ArchiveInput *in = opaque;
    int64_t pos = whence == SEEK_SET   ? offset
                  : whence == SEEK_CUR ? (int64_t)in->position + offset
                                       : (int64_t)in->length + offset;
    if (pos < 0 || (uint64_t)pos > in->length ||
        fseeko(in->file, (int64_t)in->base + pos, SEEK_SET))
        return -1;
    in->position = (uint64_t)pos;
    return pos;
}
static la_int64_t archive_input_skip(struct archive *a, void *opaque, la_int64_t count)
{
    ArchiveInput *in = opaque;
    uint64_t n = count > 0 ? (uint64_t)count : 0;
    if (n > in->length - in->position)
        n = in->length - in->position;
    return archive_input_seek(a, opaque, (int64_t)n, SEEK_CUR) < 0 ? 0 : (la_int64_t)n;
}
typedef struct DiskVisit
{
    Visit visit;
    void *context;
    FILE *file;
} DiskVisit;
static int disk_visit(void *opaque, const char *name, uint64_t size, int directory)
{
    DiskVisit *d = opaque;
    return d->visit(d->context, name, size, directory, d->file, 0, 0, 3);
}
static int archive_walk(FILE *file, Visit visit, void *context)
{
    unsigned char magic[512] = {0};
    rewind(file);
    fread(magic, 1, sizeof magic, file);
    rewind(file);
    unsigned char iso[7] = {0};
    if (!fseeko(file, 16 * 2048, SEEK_SET))
        fread(iso, 1, sizeof iso, file);
    rewind(file);
    if (!memcmp(iso + 1, "CD001", 5) ||
        (magic[510] == 0x55 && magic[511] == 0xaa && (magic[0] == 0xeb || magic[0] == 0xe9)))
    {
        DiskVisit d = {visit, context, file};
        return tb_image_stream_walk(file, disk_visit, &d);
    }
    if (magic[0] == 0xa5 && magic[1] == 0x96)
        return pack_walk(file, visit, context);
    if (!memcmp(magic, "\x77\x04\x02\xbe", 4) || !memcmp(magic, "MZ", 2))
    {
        int rc = wpi_walk(file, visit, context);
        if (rc || errno != ENOTSUP)
            return rc;
        rewind(file);
    }
    struct archive *a = archive_read_new();
    if (!a)
        return 0;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a);
    fseeko(file, 0, SEEK_END);
    int64_t length = ftello(file);
    rewind(file);
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
        int result = visit(context, name, (uint64_t)archive_entry_size(entry), directory, (FILE *)a,
                           0, 0, 0);
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
typedef struct Extract
{
    const char *name;
    FILE *out;
    int found;
} Extract;
static int extract_visit(void *opaque, const char *name, uint64_t size, int directory, FILE *input,
                         uint64_t offset, uint64_t packed, int kind)
{
    Extract *e = opaque;
    if (strcmp(e->name, name))
        return 1;
    if (directory || size > MEDIA_MEMBER_LIMIT)
        return bad(directory ? EISDIR : EFBIG);
    if (e->found++)
        return bad(EINVAL);
    e->out = tmpfile();
    if (!e->out)
        return 0;
    if (kind == 3)
    {
        FILE *out = tb_image_stream_fopen(input, name);
        if (!out)
            return 0;
        fclose(e->out);
        e->out = out;
    }
    else if (kind == 4 || kind == 5)
    {
        if (fseeko(input, (int64_t)offset, SEEK_SET))
            return bad(EIO);
        unsigned char data[65536];
        uint64_t total = 0;
        if (kind == 4)
        {
            if (packed != size)
                return bad(EINVAL);
            while (total < packed)
            {
                size_t n = (size_t)(packed - total > sizeof data ? sizeof data : packed - total);
                if (fread(data, 1, n, input) != n || fwrite(data, 1, n, e->out) != n)
                    return bad(EIO);
                total += n;
            }
        }
        else
        {
            struct archive *a = archive_read_new();
            if (!a)
                return 0;
            archive_read_support_filter_bzip2(a);
            archive_read_support_format_raw(a);
            ArchiveInput in = {.file = input, .base = offset, .length = packed};
            int ok = archive_read_open2(a, &in, NULL, archive_input_read, archive_input_skip,
                                        NULL) == ARCHIVE_OK;
            struct archive_entry *entry;
            if (ok)
                ok = archive_read_next_header(a, &entry) == ARCHIVE_OK;
            la_ssize_t n = 0;
            while (ok && (n = archive_read_data(a, data, sizeof data)) > 0)
            {
                total += (uint64_t)n;
                if (total > size || fwrite(data, 1, (size_t)n, e->out) != (size_t)n)
                    ok = 0;
            }
            if (n < 0 || total != size)
                ok = 0;
            archive_read_free(a);
            if (!ok)
                return bad(EINVAL);
        }
    }
    else if (kind)
    {
        if (packed > 16u * 1024u * 1024u || size > 16u * 1024u * 1024u)
            return bad(EFBIG);
        unsigned char *data = malloc((size_t)packed), *output = NULL;
        if (!data)
            return 0;
        int ok = !fseeko(input, (int64_t)offset, SEEK_SET) &&
                 fread(data, 1, (size_t)packed, input) == packed &&
                 tb_pack_decode(data, (size_t)packed, (size_t)size, kind, &output);
        free(data);
        if (ok)
            ok = fwrite(output, 1, (size_t)size, e->out) == size;
        free(output);
        if (!ok)
            return bad(EINVAL);
    }
    else
    {
        unsigned char data[65536];
        uint64_t total = 0;
        la_ssize_t n;
        while ((n = archive_read_data((struct archive *)input, data, sizeof data)) > 0)
        {
            total += (uint64_t)n;
            if (total > MEDIA_MEMBER_LIMIT || fwrite(data, 1, (size_t)n, e->out) != (size_t)n)
                return bad(EFBIG);
        }
        if (n < 0)
            return bad(EIO);
    }
    if (fflush(e->out) || fseeko(e->out, 0, SEEK_SET))
        return bad(EIO);
    return 2;
}
static FILE *extract_member(FILE *file, const char *member)
{
    Extract e = {.name = member};
    int rc = archive_walk(file, extract_visit, &e);
    if (rc != 2)
    {
        if (e.out)
            fclose(e.out);
        if (rc == 1)
            bad(ENOENT);
        return NULL;
    }
    return e.out;
}
/* Resolve container boundaries iteratively: nesting consumes no C call stack. */
static FILE *open_parent(const char *path, char **member)
{
    if (!tb_media_path(path))
        return NULL;
    const char *p = path + strlen(MEDIA_PREFIX), *end = strchr(p, '!');
    if (!end)
        return NULL;
    char *host = decode(p, (size_t)(end - p));
    if (!host)
        return NULL;
    FILE *file = tb_fopen(host, "rb");
    free(host);
    if (!file)
        return NULL;
    p = end + 1;
    while ((end = strchr(p, '!')))
    {
        char *name = decode(p, (size_t)(end - p));
        if (!name)
        {
            fclose(file);
            return NULL;
        }
        FILE *inner = extract_member(file, name);
        free(name);
        fclose(file);
        if (!inner)
            return NULL;
        file = inner;
        p = end + 1;
    }
    *member = decode(p, strlen(p));
    if (!*member)
    {
        fclose(file);
        return NULL;
    }
    return file;
}
FILE *tb_media_fopen(const char *path)
{
    char *member = NULL;
    FILE *file = open_parent(path, &member);
    if (!file)
        return NULL;
    FILE *out = extract_member(file, member);
    free(member);
    fclose(file);
    return out;
}
typedef struct Listing
{
    char *member;
    TbMediaEntry *items;
    size_t count;
    int found;
} Listing;
static int list_visit(void *opaque, const char *name, uint64_t size, int directory, FILE *unused,
                      uint64_t off, uint64_t packed, int kind)
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
    l->items[l->count++] =
        (TbMediaEntry){.name = copy, .size = size, .directory = slash || directory};
    return 1;
}
TbMediaEntry *tb_media_entries(const char *path, size_t *count)
{
    *count = (size_t)-1;
    Listing l = {0};
    FILE *file = open_parent(path, &l.member);
    if (!file)
        return NULL;
    int ok = archive_walk(file, list_visit, &l);
    if (ok && *l.member && !l.found)
        ok = bad(ENOENT);
    free(l.member);
    fclose(file);
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
static int probe_visit(void *opaque, const char *name, uint64_t size, int dir, FILE *f,
                       uint64_t off, uint64_t packed, int kind)
{
    return 2;
}
int tb_media_container(const char *path)
{
    FILE *file = tb_media_path(path) ? tb_media_fopen(path) : tb_fopen(path, "rb");
    if (!file)
        return 0;
    int ok = archive_walk(file, probe_visit, NULL);
    fclose(file);
    return ok == 2 || ok == 1;
}
