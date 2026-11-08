/* Public domain. No rights reserved. Wholly AI-authored. */
/* Image and device virtual paths, the 2INEMED1 device control file and source access. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/image_path.h"
#include "storage/volume/volume_internal.h"
#include "storage/paths/hex_path.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_PREFIX "/@2ine-image/"
#define DEVICE_PREFIX "/@2ine-device/"
/* Hex-encoded sources longer than this are rejected. */
#define ENCODED_SOURCE_LIMIT 65536u

/* Device control file: a fixed header followed by the current medium's path. */
#define CONTROL_MAGIC "2INEMED1"

enum
{
    CONTROL_MAGIC_BYTES = 8,
    CONTROL_KIND = 8,
    CONTROL_READONLY = 9,
    CONTROL_SOURCE_BYTES = 12, /* little-endian; zero when no medium is inserted */
    CONTROL_HEADER_BYTES = 16,
    CONTROL_SOURCE_LIMIT = 32768
};

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

char *tb_image_root(const char *source)
{
    return tb_hex_path_encode(IMAGE_PREFIX, source);
}

char *tb_image_device_root(const char *control)
{
    return tb_hex_path_encode(DEVICE_PREFIX, control);
}

FILE *tb_image_open_source(const char *path, int write)
{
    return tb_host_fopen(path, write ? "rb+" : "rb");
}

static FILE *open_source(const char *path)
{
    return tb_image_open_source(path, 0);
}

int tb_image_lock_source(FILE *file, int write)
{
    return tb_host_lock(file, write);
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
    if (!n || n > ENCODED_SOURCE_LIMIT)
    {
        errno = EINVAL;
        return NULL;
    }
    return tb_hex_path_decode(hex, n);
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
    unsigned char header[CONTROL_HEADER_BYTES];
    if (fread(header, 1, sizeof header, file) != sizeof header ||
        memcmp(header, CONTROL_MAGIC, CONTROL_MAGIC_BYTES) ||
        (header[CONTROL_KIND] < TB_DEVICE_CD || header[CONTROL_KIND] > TB_DEVICE_DISK))
    {
        fclose(file);
        errno = EINVAL;
        return NULL;
    }
    *kind = header[CONTROL_KIND];
    /* CD media are always read-only. */
    if (readonly)
        *readonly = header[CONTROL_KIND] == TB_DEVICE_CD || !!header[CONTROL_READONLY];
    uint32_t n = tb_le32(header + CONTROL_SOURCE_BYTES);
    if (n > CONTROL_SOURCE_LIMIT)
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

char *tb_image_resolve_source(const char *path, int *kind, int *readonly)
{
    return tb_image_device(path) ? device_source(path, kind, readonly) : decoded_source(path);
}

char *tb_image_source(const char *path)
{
    int kind = 0;
    return tb_image_resolve_source(path, &kind, NULL);
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
