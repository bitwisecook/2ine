/* Public domain. No rights reserved. Wholly AI-authored. */
/* Image and device virtual paths, the 2INEMED1 device control file and source access. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/image_path.h"
#include "storage/volume/volume_internal.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "formats/endian.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_PREFIX "/@2ine-image/"
#define DEVICE_PREFIX "/@2ine-device/"

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
    uint32_t n = tb_le32(header + 12);
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
