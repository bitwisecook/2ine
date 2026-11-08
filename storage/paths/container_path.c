/* Public domain. No rights reserved. Wholly AI-authored. */
/* /@2ine-media/<hex source>!<hex member>... paths and archive member-name sanitising. */
#define _POSIX_C_SOURCE 200809L
#include "storage/container.h"
#include "storage/archives/container_internal.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "storage/volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A container source is a host file or a file inside a mapped disk image. */
static char *source_canonical(const char *path)
{
    return tb_image_path(path) ? tb_image_canonical(path) : tb_host_canonical(path);
}

int tb_media_path(const char *path)
{
    return path && !strncmp(path, TB_MEDIA_PREFIX, strlen(TB_MEDIA_PREFIX));
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

char *tb_media_decode_part(const char *p, size_t n)
{
    if (n % 2)
    {
        tb_archive_fail(EINVAL);
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
            tb_archive_fail(EINVAL);
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
    char *absolute = source_canonical(source);
    if (!absolute)
        return NULL;
    char *encoded = encode(absolute);
    free(absolute);
    if (!encoded)
        return NULL;
    char *out = append(TB_MEDIA_PREFIX, encoded, "!");
    free(encoded);
    return out;
}

/* Archive member names are relative and never interpreted by the host filesystem. */
char *tb_media_sanitise_member(const char *name, int strip_root)
{
    if (!name)
        return NULL;
    if (strip_root)
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
    return last ? tb_media_decode_part(last + 1, strlen(last + 1)) : NULL;
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
        return source_canonical(path);
    const char *begin = path + strlen(TB_MEDIA_PREFIX), *end = strchr(begin, '!');
    if (!end)
        return NULL;
    char *decoded = tb_media_decode_part(begin, (size_t)(end - begin));
    if (!decoded)
        return NULL;
    char *canonical = source_canonical(decoded);
    free(decoded);
    return canonical;
}

char *tb_media_child(const char *directory, const char *name)
{
    if (!tb_media_path(directory) || strpbrk(name, "/\\:") || !strcmp(name, ".") ||
        !strcmp(name, ".."))
    {
        tb_archive_fail(EINVAL);
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
            if (strchr(source + strlen(TB_MEDIA_PREFIX), '!'))
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
    const char *p = path + strlen(TB_MEDIA_PREFIX);
    char *out = strdup("");
    if (!out)
        return NULL;
    while (1)
    {
        const char *end = strchr(p, '!');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        char *part = tb_media_decode_part(p, n);
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
