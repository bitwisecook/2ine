/* Public domain. No rights reserved. Wholly AI-authored. */
/* /proc/self/maps names each mapping, its address range and its image. */
#define _GNU_SOURCE 1
#include "host/memory.h"
#include "host/linux/system/maps.h"
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

enum
{
    MAPS_BLOCK_BYTES = 4096,
    /* An address range, permissions, offset, device and inode, then a path of
       up to PATH_MAX bytes. */
    MAPS_LINE_BYTES = 128 + 4096
};

int lx_linux_maps_visit(int (*visit)(const char *line, size_t length, void *context), void *context)
{
    int maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (maps < 0)
        return 0;
    char block[MAPS_BLOCK_BYTES], line[MAPS_LINE_BYTES];
    size_t used = 0;
    ssize_t count;
    int more = 1;
    while (more && (count = read(maps, block, sizeof block)) > 0)
        for (ssize_t i = 0; more && i < count; i++)
        {
            if (used < sizeof line)
                line[used++] = block[i];
            if (block[i] == '\n')
            {
                more = visit(line, used, context);
                used = 0;
            }
        }
    if (more && used)
        visit(line, used, context);
    close(maps);
    return 1;
}

/* Reads a hexadecimal address and advances past it. */
static uintptr_t hex_address(const char **text, const char *end)
{
    uintptr_t value = 0;
    for (; *text < end; ++*text)
    {
        char digit = **text;
        unsigned nibble = digit >= '0' && digit <= '9'   ? (unsigned)(digit - '0')
                          : digit >= 'a' && digit <= 'f' ? (unsigned)(digit - 'a' + 10)
                                                         : 16u;
        if (nibble == 16u)
            break;
        value = value << 4 | nibble;
    }
    return value;
}

typedef struct MappingSearch
{
    uintptr_t address;
    char *text;
    size_t size;
    int found;
} MappingSearch;

static int find_mapping(const char *line, size_t length, void *context)
{
    MappingSearch *search = context;
    const char *at = line, *end = line + length;
    uintptr_t start = hex_address(&at, end);
    if (at == line || at == end || *at++ != '-')
        return 1;
    uintptr_t limit = hex_address(&at, end);
    if (search->address < start || search->address >= limit)
        return 1;
    size_t copy = length && line[length - 1] == '\n' ? length - 1 : length;
    if (copy >= search->size)
        copy = search->size - 1;
    for (size_t i = 0; i < copy; i++)
        search->text[i] = line[i];
    search->text[copy] = '\0';
    search->found = 1;
    return 0;
}

int lx_host_memory_mapping(const void *address, char *text, size_t size)
{
    if (!text || !size)
        return 0;
    *text = '\0';
    MappingSearch search = {(uintptr_t)address, text, size, 0};
    lx_linux_maps_visit(find_mapping, &search);
    return search.found;
}
