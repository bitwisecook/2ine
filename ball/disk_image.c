/* Public domain. No rights reserved. Wholly AI-authored. */
/* ISO 9660/Joliet and FAT12/FAT16 readers. All image access is read-only. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "ball/disk_image.h"
#include "ball/image_internal.h"
#include "ball/file_time.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#define strdup _strdup
#define fseeko _fseeki64
#define ftello _ftelli64
#endif
#define IMAGE_PREFIX "/@2ine-image/"
#define DEVICE_PREFIX "/@2ine-device/"
#define SECTOR 2048u
#define DIRECTORY_LIMIT (64u * 1024u * 1024u)

typedef struct Node
{
    uint64_t offset, size;
    int64_t modified, accessed, created;
    uint32_t cluster;
    int directory;
    uint64_t record;
    unsigned attributes;
} Node;

typedef struct Disk
{
    struct ImageIndex *index;
    FILE *file;
    TbReadStream *stream;
    unsigned char *memory;
    int readonly;
    uint64_t bytes;
    unsigned fat_bits, sector_bytes, cluster_bytes;
    unsigned reserved_sectors, fat_count, root_entries, fat_sectors, sectors_per_track, heads,
        media;
    uint32_t hidden_sectors;
    uint32_t clusters;
    uint64_t data_offset;
    unsigned char *fat;
    size_t fat_size;
    int joliet;
    uint32_t serial;
    char label[12];
    Node root;
} Disk;

typedef int (*EntryCallback)(void *, const char *, const Node *);

static unsigned le16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t le32(const unsigned char *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static int failure(int error)
{
    errno = error;
    return 0;
}

int tb_image_path(const char *path)
{
    return path && (!strncmp(path, IMAGE_PREFIX, strlen(IMAGE_PREFIX)) ||
                    !strncmp(path, DEVICE_PREFIX, strlen(DEVICE_PREFIX)));
}

int tb_image_device(const char *path)
{
    return path && !strncmp(path, DEVICE_PREFIX, strlen(DEVICE_PREFIX));
}

static size_t prefix_size(const char *path)
{
    return strlen(tb_image_device(path) ? DEVICE_PREFIX : IMAGE_PREFIX);
}

static char *encoded_root(const char *source, const char *kind)
{
    static const char hex[] = "0123456789abcdef";
    size_t n = strlen(source), prefix = strlen(kind);
    if (n > (SIZE_MAX - prefix - 1) / 2)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    char *result = malloc(prefix + n * 2 + 1);
    if (!result)
        return NULL;
    memcpy(result, kind, prefix);
    for (size_t i = 0; i < n; i++)
    {
        result[prefix + i * 2] = hex[(unsigned char)source[i] >> 4];
        result[prefix + i * 2 + 1] = hex[(unsigned char)source[i] & 15];
    }
    result[prefix + n * 2] = 0;
    return result;
}

char *tb_image_root(const char *source)
{
    return encoded_root(source, IMAGE_PREFIX);
}

char *tb_image_device_root(const char *control)
{
    return encoded_root(control, DEVICE_PREFIX);
}

static int digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

FILE *tb_image_open_source(const char *path, int write)
{
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!n)
    {
        errno = EINVAL;
        return NULL;
    }
    wchar_t *wide = malloc((size_t)n * sizeof *wide);
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n);
    FILE *result = _wfopen(wide, write ? L"rb+" : L"rb");
    free(wide);
    return result;
#else
    return fopen(path, write ? "rb+" : "rb");
#endif
}

static FILE *open_source(const char *path)
{
    return tb_image_open_source(path, 0);
}

int tb_image_lock_source(FILE *file, int write)
{
#ifdef _WIN32
    OVERLAPPED overlapped = {0};
    if (!LockFileEx((HANDLE)_get_osfhandle(_fileno(file)), write ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0,
                    MAXDWORD, MAXDWORD, &overlapped))
        return failure(EACCES);
    return 1;
#else
    int result;
    do
        result = flock(fileno(file), write ? LOCK_EX : LOCK_SH);
    while (result && errno == EINTR);
    return result == 0;
#endif
}

static int read_at(Disk *d, uint64_t offset, void *data, size_t n)
{
    if (offset > d->bytes || n > d->bytes - offset || offset > INT64_MAX)
        return failure(EIO);
    if (d->stream)
        return tb_stream_read_at(d->stream, offset, data, n);
    if (d->memory)
    {
        memcpy(data, d->memory + offset, n);
        return 1;
    }
    if (fseeko(d->file, (int64_t)offset, SEEK_SET) || fread(data, 1, n, d->file) != n)
        return failure(EIO);
    return 1;
}

static void close_disk(Disk *d)
{
    int error = errno;
    if (d->file)
        fclose(d->file);
    free(d->fat);
    free(d->memory);
    errno = error;
}

static int iso_node(Disk *d, const unsigned char *r, size_t length, Node *node)
{
    if (length < 34 || r[0] < 34 || r[0] > length || r[32] > r[0] - 33)
        return failure(EINVAL);
    if (r[26] || r[27] || (r[25] & 0x80))
        return failure(ENOTSUP);
    node->offset = ((uint64_t)le32(r + 2) + r[1]) * SECTOR;
    node->size = le32(r + 10);
    node->directory = !!(r[25] & 2);
    node->cluster = 0;
    node->modified = tb_iso_time(r + 18);
    if (node->offset > d->bytes || node->size > d->bytes - node->offset)
        return failure(EIO);
    return 1;
}

static int identify(Disk *d)
{
    unsigned char b[SECTOR];
    if (d->bytes >= 17 * SECTOR && read_at(d, 16 * SECTOR, b, sizeof b) &&
        !memcmp(b + 1, "CD001", 5))
    {
        int primary = 0;
        for (unsigned i = 16; i < 144 && (uint64_t)(i + 1) * SECTOR <= d->bytes; i++)
        {
            if (!read_at(d, (uint64_t)i * SECTOR, b, sizeof b))
                return 0;
            if (memcmp(b + 1, "CD001", 5) || b[6] != 1)
                return failure(EINVAL);
            if (b[0] == 255)
                break;
            int joliet = b[0] == 2 && b[88] == '%' && b[89] == '/' &&
                         (b[90] == '@' || b[90] == 'C' || b[90] == 'E');
            if (b[0] != 1 && !joliet)
                continue;
            uint64_t volume_bytes = (uint64_t)le32(b + 80) * SECTOR;
            if (le16(b + 128) != SECTOR || !volume_bytes || volume_bytes > d->bytes)
                return failure(EINVAL);
            d->bytes = volume_bytes;
            if ((b[0] == 1 && !d->joliet) || joliet)
            {
                if (!iso_node(d, b + 156, SECTOR - 156, &d->root) || !d->root.directory)
                    return failure(EINVAL);
                d->joliet = joliet;
            }
            if (b[0] == 1)
            {
                primary = 1;
                memcpy(d->label, b + 40, 11);
                d->label[11] = 0;
                for (int j = 10; j >= 0 && d->label[j] == ' '; j--)
                    d->label[j] = 0;
            }
        }
        return primary ? 1 : failure(EINVAL);
    }
    if (!read_at(d, 0, b, 512))
        return 0;
    if (b[510] != 0x55 || b[511] != 0xaa)
        return failure(EINVAL);
    unsigned sector = le16(b + 11), spc = b[13], reserved = le16(b + 14), fats = b[16],
             entries = le16(b + 17), fat_sectors = le16(b + 22);
    uint32_t total = le16(b + 19);
    if (!total)
        total = le32(b + 32);
    if ((sector != 512 && sector != 1024 && sector != 2048 && sector != 4096) || !spc ||
        (spc & (spc - 1)) || spc > 128 || !reserved || !fats || fats > 2 || !entries ||
        !fat_sectors)
        return failure(ENOTSUP);
    uint64_t root_sectors = ((uint64_t)entries * 32 + sector - 1) / sector;
    uint64_t data_sector = reserved + (uint64_t)fats * fat_sectors + root_sectors;
    if (data_sector >= total || (uint64_t)total * sector > d->bytes)
        return failure(EINVAL);
    uint64_t clusters = (total - data_sector) / spc;
    if (!clusters || clusters >= 65525)
        return failure(ENOTSUP);
    d->reserved_sectors = reserved;
    d->fat_count = fats;
    d->root_entries = entries;
    d->fat_sectors = fat_sectors;
    d->sectors_per_track = le16(b + 24);
    d->heads = le16(b + 26);
    d->hidden_sectors = le32(b + 28);
    d->media = b[21];
    d->bytes = (uint64_t)total * sector;
    d->fat_bits = clusters < 4085 ? 12 : 16;
    d->clusters = (uint32_t)clusters;
    d->sector_bytes = sector;
    d->cluster_bytes = sector * spc;
    d->data_offset = data_sector * sector;
    d->fat_size = (size_t)fat_sectors * sector;
    if (((clusters + 2) * d->fat_bits + 7) / 8 > d->fat_size)
        return failure(EINVAL);
    d->fat = malloc(d->fat_size);
    if (!d->fat)
        return 0;
    if (!read_at(d, (uint64_t)reserved * sector, d->fat, d->fat_size))
        return 0;
    d->root = (Node){.offset = ((uint64_t)reserved + (uint64_t)fats * fat_sectors) * sector,
                     .size = (uint64_t)entries * 32,
                     .modified = INT64_MIN,
                     .directory = 1};
    if (b[38] == 0x29)
    {
        d->serial = le32(b + 39);
        memcpy(d->label, b + 43, 11);
    }
    unsigned char record[32];
    for (unsigned i = 0; i < entries; i++)
    {
        if (!read_at(d, d->root.offset + i * 32u, record, sizeof record))
            return 0;
        if (!record[0])
            break;
        if (record[0] != 0xe5 && record[11] == 8)
        {
            memcpy(d->label, record, 11);
            break;
        }
    }
    d->label[11] = 0;
    for (int i = 10; i >= 0 && d->label[i] == ' '; i--)
        d->label[i] = 0;

    return 1;
}

const char *tb_image_member(const char *path)
{
    if (!tb_image_path(path))
        return NULL;
    const char *end = strchr(path + prefix_size(path), '/');
    return end ? end + 1 : path + strlen(path);
}

static char *decoded_source(const char *path)
{
    if (!tb_image_path(path))
    {
        errno = EINVAL;
        return NULL;
    }
    const char *hex = path + prefix_size(path), *end = strchr(hex, '/');
    size_t n = end ? (size_t)(end - hex) : strlen(hex);
    if (!n || n % 2 || n > 65536)
    {
        errno = EINVAL;
        return NULL;
    }
    char *source = malloc(n / 2 + 1);
    if (!source)
        return NULL;
    for (size_t i = 0; i < n; i += 2)
    {
        int a = digit(hex[i]), b = digit(hex[i + 1]);
        if (a < 0 || b < 0 || !(a | b))
        {
            free(source);
            errno = EINVAL;
            return NULL;
        }
        source[i / 2] = (char)(a * 16 + b);
    }
    source[n / 2] = 0;
    return source;
}

/* A stable device resolves its current medium for every new operation. */
static char *device_source(const char *path, int *kind, int *readonly)
{
    char *control = decoded_source(path);
    if (!control)
        return NULL;
    FILE *file = open_source(control);
    free(control);
    if (!file)
        return NULL;
    unsigned char header[16];
    if (fread(header, 1, sizeof header, file) != sizeof header || memcmp(header, "2INEMED1", 8) ||
        (header[8] < 1 || header[8] > 3))
    {
        fclose(file);
        errno = EINVAL;
        return NULL;
    }
    *kind = header[8];
    if (readonly)
        *readonly = header[8] == 1 || !!header[9];
    uint32_t n = le32(header + 12);
    if (n > 32768)
    {
        fclose(file);
        errno = EINVAL;
        return NULL;
    }
    if (!n)
    {
        fclose(file);
        errno = ENXIO;
        return NULL;
    }
    char *source = malloc((size_t)n + 1);
    if (!source)
    {
        fclose(file);
        return NULL;
    }
    if (fread(source, 1, n, file) != n || memchr(source, 0, n))
    {
        free(source);
        fclose(file);
        errno = EINVAL;
        return NULL;
    }
    source[n] = 0;
    fclose(file);
    return source;
}

char *tb_image_source(const char *path)
{
    int kind = 0;
    return tb_image_device(path) ? device_source(path, &kind, NULL) : decoded_source(path);
}

int tb_image_device_kind(const char *path)
{
    int kind = 0;
    if (tb_image_device(path))
    {
        char *source = device_source(path, &kind, NULL);
        free(source);
    }
    return kind;
}

char *tb_image_volume_root(const char *path)
{
    char *decoded = decoded_source(path);
    if (!decoded)
        return NULL;
    free(decoded);
    const char *slash = strchr(path + prefix_size(path), '/');
    size_t n = slash ? (size_t)(slash - path) : strlen(path);
    char *root = malloc(n + 1);
    if (root)
    {
        memcpy(root, path, n);
        root[n] = 0;
    }
    return root;
}

int tb_image_protected(const char *path)
{
    if (!tb_image_device(path))
        return 0;
    int kind = 0, readonly = 0;
    char *source = device_source(path, &kind, &readonly);
    if (!source)
        return -1;
    free(source);
    return readonly;
}

static int open_disk_mode(const char *path, Disk *d, const char **member, int write)
{
    memset(d, 0, sizeof *d);
    int kind = 0, readonly = 0;
    char *source =
        tb_image_device(path) ? device_source(path, &kind, &readonly) : decoded_source(path);
    if (!source)
        return 0;
    if (write && readonly)
    {
        free(source);
        return failure(EROFS);
    }
    d->file = tb_image_open_source(source, write);
    free(source);
    if (!d->file)
        return 0;
    if (!tb_image_lock_source(d->file, write) || fseeko(d->file, 0, SEEK_END))
    {
        close_disk(d);
        return 0;
    }
    int64_t size = ftello(d->file);
    if (size < 0)
    {
        close_disk(d);
        return 0;
    }
    d->bytes = (uint64_t)size;
    if (!identify(d))
    {
        close_disk(d);
        return 0;
    }
    d->readonly = readonly || !d->fat_bits;
    if (write && d->readonly)
    {
        close_disk(d);
        return failure(EROFS);
    }
    *member = tb_image_member(path);
    return 1;
}

static int open_disk(const char *path, Disk *d, const char **member)
{
    return open_disk_mode(path, d, member, 0);
}

static uint32_t next_cluster(Disk *d, uint32_t cluster)
{
    if (cluster < 2 || cluster >= d->clusters + 2 ||
        cluster >= (d->fat_bits == 12 ? 0xff0u : 0xfff0u))
    {
        errno = EIO;
        return UINT32_MAX;
    }
    size_t at = d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
    uint32_t next = le16(d->fat + at);
    if (d->fat_bits == 12)
        next = cluster & 1 ? next >> 4 : next & 0xfff;
    if (next >= (d->fat_bits == 12 ? 0xff8u : 0xfff8u))
        return 0;
    if (next < 2 || next >= d->clusters + 2 || next >= (d->fat_bits == 12 ? 0xff0u : 0xfff0u))
    {
        errno = EIO;
        return UINT32_MAX;
    }
    return next;
}

static uint64_t directory_offset(Disk *d, Node directory, size_t at)
{
    if (!directory.cluster)
        return directory.offset + at;
    uint32_t cluster = directory.cluster;
    for (size_t i = 0; i < at / d->cluster_bytes; i++)
    {
        cluster = next_cluster(d, cluster);
        if (!cluster || cluster == UINT32_MAX || i >= d->clusters)
            return UINT64_MAX;
    }
    return d->data_offset + (uint64_t)(cluster - 2) * d->cluster_bytes + at % d->cluster_bytes;
}

static unsigned char *directory_data(Disk *d, Node node, size_t *size)
{
    uint64_t length = node.size;
    if (d->fat_bits && node.cluster)
    {
        length = 0;
        uint32_t cluster = node.cluster;
        for (uint32_t steps = 0; cluster; steps++)
        {
            if (steps >= d->clusters || length + d->cluster_bytes > DIRECTORY_LIMIT)
            {
                errno = EIO;
                return NULL;
            }
            length += d->cluster_bytes;
            cluster = next_cluster(d, cluster);
            if (cluster == UINT32_MAX)
                return NULL;
        }
    }
    if (length > DIRECTORY_LIMIT)
    {
        errno = EFBIG;
        return NULL;
    }
    unsigned char *data = malloc(length ? (size_t)length : 1);
    if (!data)
        return NULL;
    if (!node.cluster)
    {
        if (!read_at(d, node.offset, data, (size_t)length))
        {
            free(data);
            return NULL;
        }
    }
    else
    {
        uint32_t cluster = node.cluster;
        for (size_t at = 0; at < length; at += d->cluster_bytes)
        {
            if (!read_at(d, d->data_offset + (uint64_t)(cluster - 2) * d->cluster_bytes, data + at,
                         d->cluster_bytes))
            {
                free(data);
                return NULL;
            }
            cluster = next_cluster(d, cluster);
            if (cluster == UINT32_MAX)
            {
                free(data);
                return NULL;
            }
        }
    }
    *size = (size_t)length;
    return data;
}

static int utf16_name(const uint16_t *text, size_t n, char *out, size_t capacity)
{
    size_t at = 0;
    for (size_t i = 0; i < n && text[i] && text[i] != 0xffff; i++)
    {
        unsigned v = text[i];
        if (v >= 0xd800 && v <= 0xdfff)
            return failure(ENOTSUP);
        size_t bytes = v < 0x80 ? 1 : v < 0x800 ? 2 : 3;
        if (at + bytes >= capacity)
            return failure(ENAMETOOLONG);
        if (bytes == 1)
            out[at++] = (char)v;
        else if (bytes == 2)
        {
            out[at++] = (char)(0xc0 | v >> 6);
            out[at++] = (char)(0x80 | (v & 63));
        }
        else
        {
            out[at++] = (char)(0xe0 | v >> 12);
            out[at++] = (char)(0x80 | ((v >> 6) & 63));
            out[at++] = (char)(0x80 | (v & 63));
        }
    }
    out[at] = 0;
    return 1;
}

static int safe_name(const char *name)
{
    return *name && strcmp(name, ".") && strcmp(name, "..") && !strpbrk(name, "/\\:");
}

static int read_entries(Disk *d, Node directory, EntryCallback callback, void *context)
{
    if (!directory.directory)
        return failure(ENOTDIR);
    size_t size;
    unsigned char *data = directory_data(d, directory, &size);
    if (!data)
        return 0;
    int ok = 1;
    uint16_t lfn[260];
    unsigned expected = 0, checksum = 0, has_lfn = 0;
    memset(lfn, 0xff, sizeof lfn);
    for (size_t at = 0; at < size;)
    {
        char name[1024];
        Node node = {0};
        if (!d->fat_bits)
        {
            unsigned length = data[at];
            if (!length)
            {
                at += SECTOR - at % SECTOR;
                continue;
            }
            if (length > size - at || length > SECTOR - at % SECTOR ||
                !iso_node(d, data + at, length, &node))
            {
                ok = failure(EINVAL);
                break;
            }
            unsigned n = data[at + 32];
            const unsigned char *raw = data + at + 33;
            if (n == 1 && raw[0] < 2)
            {
                at += length;
                continue;
            }
            if (d->joliet)
            {
                if (n % 2)
                {
                    ok = failure(EINVAL);
                    break;
                }
                uint16_t units[128];
                for (unsigned i = 0; i < n / 2; i++)
                    units[i] = (uint16_t)(raw[i * 2] << 8 | raw[i * 2 + 1]);
                if (!utf16_name(units, n / 2, name, sizeof name))
                {
                    ok = 0;
                    break;
                }
            }
            else
            {
                memcpy(name, raw, n);
                name[n] = 0;
            }
            char *version = strrchr(name, ';');
            if (version)
                *version = 0;
            size_t len = strlen(name);
            if (len && name[len - 1] == '.')
                name[len - 1] = 0;
            at += length;
        }
        else
        {
            if (size - at < 32)
            {
                ok = failure(EINVAL);
                break;
            }
            const unsigned char *r = data + at;
            at += 32;
            if (!r[0])
                break;
            if (r[0] == 0xe5)
            {
                expected = 0;
                has_lfn = 0;
                continue;
            }
            if (r[11] == 0x0f)
            {
                unsigned ordinal = r[0] & 31;
                if ((r[0] & 0x40) && ordinal && ordinal <= 20)
                {
                    memset(lfn, 0xff, sizeof lfn);
                    expected = ordinal;
                    checksum = r[13];
                    has_lfn = 1;
                }
                if (!expected || ordinal != expected || r[13] != checksum || le16(r + 26))
                {
                    expected = 0;
                    has_lfn = 0;
                    continue;
                }
                static const unsigned offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                for (unsigned i = 0; i < 13; i++)
                    lfn[(ordinal - 1) * 13 + i] = (uint16_t)le16(r + offsets[i]);
                expected--;
                continue;
            }
            if (r[11] & 8)
            {
                expected = 0;
                has_lfn = 0;
                continue;
            }
            unsigned sum = 0;
            for (unsigned i = 0; i < 11; i++)
                sum = (((sum & 1) << 7) | (sum >> 1)) + r[i], sum &= 255;
            int long_name = has_lfn && !expected && sum == checksum && lfn[0] != 0xffff;
            if (long_name && !utf16_name(lfn, 260, name, sizeof name))
            {
                ok = 0;
                break;
            }
            if (!long_name)
            {
                size_t n = 0;
                for (unsigned i = 0; i < 8 && r[i] != ' '; i++)
                    name[n++] = (char)r[i];
                if (r[8] != ' ')
                {
                    name[n++] = '.';
                    for (unsigned i = 8; i < 11 && r[i] != ' '; i++)
                        name[n++] = (char)r[i];
                }
                name[n] = 0;
            }
            expected = 0;
            has_lfn = 0;
            memset(lfn, 0xff, sizeof lfn);
            node.record = directory_offset(d, directory, at - 32);
            node.attributes = r[11];
            node.modified = tb_dos_time(le16(r + 24), le16(r + 22));
            node.accessed = tb_dos_time(le16(r + 18), 0);
            node.created = tb_dos_time(le16(r + 16), le16(r + 14));
            node.accessed = node.accessed == INT64_MIN ? 0 : node.accessed;
            node.created = node.created == INT64_MIN ? 0 : node.created;
            node.cluster = le16(r + 26);
            node.size = le32(r + 28);
            node.directory = !!(r[11] & 16);
            if (node.directory && node.cluster < 2 && strcmp(name, ".") && strcmp(name, ".."))
            {
                ok = failure(EIO);
                break;
            }
        }
        if (!safe_name(name))
            continue;
        int result = callback(context, name, &node);
        if (result <= 0)
        {
            ok = result == 0;
            break;
        }
    }
    free(data);
    return ok;
}

#include "ball/image_index.h"

typedef struct Find
{
    const char *name;
    Node node;
    int found;
} Find;

static int same_name(const char *a, const char *b)
{
    while (*a && *b)
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++))
            return 0;
    return !*a && !*b;
}

static int find_entry(void *context, const char *name, const Node *node)
{
    Find *find = context;
    if (!same_name(find->name, name))
        return 1;
    find->node = *node;
    find->found = 1;
    return 0;
}

static int lookup(Disk *d, const char *member, Node *node)
{
    char *copy = strdup(member);
    if (!copy)
        return 0;
    /* Normalize before traversal; never let '..' escape the virtual root. */
    char *parts[256];
    size_t count = 0;
    char *cursor = copy;
    while (*cursor)
    {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char *part = cursor;
        while (*cursor && *cursor != '/')
            cursor++;
        if (*cursor)
            *cursor++ = 0;
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, ".."))
        {
            if (!count)
            {
                free(copy);
                return failure(EACCES);
            }
            count--;
            continue;
        }
        if (count == 256 || strpbrk(part, "\\:"))
        {
            free(copy);
            return failure(EINVAL);
        }
        parts[count++] = part;
    }
    *node = d->root;
    for (size_t i = 0; i < count; i++)
    {
        Find find = {parts[i], {0}, 0};
        if (!entries(d, *node, find_entry, &find))
        {
            free(copy);
            return 0;
        }
        if (!find.found)
        {
            free(copy);
            return failure(ENOENT);
        }
        *node = find.node;
    }
    free(copy);
    return 1;
}

static int root_member(const char *path)
{
    const char *member = tb_image_member(path);
    if (!member)
        return 0;
    while (*member)
    {
        if (*member == '/')
            member++;
        else if (*member == '.' && (!member[1] || member[1] == '/'))
            member++;
        else
            return 0;
    }
    return 1;
}

int tb_image_info(const char *path, TbImageInfo *info)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_info(path, info);
    if (tb_fat_volume(path))
        return tb_fat_info(path, info);
    Disk disk;
    const char *member;
    if (!open_disk(path, &disk, &member))
    {
        /* An empty device still occupies a drive letter and has a root. */
        if (tb_image_device(path) && errno == ENXIO && root_member(path))
        {
            int kind = 0;
            char *source = device_source(path, &kind, NULL);
            free(source);
            if (errno != ENXIO)
                return 0;
            *info = (TbImageInfo){.modified = INT64_MIN};
            info->directory = 1;
            info->fat = kind == 2;
            strcpy(info->filesystem, kind == 2 ? "FAT" : kind == 1 ? "CDFS" : "UNKNOWN");
            info->sector_bytes = info->cluster_bytes = kind == 2 ? 512 : SECTOR;
            return 1;
        }
        return 0;
    }
    Node node;
    int ok = lookup(&disk, member, &node);
    if (ok)
    {
        *info = (TbImageInfo){.modified = node.modified,
                              .accessed = node.accessed,
                              .created = node.created,
                              .attributes = node.attributes & 0x26};
        info->size = node.size;
        info->readonly = disk.readonly || !!(node.attributes & 1);
        if (disk.fat_bits && !disk.readonly)
            for (uint32_t i = 2; i < disk.clusters + 2; i++)
            {
                size_t at = disk.fat_bits == 12 ? i + i / 2 : i * 2;
                unsigned value = le16(disk.fat + at);
                if (disk.fat_bits == 12)
                    value = i & 1 ? value >> 4 : value & 0xfff;
                if (!value)
                    info->available += disk.cluster_bytes;
            }
        info->capacity = disk.bytes;
        info->directory = node.directory;
        info->fat = !!disk.fat_bits;
        strcpy(info->filesystem, disk.fat_bits ? "FAT" : "CDFS");
        info->serial = disk.serial;
        info->reserved_sectors = disk.reserved_sectors;
        info->fat_count = disk.fat_count;
        info->root_entries = disk.root_entries;
        info->fat_sectors = disk.fat_sectors;
        info->sectors_per_track = disk.sectors_per_track;
        info->heads = disk.heads;
        info->hidden_sectors = disk.hidden_sectors;
        info->media = disk.media;
        info->sector_bytes = disk.fat_bits ? disk.sector_bytes : SECTOR;
        info->cluster_bytes = disk.fat_bits ? disk.cluster_bytes : SECTOR;
        memcpy(info->label, disk.label, sizeof info->label);
    }
    close_disk(&disk);
    return ok;
}

char *tb_image_absolute(const char *path)
{
    char *decoded = decoded_source(path);
    if (!decoded)
        return NULL;
    free(decoded);
    char *result = strdup(path);
    if (!result)
        return NULL;
    /* The encoded source is a single component, so lexical normalization is safe. */
    const char *source = path + prefix_size(path);
    const char *end = strchr(source, '/');
    size_t root = end ? (size_t)(end - path) : strlen(path), used = root;
    if (end)
    {
        char *tail = strdup(end + 1);
        if (!tail)
        {
            free(result);
            return NULL;
        }
        char *cursor = tail;
        while (*cursor)
        {
            while (*cursor == '/')
                cursor++;
            if (!*cursor)
                break;
            char *part = cursor;
            while (*cursor && *cursor != '/')
                cursor++;
            if (*cursor)
                *cursor++ = 0;
            if (strpbrk(part, "\\:"))
            {
                free(tail);
                free(result);
                errno = EINVAL;
                return NULL;
            }
            if (!strcmp(part, ".."))
            {
                if (used <= root)
                {
                    free(tail);
                    free(result);
                    errno = EACCES;
                    return NULL;
                }
                while (used > root && result[used - 1] != '/')
                    used--;
                if (used > root)
                    used--;
            }
            else if (strcmp(part, "."))
            {
                result[used++] = '/';
                size_t n = strlen(part);
                memcpy(result + used, part, n);
                used += n;
            }
        }
        free(tail);
    }
    result[used] = 0;
    return result;
}

char *tb_image_canonical(const char *path)
{
    TbImageInfo info;
    return tb_image_info(path, &info) ? tb_image_absolute(path) : NULL;
}

typedef struct Names
{
    char **items;
    size_t count;
} Names;

static int add_name(void *context, const char *name, const Node *node)
{
    (void)node;
    Names *list = context;
    char *copy = strdup(name);
    if (!copy)
        return -1;
    char **items = realloc(list->items, (list->count + 1) * sizeof *items);
    if (!items)
    {
        free(copy);
        return -1;
    }
    list->items = items;
    list->items[list->count++] = copy;
    return 1;
}

char **tb_image_names(const char *path, size_t *count)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_names(path, count);
    if (tb_fat_volume(path))
        return tb_fat_names(path, count);
    Disk disk;
    const char *member;
    *count = (size_t)-1;
    if (!open_disk(path, &disk, &member))
        return NULL;
    Node node;
    Names names = {0};
    int ok = lookup(&disk, member, &node) && entries(&disk, node, add_name, &names);
    close_disk(&disk);
    if (!ok)
    {
        for (size_t i = 0; i < names.count; i++)
            free(names.items[i]);
        free(names.items);
        return NULL;
    }
    *count = names.count;
    return names.items;
}

static FILE *disk_fopen(Disk *disk, const char *member)
{
    Node node;
    FILE *output = NULL;
    unsigned char *buffer = NULL, *visited = NULL;

    enum
    {
        BUFFER_SIZE = 65536
    };

    if (!lookup(disk, member, &node))
        goto done;
    if (node.directory)
    {
        errno = EISDIR;
        goto done;
    }
    output = tmpfile();
    if (!output)
        goto done;
    uint64_t remaining = node.size, offset = node.offset;
    uint32_t cluster = node.cluster, steps = 0, within = 0;
    buffer = malloc(BUFFER_SIZE);
    if (!buffer)
        goto failed;
    visited = disk->fat_bits ? calloc((disk->clusters + 9u) / 8u, 1) : NULL;
    if (disk->fat_bits && !visited)
        goto failed;
    while (remaining)
    {
        size_t chunk = remaining > BUFFER_SIZE ? BUFFER_SIZE : (size_t)remaining;
        if (disk->fat_bits)
        {
            if (cluster < 2 || cluster >= disk->clusters + 2 ||
                cluster >= (disk->fat_bits == 12 ? 0xff0u : 0xfff0u) || steps >= disk->clusters)
            {
                errno = EIO;
                goto failed;
            }
            if (!within)
            {
                if (visited[cluster / 8] & (1u << (cluster % 8)))
                {
                    errno = EIO;
                    goto failed;
                }
                visited[cluster / 8] |= (unsigned char)(1u << (cluster % 8));
            }
            offset = disk->data_offset + (uint64_t)(cluster - 2) * disk->cluster_bytes + within;
            if (chunk > disk->cluster_bytes - within)
                chunk = disk->cluster_bytes - within;
        }
        if (!read_at(disk, offset, buffer, chunk) || fwrite(buffer, 1, chunk, output) != chunk)
            goto failed;
        remaining -= chunk;
        offset += chunk;
        if (disk->fat_bits)
        {
            within += (uint32_t)chunk;
            if (within == disk->cluster_bytes && remaining)
            {
                cluster = next_cluster(disk, cluster);
                within = 0;
                steps++;
                if (!cluster || cluster == UINT32_MAX)
                {
                    errno = EIO;
                    goto failed;
                }
            }
        }
    }
    if (fflush(output) || fseeko(output, 0, SEEK_SET))
        goto failed;
    goto done;
failed:
{
    int error = errno;
    fclose(output);
    output = NULL;
    errno = error;
}

done:
    free(visited);
    free(buffer);
    return output;
}

FILE *tb_image_fopen(const char *path)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_fopen(path);
    if (tb_fat_volume(path))
        return tb_fat_fopen(path);
    Disk disk;
    const char *member;
    if (!open_disk(path, &disk, &member))
        return NULL;
    FILE *output = disk_fopen(&disk, member);
    close_disk(&disk);
    return output;
}

static int stream_disk(FILE *file, Disk *disk)
{
    memset(disk, 0, sizeof *disk);
    disk->file = file;
    if (fseeko(file, 0, SEEK_END))
        return 0;
    int64_t size = ftello(file);
    if (size < 0)
        return 0;
    disk->bytes = (uint64_t)size;
    return identify(disk);
}

FILE *tb_image_stream_fopen(FILE *file, const char *member)
{
    Disk disk;
    if (!stream_disk(file, &disk))
    {
        free(disk.fat);
        return NULL;
    }
    FILE *out = disk_fopen(&disk, member);
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

static int stream_entry(void *opaque, const char *name, const Node *node)
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

static int reader_disk(TbReadStream *stream, Disk *disk)
{
    ImageIndex *index = tb_stream_index(stream);
    if (index)
    {
        *disk = index->disk;
        return 1;
    }
    memset(disk, 0, sizeof *disk);
    disk->stream = stream;
    disk->bytes = tb_stream_size(stream);
    if (!identify(disk))
        return 0;
    index = calloc(1, sizeof *index);
    if (index)
    {
        index->bytes = sizeof *index + disk->fat_size;
        if (tb_stream_set_index(stream, index, index->bytes, image_index_free))
        {
            disk->index = index;
            index->disk = *disk;
        }
        else
            free(index);
    }
    return 1;
}

static int disk_walk(Disk *disk, TbImageVisitor visitor, void *context)
{
    StreamWalk walk = {.visitor = visitor, .context = context, .parent = ""};
    int result = entries(disk, disk->root, stream_entry, &walk);
    for (size_t i = 0; result == 1 && i < walk.count; i++)
    {
        walk.parent = walk.directories[i];
        Node node;
        result = lookup(disk, walk.parent, &node) ? entries(disk, node, stream_entry, &walk) : 0;
    }
    for (size_t i = 0; i < walk.count; i++)
        free(walk.directories[i]);
    free(walk.directories);
    return walk.result ? walk.result : result;
}

int tb_image_stream_walk(FILE *file, TbImageVisitor visitor, void *context)
{
    Disk disk;
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
    Disk disk;
    if (!reader_disk(stream, &disk))
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
    Disk disk;
    Node node;
    if (!reader_disk(stream, &disk))
    {
        free(disk.fat);
        return 0;
    }
    StreamWalk walk = {.visitor = visitor, .context = context, .parent = member};
    int result = lookup(&disk, member, &node) ? entries(&disk, node, stream_entry, &walk) : 0;
    for (size_t i = 0; i < walk.count; i++)
        free(walk.directories[i]);
    free(walk.directories);
    if (!disk.index)
        free(disk.fat);
    return walk.result ? walk.result : result;
}

void tb_image_reader_cache_stats(TbReadStream *stream, uint64_t *builds, uint64_t *hits)
{
    ImageIndex *index = tb_stream_index(stream);
    *builds = index ? index->builds : 0;
    *hits = index ? index->hits : 0;
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
    Disk disk;
    Node node;
    TbReadStream *result = NULL;
    if (!reader_disk(stream, &disk) || !lookup(&disk, member, &node))
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
            valid = failure(EIO);
            break;
        }
        visited[cluster / 8] |= (unsigned char)(1u << (cluster % 8));
        context->clusters[i] = cluster;
        if (i + 1 < count)
            cluster = next_cluster(&disk, cluster);
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

/* FAT updates stage a complete filesystem in memory before touching the source.
   The source is exclusively locked through validation and commit. */
static void store16(unsigned char *p, unsigned value)
{
    p[0] = value;
    p[1] = value >> 8;
}

static void store32(unsigned char *p, uint32_t value)
{
    store16(p, value);
    store16(p + 2, value >> 16);
}

static unsigned fat_value(Disk *d, uint32_t cluster)
{
    size_t at = d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
    unsigned value = le16(d->fat + at);
    return d->fat_bits == 12 ? (cluster & 1 ? value >> 4 : value & 0xfff) : value;
}

static void fat_set(Disk *d, uint32_t cluster, unsigned value)
{
    size_t at = d->fat_bits == 12 ? cluster + cluster / 2 : cluster * 2;
    if (d->fat_bits == 12)
    {
        unsigned old = le16(d->fat + at);
        value = cluster & 1 ? (old & 15) | (value << 4) : (old & 0xf000) | value;
    }
    store16(d->fat + at, value);
}

static uint64_t cluster_offset(Disk *d, uint32_t cluster)
{
    return d->data_offset + (uint64_t)(cluster - 2) * d->cluster_bytes;
}

static uint32_t allocate_cluster(Disk *d)
{
    for (uint32_t i = 2; i < d->clusters + 2; i++)
        if (!fat_value(d, i))
        {
            fat_set(d, i, d->fat_bits == 12 ? 0xfff : 0xffff);
            memset(d->memory + cluster_offset(d, i), 0, d->cluster_bytes);
            return i;
        }
    errno = ENOSPC;
    return 0;
}

static int release_chain(Disk *d, uint32_t cluster)
{
    for (uint32_t steps = 0; cluster; steps++)
    {
        if (steps >= d->clusters)
            return failure(EIO);
        uint32_t next = next_cluster(d, cluster);
        if (next == UINT32_MAX)
            return 0;
        fat_set(d, cluster, 0);
        cluster = next;
    }
    return 1;
}

static int stage_disk(const char *path, const char *expected, Disk *d, const char **member)
{
    if (expected)
    {
        char *current = tb_image_source(path);
        int same = current && !strcmp(current, expected);
        free(current);
        if (!same)
            return failure(ENXIO);
    }
    if (!open_disk_mode(path, d, member, 1))
        return 0;
    if (d->bytes > SIZE_MAX)
    {
        close_disk(d);
        return failure(ENOMEM);
    }
    unsigned char *memory = malloc((size_t)d->bytes);
    if (!memory)
    {
        close_disk(d);
        return 0;
    }
    if (!read_at(d, 0, memory, (size_t)d->bytes))
    {
        free(memory);
        close_disk(d);
        return 0;
    }
    d->memory = memory;
    return 1;
}

static int commit_disk(Disk *d)
{
    for (unsigned i = 0; i < d->fat_count; i++)
        memcpy(d->memory + (uint64_t)d->reserved_sectors * d->sector_bytes + i * d->fat_size,
               d->fat, d->fat_size);
    if (fseeko(d->file, 0, SEEK_SET) ||
        fwrite(d->memory, 1, (size_t)d->bytes, d->file) != d->bytes || fflush(d->file))
        return failure(errno ? errno : EIO);
#ifdef _WIN32
    return _commit(_fileno(d->file)) ? failure(errno) : 1;
#else
    return fsync(fileno(d->file)) ? failure(errno) : 1;
#endif
}

static int parent_node(Disk *d, const char *member, Node *parent, char **name)
{
    char *copy = strdup(member);
    if (!copy)
        return 0;
    size_t n = strlen(copy);
    while (n && copy[n - 1] == '/')
        copy[--n] = 0;
    char *slash = strrchr(copy, '/');
    *name = strdup(slash ? slash + 1 : copy);
    if (!*name)
    {
        free(copy);
        return 0;
    }
    if (slash)
        *slash = 0;
    else
        *copy = 0;
    int ok = safe_name(*name) && !strpbrk(*name, "<>\"|?*") && lookup(d, copy, parent);
    free(copy);
    if (!ok)
    {
        free(*name);
        *name = NULL;
        if (!errno)
            errno = EINVAL;
        return 0;
    }
    return parent->directory ? 1 : failure(ENOTDIR);
}

static void record_time(unsigned char *entry)
{
    time_t seconds = time(NULL);
    struct tm value;
#ifdef _WIN32
    localtime_s(&value, &seconds);
#else
    localtime_r(&seconds, &value);
#endif
    unsigned year = value.tm_year + 1900;
    if (year < 1980)
        year = 1980;
    if (year > 2107)
        year = 2107;
    store16(entry + 22, value.tm_hour << 11 | value.tm_min << 5 | value.tm_sec / 2);
    store16(entry + 24, (year - 1980) << 9 | (value.tm_mon + 1) << 5 | value.tm_mday);
}

static int short_exists(Disk *d, Node parent, const unsigned char name[11])
{
    size_t size;
    unsigned char *data = directory_data(d, parent, &size);
    if (!data)
        return -1;
    int found = 0;
    for (size_t i = 0; i + 32 <= size && data[i]; i += 32)
        if (data[i] != 0xe5 && data[i + 11] != 15 && !memcmp(data + i, name, 11))
        {
            found = 1;
            break;
        }
    free(data);
    return found;
}

static int name_units(const char *name, uint16_t units[260], size_t *length)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)name;
    while (*p)
    {
        unsigned value = *p++;
        if (value >= 0x80)
        {
            unsigned more = value >= 0xe0 && value < 0xf0   ? 2
                            : value >= 0xc2 && value < 0xe0 ? 1
                                                            : 0;
            if (!more)
                return failure(ENOTSUP);
            value &= more == 2 ? 15 : 31;
            for (unsigned i = 0; i < more; i++)
            {
                if ((*p & 0xc0) != 0x80)
                    return failure(EINVAL);
                value = value << 6 | (*p++ & 63);
            }
            if ((more == 2 && value < 0x800) || (value >= 0xd800 && value <= 0xdfff))
                return failure(EINVAL);
        }
        if (value < 32 || n == 255)
            return failure(ENAMETOOLONG);
        units[n++] = value;
    }
    if (!n || units[n - 1] == ' ' || units[n - 1] == '.')
        return failure(EINVAL);
    *length = n;
    while (n < 260)
        units[n++] = 0xffff;
    return 1;
}

static int short_character(unsigned char c)
{
    return c < 128 && (isalnum(c) || strchr("!#$%&'()-@^_`{}~", c));
}

static int make_short(Disk *d, Node parent, const char *name, unsigned char alias[11], int *lfn)
{
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name), ext = dot ? strlen(dot + 1) : 0;
    memset(alias, ' ', 11);
    *lfn = base > 8 || ext > 3 || !base;
    for (size_t i = 0; i < base; i++)
    {
        unsigned char c = name[i];
        if (!short_character(c) || islower(c))
            *lfn = 1;
        if (i < 8)
            alias[i] = short_character(c) ? toupper(c) : '_';
    }
    for (size_t i = 0; i < ext && i < 3; i++)
    {
        unsigned char c = dot[i + 1];
        if (!short_character(c) || islower(c))
            *lfn = 1;
        alias[8 + i] = short_character(c) ? toupper(c) : '_';
    }
    if (!*lfn)
    {
        int found = short_exists(d, parent, alias);
        return found < 0 ? 0 : found ? failure(EEXIST) : 1;
    }
    unsigned char stem[8];
    memcpy(stem, alias, 8);
    for (unsigned i = 1; i < 1000000; i++)
    {
        char number[9];
        snprintf(number, sizeof number, "~%u", i);
        size_t digits = strlen(number), prefix = 8 - digits;
        while (prefix && stem[prefix - 1] == ' ')
            prefix--;
        memset(alias, ' ', 8);
        memcpy(alias, stem, prefix);
        memcpy(alias + prefix, number, digits);
        int exists = short_exists(d, parent, alias);
        if (exists < 0)
            return 0;
        if (!exists)
            return 1;
    }
    return failure(ENOSPC);
}

static int directory_slots(Disk *d, Node parent, size_t required, size_t *slot)
{
    for (;;)
    {
        size_t size;
        unsigned char *data = directory_data(d, parent, &size);
        if (!data)
            return 0;
        size_t run = 0;
        int end = 0;
        for (size_t at = 0; at + 32 <= size; at += 32)
        {
            if (!data[at])
                end = 1;
            run = end || data[at] == 0xe5 ? run + 1 : 0;
            if (run == required)
            {
                *slot = at / 32 + 1 - required;
                free(data);
                return 1;
            }
        }
        free(data);
        if (!parent.cluster)
            return failure(ENOSPC);
        uint32_t last = parent.cluster, next;
        for (uint32_t steps = 0;; steps++)
        {
            if (steps >= d->clusters)
                return failure(EIO);
            next = next_cluster(d, last);
            if (next == UINT32_MAX)
                return 0;
            if (!next)
                break;
            last = next;
        }
        uint32_t added = allocate_cluster(d);
        if (!added)
            return 0;
        fat_set(d, last, added);
    }
}

static int new_record(Disk *d, Node parent, const char *name, const unsigned char *previous,
                      Node *result)
{
    unsigned char alias[11];
    int lfn;
    uint16_t units[260];
    size_t n;
    if (!name_units(name, units, &n) || !make_short(d, parent, name, alias, &lfn))
        return 0;
    size_t count = lfn ? (n + 12) / 13 : 0, slot;
    if (!directory_slots(d, parent, count + 1, &slot))
        return 0;
    unsigned checksum = 0;
    for (unsigned i = 0; i < 11; i++)
        checksum = ((((checksum & 1) << 7) | checksum >> 1) + alias[i]) & 255;
    if (n < 260)
        units[n] = 0;
    static const unsigned offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (size_t i = 0; i < count; i++)
    {
        uint64_t offset = directory_offset(d, parent, (slot + i) * 32);
        if (offset == UINT64_MAX)
            return failure(EIO);
        unsigned char *entry = d->memory + offset;
        memset(entry, 0, 32);
        unsigned ordinal = (unsigned)(count - i);
        entry[0] = ordinal | (!i ? 0x40 : 0);
        entry[11] = 15;
        entry[13] = checksum;
        for (unsigned j = 0; j < 13; j++)
            store16(entry + offsets[j], units[(ordinal - 1) * 13 + j]);
    }
    uint64_t offset = directory_offset(d, parent, (slot + count) * 32);
    if (offset == UINT64_MAX)
        return failure(EIO);
    unsigned char *entry = d->memory + offset;
    if (previous)
        memcpy(entry, previous, 32);
    else
    {
        memset(entry, 0, 32);
        entry[11] = 32;
        record_time(entry);
    }
    memcpy(entry, alias, 11);
    *result = (Node){0};
    result->record = offset;
    result->cluster = le16(entry + 26);
    result->size = le32(entry + 28);
    result->attributes = entry[11];
    result->directory = !!(entry[11] & 16);
    return 1;
}

static int erase_record(Disk *d, Node parent, Node node)
{
    size_t size;
    unsigned char *data = directory_data(d, parent, &size);
    if (!data)
        return 0;
    size_t slot = SIZE_MAX;
    for (size_t at = 0; at + 32 <= size; at += 32)
        if (directory_offset(d, parent, at) == node.record)
        {
            slot = at;
            break;
        }
    if (slot == SIZE_MAX)
    {
        free(data);
        return failure(EIO);
    }
    d->memory[node.record] = 0xe5;
    while (slot >= 32 && data[slot - 32 + 11] == 15 && data[slot - 32] != 0xe5)
    {
        slot -= 32;
        uint64_t offset = directory_offset(d, parent, slot);
        if (offset == UINT64_MAX)
        {
            free(data);
            return failure(EIO);
        }
        d->memory[offset] = 0xe5;
    }
    free(data);
    return 1;
}

static int file_contents(Disk *d, Node node, unsigned char *out, size_t size)
{
    uint32_t cluster = node.cluster;
    for (size_t at = 0, steps = 0; at < size; steps++)
    {
        if (!cluster || cluster == UINT32_MAX || steps >= d->clusters)
            return failure(EIO);
        size_t n = size - at < d->cluster_bytes ? size - at : d->cluster_bytes;
        if (cluster < 2 || cluster >= d->clusters + 2)
            return failure(EIO);
        memcpy(out + at, d->memory + cluster_offset(d, cluster), n);
        at += n;
        cluster = next_cluster(d, cluster);
    }
    return 1;
}

static int replace_contents(Disk *d, Node node, const void *contents, uint64_t size)
{
    if (node.directory)
        return failure(EISDIR);
    if (node.attributes & 1)
        return failure(EACCES);
    if (size > UINT32_MAX || size > d->bytes)
        return failure(ENOSPC);
    if (!release_chain(d, node.cluster))
        return 0;
    uint32_t first = 0, last = 0;
    for (uint64_t at = 0; at < size; at += d->cluster_bytes)
    {
        uint32_t cluster = allocate_cluster(d);
        if (!cluster)
            return 0;
        if (!first)
            first = cluster;
        if (last)
            fat_set(d, last, cluster);
        last = cluster;
        size_t n = size - at < d->cluster_bytes ? (size_t)(size - at) : d->cluster_bytes;
        memcpy(d->memory + cluster_offset(d, cluster), (const unsigned char *)contents + at, n);
    }
    unsigned char *entry = d->memory + node.record;
    store16(entry + 26, first);
    store32(entry + 28, (uint32_t)size);
    entry[11] |= 32;
    record_time(entry);
    return 1;
}

static int change_contents(const char *path, const char *expected, uint64_t offset,
                           const void *bytes, size_t length, uint64_t resized, int resize)
{
    Disk disk;
    const char *member;
    if (!stage_disk(path, expected, &disk, &member))
        return 0;
    Node node;
    unsigned char *contents = NULL;
    int ok = lookup(&disk, member, &node);
    uint64_t size = resize ? resized : offset <= UINT64_MAX - length ? offset + length : UINT64_MAX;
    if (ok && !resize && size < node.size)
        size = node.size;
    if (ok && (size > UINT32_MAX || size > disk.bytes))
        ok = failure(ENOSPC);
    if (ok)
    {
        contents = calloc(size ? (size_t)size : 1, 1);
        if (!contents)
            ok = 0;
    }
    if (ok)
        ok = file_contents(&disk, node, contents, (size_t)(node.size < size ? node.size : size));
    if (ok && length)
        memcpy(contents + offset, bytes, length);
    if (ok)
        ok = replace_contents(&disk, node, contents, size) && commit_disk(&disk);
    free(contents);
    close_disk(&disk);
    return ok;
}

int tb_image_write(const char *path, const char *expected, uint64_t offset, const void *bytes,
                   size_t size)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_WRITE, offset, bytes, size, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_WRITE, offset, bytes, size, NULL);
    return change_contents(path, expected, offset, bytes, size, 0, 0);
}

int tb_image_resize(const char *path, const char *expected, uint64_t size)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_RESIZE, size, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_RESIZE, size, NULL, 0, NULL);
    return change_contents(path, expected, 0, NULL, 0, size, 1);
}

int tb_image_create_file(const char *path, int exclusive)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, exclusive ? TB_FS_EXCLUSIVE : TB_FS_CREATE, 0, NULL, 0,
                              NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, exclusive ? TB_FS_EXCLUSIVE : TB_FS_CREATE, 0, NULL, 0,
                             NULL);
    Disk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    Node node, parent;
    char *name = NULL;
    int ok = lookup(&disk, member, &node);
    if (ok)
        ok = exclusive ? failure(EEXIST) : node.directory ? failure(EISDIR) : 1;
    else if (errno == ENOENT)
        ok = parent_node(&disk, member, &parent, &name) &&
             new_record(&disk, parent, name, NULL, &node) && commit_disk(&disk);
    free(name);
    close_disk(&disk);
    return ok;
}

static int count_entry(void *value, const char *name, const Node *node)
{
    (void)name;
    (void)node;
    (*(unsigned *)value)++;
    return 1;
}

int tb_image_remove(const char *path, int directory)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, directory ? TB_FS_RMDIR : TB_FS_DELETE, 0, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, directory ? TB_FS_RMDIR : TB_FS_DELETE, 0, NULL, 0, NULL);
    Disk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    Node node, parent;
    char *name = NULL;
    unsigned children = 0;
    int ok = lookup(&disk, member, &node) && parent_node(&disk, member, &parent, &name);
    if (ok && node.directory != !!directory)
        ok = failure(node.directory ? EISDIR : ENOTDIR);
    if (ok && node.attributes & 1)
        ok = failure(EACCES);
    if (ok && directory)
        ok = entries(&disk, node, count_entry, &children) && (!children || failure(ENOTEMPTY));
    if (ok)
        ok = release_chain(&disk, node.cluster) && erase_record(&disk, parent, node) &&
             commit_disk(&disk);
    free(name);
    close_disk(&disk);
    return ok;
}

int tb_image_mkdir(const char *path)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, TB_FS_MKDIR, 0, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, TB_FS_MKDIR, 0, NULL, 0, NULL);
    Disk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    Node node, parent;
    char *name = NULL;
    int ok = lookup(&disk, member, &node);
    if (ok)
        ok = failure(EEXIST);
    else if (errno == ENOENT)
    {
        ok = parent_node(&disk, member, &parent, &name) &&
             new_record(&disk, parent, name, NULL, &node);
        uint32_t cluster = ok ? allocate_cluster(&disk) : 0;
        if (!cluster)
            ok = 0;
        if (ok)
        {
            unsigned char *record = disk.memory + node.record;
            record[11] = 16;
            store16(record + 26, cluster);
            unsigned char *dot = disk.memory + cluster_offset(&disk, cluster);
            memset(dot, ' ', 11);
            dot[0] = '.';
            dot[11] = 16;
            store16(dot + 26, cluster);
            record_time(dot);
            memcpy(dot + 32, dot, 32);
            dot[33] = '.';
            store16(dot + 32 + 26, parent.cluster);
            ok = commit_disk(&disk);
        }
    }
    free(name);
    close_disk(&disk);
    return ok;
}

int tb_image_readonly(const char *path, int readonly)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, NULL, readonly ? TB_FS_READONLY : TB_FS_WRITABLE, 0, NULL, 0,
                              NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, NULL, readonly ? TB_FS_READONLY : TB_FS_WRITABLE, 0, NULL, 0,
                             NULL);
    Disk disk;
    const char *member;
    if (!stage_disk(path, NULL, &disk, &member))
        return 0;
    Node node;
    int ok = lookup(&disk, member, &node);
    if (ok && !node.record)
        ok = failure(EACCES);
    if (ok)
    {
        unsigned char *record = disk.memory + node.record;
        record[11] = readonly ? record[11] | 1 : record[11] & ~1u;
        ok = commit_disk(&disk);
    }
    close_disk(&disk);
    return ok;
}

static void store_stamp(unsigned char *field, uint32_t stamp)
{
    field[0] = (unsigned char)stamp;
    field[1] = (unsigned char)(stamp >> 8);
    field[2] = (unsigned char)(stamp >> 16);
    field[3] = (unsigned char)(stamp >> 24);
}

int tb_image_stamps(const char *path, const char *expected, const TbImageStamps *stamps)
{
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_TOUCH, 0, stamps, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_TOUCH, 0, stamps, 0, NULL);
    Disk disk;
    const char *member;
    if (!stage_disk(path, expected, &disk, &member))
        return 0;
    Node node;
    int ok = lookup(&disk, member, &node);
    if (ok && !node.record)
        ok = failure(EACCES);
    if (ok)
    {
        unsigned char *record = disk.memory + node.record;
        if (stamps->created != INT64_MIN)
            store_stamp(record + 14, tb_dos_stamp(stamps->created));
        if (stamps->accessed != INT64_MIN)
        {
            uint32_t stamp = tb_dos_stamp(stamps->accessed);
            record[18] = (unsigned char)(stamp >> 16);
            record[19] = (unsigned char)(stamp >> 24);
        }
        if (stamps->modified != INT64_MIN)
            store_stamp(record + 22, tb_dos_stamp(stamps->modified));
        ok = commit_disk(&disk);
    }
    close_disk(&disk);
    return ok;
}

int tb_image_attributes(const char *path, const char *expected, unsigned attributes)
{
    attributes &= 0x26;
    if (tb_hpfs_volume(path))
        return tb_hpfs_modify(path, expected, TB_FS_ATTRIBUTES, attributes, NULL, 0, NULL);
    if (tb_fat_volume(path))
        return tb_fat_modify(path, expected, TB_FS_ATTRIBUTES, attributes, NULL, 0, NULL);
    Disk disk;
    const char *member;
    if (!stage_disk(path, expected, &disk, &member))
        return 0;
    Node node;
    int ok = lookup(&disk, member, &node);
    if (ok && !node.record)
        ok = failure(EACCES);
    if (ok)
    {
        unsigned char *record = disk.memory + node.record;
        record[11] = (unsigned char)((record[11] & ~0x26u) | attributes);
        ok = commit_disk(&disk);
    }
    close_disk(&disk);
    return ok;
}

int tb_image_move(const char *source, const char *target)
{
    if (tb_hpfs_volume(source))
        return tb_hpfs_modify(source, NULL, TB_FS_RENAME, 0, NULL, 0, target);
    if (tb_fat_volume(source))
        return tb_fat_modify(source, NULL, TB_FS_RENAME, 0, NULL, 0, target);
    char *a = tb_image_source(source), *b = tb_image_source(target);
    int same = a && b && !strcmp(a, b);
    free(a);
    free(b);
    if (!same)
        return failure(EXDEV);
    Disk disk;
    const char *member;
    if (!stage_disk(source, NULL, &disk, &member))
        return 0;
    Node node, parent, destination, found, created;
    char *oldname = NULL, *newname = NULL;
    const char *tail = tb_image_member(target);
    int ok = tail && lookup(&disk, member, &node) &&
             parent_node(&disk, member, &parent, &oldname) &&
             parent_node(&disk, tail, &destination, &newname);
    if (ok && lookup(&disk, tail, &found))
        ok = failure(EEXIST);
    if (ok && node.attributes & 1)
        ok = failure(EACCES);
    if (ok && node.directory)
    {
        Node ancestor = destination;
        for (uint32_t steps = 0; ancestor.cluster; steps++)
        {
            if (ancestor.cluster == node.cluster)
            {
                ok = failure(EINVAL);
                break;
            }
            if (steps >= disk.clusters || ancestor.cluster < 2 ||
                ancestor.cluster >= disk.clusters + 2)
            {
                ok = failure(EIO);
                break;
            }
            uint64_t offset = cluster_offset(&disk, ancestor.cluster);
            uint32_t upper = le16(disk.memory + offset + 32 + 26);
            ancestor.cluster = upper;
        }
    }
    unsigned char original[32];
    if (ok)
    {
        memcpy(original, disk.memory + node.record, 32);
        ok = new_record(&disk, destination, newname, original, &created) &&
             erase_record(&disk, parent, node);
    }
    if (ok && node.directory)
        store16(disk.memory + cluster_offset(&disk, node.cluster) + 32 + 26, destination.cluster);
    if (ok)
        ok = commit_disk(&disk);
    free(oldname);
    free(newname);
    close_disk(&disk);
    return ok;
}

int tb_image_copy(FILE *source, const char *destination, int exclusive)
{
    if (fseeko(source, 0, SEEK_END))
        return 0;
    int64_t size = ftello(source);
    if (size < 0 || (uint64_t)size > SIZE_MAX || (uint64_t)size > UINT32_MAX)
        return failure(EFBIG);
    unsigned char *bytes = malloc(size ? (size_t)size : 1);
    if (!bytes)
        return 0;
    int ok = !fseeko(source, 0, SEEK_SET) && fread(bytes, 1, (size_t)size, source) == (size_t)size;
    if (ok)
        ok = tb_image_create_file(destination, exclusive);
    if (ok)
        ok = tb_image_resize(destination, NULL, 0) &&
             tb_image_write(destination, NULL, 0, bytes, (size_t)size);
    free(bytes);
    return ok;
}

int tb_image_create_floppy(const char *path, unsigned kib)
{
    const struct Geometry
    {
        unsigned kib, sectors, cluster, fat, roots, track, media;
    } formats[] = {{360, 720, 2, 2, 112, 9, 0xfd},
                   {720, 1440, 2, 3, 112, 9, 0xf9},
                   {1200, 2400, 1, 7, 224, 15, 0xf9},
                   {1440, 2880, 1, 9, 224, 18, 0xf0},
                   {2880, 5760, 2, 9, 240, 36, 0xf0}};
    const struct Geometry *g = NULL;
    for (size_t i = 0; i < sizeof formats / sizeof formats[0]; i++)
        if (formats[i].kib == kib)
            g = &formats[i];
    if (!g)
        return failure(EINVAL);
    size_t size = (size_t)g->sectors * 512;
    unsigned char *bytes = calloc(size, 1);
    if (!bytes)
        return 0;
    memcpy(bytes,
           "\xeb\x3c\x90"
           "2INEFAT ",
           11);
    store16(bytes + 11, 512);
    bytes[13] = g->cluster;
    store16(bytes + 14, 1);
    bytes[16] = 2;
    store16(bytes + 17, g->roots);
    store16(bytes + 19, g->sectors);
    bytes[21] = g->media;
    store16(bytes + 22, g->fat);
    store16(bytes + 24, g->track);
    store16(bytes + 26, 2);
    bytes[38] = 0x29;
    store32(bytes + 39, (uint32_t)time(NULL));
    memcpy(bytes + 43, "NO NAME    FAT12   ", 19);
    /* A formatted data disk; its boot sector stops instead of executing zeros. */
    memcpy(bytes + 62, "\xfa\xf4\xeb\xfd", 4);
    bytes[510] = 0x55;
    bytes[511] = 0xaa;
    for (unsigned i = 0; i < 2; i++)
    {
        size_t at = (1 + i * g->fat) * 512;
        bytes[at] = g->media;
        bytes[at + 1] = bytes[at + 2] = 0xff;
    }
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = n ? malloc((size_t)n * sizeof *wide) : NULL;
    if (wide)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n);
    FILE *file = wide ? _wfopen(wide, L"wbx") : NULL;
    free(wide);
#else
    FILE *file = fopen(path, "wbx");
#endif
    int ok = file != NULL;
    if (file)
    {
        ok = fwrite(bytes, 1, size, file) == size;
        if (fclose(file))
            ok = 0;
        if (!ok)
            remove(path);
    }
    free(bytes);
    return ok;
}

int tb_image_create_volume(const char *path, const char *filesystem, uint64_t bytes)
{
    if (!strcmp(filesystem, "hpfs"))
        return tb_hpfs_format(path, bytes);
    if (!strcmp(filesystem, "fat32") || !strcmp(filesystem, "exfat"))
        return tb_fat_format(path, filesystem, bytes);
    return failure(ENOTSUP);
}

int tb_image_metadata_get(const char *path, const char *expected, void *data, size_t capacity,
                          size_t *size)
{
    *size = 0;
    char *current = tb_image_source(path);
    if (!current)
        return 0;
    int same = !expected || !strcmp(current, expected);
    free(current);
    if (!same)
        return failure(ENXIO);
    if (tb_hpfs_volume(path))
        return tb_hpfs_metadata_get(path, expected, data, capacity, size);
    return 1;
}

int tb_image_metadata_set(const char *path, const char *expected, const void *data, size_t size)
{
    int readonly = tb_image_protected(path);
    if (readonly < 0)
        return 0;
    if (readonly)
        return failure(EROFS);
    if (tb_hpfs_volume(path))
        return tb_hpfs_metadata_set(path, expected, data, size);
    return failure(ENOTSUP);
}
