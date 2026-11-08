/* Public domain. No rights reserved. Wholly AI-authored. */
/* Member path normalisation shared by the HPFS, FatFs and built-in volumes. */
#define _POSIX_C_SOURCE 200809L
#include "storage/volume/member_path.h"
#include "storage/common/host_stdio.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *fail(char *copy, char *out, int error)
{
    free(copy);
    free(out);
    errno = error;
    return NULL;
}

char *tb_member_path(const char *member)
{
    if (!member || strpbrk(member, "\\:"))
    {
        errno = EINVAL;
        return NULL;
    }
    char *copy = strdup(member), *out = malloc(strlen(member) + 1);
    if (!copy || !out)
        return fail(copy, out, ENOMEM);
    /* Output length before each kept component, so ".." can drop it. */
    size_t starts[TB_MEMBER_PATH_DEPTH], depth = 0, used = 0;
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
            if (!depth)
                return fail(copy, out, EACCES);
            used = starts[--depth];
            continue;
        }
        if (depth == TB_MEMBER_PATH_DEPTH)
            return fail(copy, out, ENAMETOOLONG);
        starts[depth++] = used;
        if (used)
            out[used++] = '/';
        size_t n = strlen(part);
        memcpy(out + used, part, n);
        used += n;
    }
    out[used] = 0;
    free(copy);
    return out;
}
