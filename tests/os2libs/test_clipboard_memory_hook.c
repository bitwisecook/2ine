/* Public domain. No rights reserved. Wholly AI-authored. */
/* DOSCALLS publishes only its own live clipboard memory-release callback. */
#include "runtime/runtime.h"
#include "hal/host_library.h"
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
    CHECK(argc == 2 && !GLoaderState.freeSharedMemory);
    GLoaderState.allocSegment = segment;
    GLoaderState.freeSelector = release;
    char error[256];
    LxHostLibrary *library = lx_host_library_open(argv[1], error, sizeof error);
    CHECK(library && GLoaderState.freeSharedMemory);
    lx_host_library_close(library);
    CHECK(!GLoaderState.freeSharedMemory && !segments[0] && !segments[1]);
    library = lx_host_library_open(argv[1], error, sizeof error);
    CHECK(library && GLoaderState.freeSharedMemory);
    GLoaderState.freeSharedMemory = foreign;
    lx_host_library_close(library);
    CHECK(GLoaderState.freeSharedMemory == foreign && !segments[0] && !segments[1]);
    GLoaderState.freeSharedMemory = NULL;
    return 0;
}
