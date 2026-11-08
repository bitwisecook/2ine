/* Public domain. No rights reserved. Wholly AI-authored. */
/* DOSCALLS publishes only its own live clipboard memory-release callback. */
#include "runtime/runtime.h"
#include "host/library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void *segments[2];

static void *segment(uint16 *selector, int code)
{
    (void)code;
    for (unsigned i = 0; i < 2; i++)
        if (!segments[i])
        {
            *selector = (uint16)(0x100 + i * 8);
            return segments[i] = calloc(1, 65536);
        }
    return NULL;
}

static void release(uint16 selector)
{
    unsigned slot = (selector - 0x100) / 8;
    CHECK(slot < 2 && segments[slot]);
    free(segments[slot]);
    segments[slot] = NULL;
}

static uint32 foreign(void *memory)
{
    (void)memory;
    return 487;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && !GProcessState.freeSharedMemory);
    GProcessState.allocSegment = segment;
    GProcessState.freeSelector = release;
    char error[256];
    LxHostLibrary *library = lx_host_library_open(argv[1], error, sizeof error);
    CHECK(library && GProcessState.freeSharedMemory);
    lx_host_library_close(library);
    CHECK(!GProcessState.freeSharedMemory && !segments[0] && !segments[1]);
    library = lx_host_library_open(argv[1], error, sizeof error);
    CHECK(library && GProcessState.freeSharedMemory);
    GProcessState.freeSharedMemory = foreign;
    lx_host_library_close(library);
    CHECK(GProcessState.freeSharedMemory == foreign && !segments[0] && !segments[1]);
    GProcessState.freeSharedMemory = NULL;
    return 0;
}
