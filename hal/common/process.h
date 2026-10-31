/* Public domain. No rights reserved. Wholly AI-authored. */
/* Serialize host argv as consecutive NUL-terminated strings. */
#ifndef TWOINE_HOST_PROCESS_INTERNAL_H
#define TWOINE_HOST_PROCESS_INTERNAL_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
static inline char *lx_host_pack_arguments(int argc, char *const *argv, size_t *size)
{
    size_t total = 0;
    for (int i = 0; i < argc; i++)
        total += strlen(argv[i]) + 1;
    char *buffer = malloc(total ? total : 1);
    if (buffer)
    {
        char *p = buffer;
        for (int i = 0; i < argc; i++)
        {
            size_t n = strlen(argv[i]) + 1;
            memcpy(p, argv[i], n);
            p += n;
        }
        *size = total;
    }
    return buffer;
}
#endif
