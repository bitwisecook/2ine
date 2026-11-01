/* Public domain. No rights reserved. Wholly AI-authored. */
/* File-open failures release slots; FIL_STANDARD prefixes respect caller bounds. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "os2libs/doscalls.h"
#include "os2libs/os2errors.h"
#include "runtime/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
static __typeof__(DosOpen) *call_open;
static __typeof__(DosClose) *call_close;
static __typeof__(DosQueryFileInfo) *call_file_info;
static __typeof__(DosQueryPathInfo) *call_path_info;
static __typeof__(DosSetPriority) *call_priority;
#define DosOpen call_open
#define DosClose call_close
#define DosQueryFileInfo call_file_info
#define DosQueryPathInfo call_path_info
#define DosSetPriority call_priority
static void *segments[2];
static unsigned segment_count;
static void *segment_alloc(uint16 *selector, int executable)
{
    (void)executable;
    if (segment_count == 2)
        return NULL;
    *selector = (uint16)segment_count;
    return segments[segment_count++] = calloc(1, 65536);
}
static void segment_free(uint16 selector)
{
    if (selector < 2)
    {
        free(segments[selector]);
        segments[selector] = NULL;
    }
}
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static char *host_path(const char *name, uint32 *error)
{
    *error = NO_ERROR;
    return strdup(name);
}
static void canaries(const unsigned char *buffer, size_t written)
{
    CHECK(buffer[0] == 0xcc);
    for (size_t i = written + 1; i < 32; i++)
        CHECK(buffer[i] == 0xcc);
}
static void file_status(char *path, HFILE handle)
{
    unsigned char buffer[32];
    FILESTATUS3 normal;
    CHECK(sizeof normal == 24);
    for (int by_handle = 0; by_handle <= 1; by_handle++)
    {
        memset(buffer, 0xcc, sizeof buffer);
        APIRET result = by_handle ? DosQueryFileInfo(handle, FIL_STANDARD, buffer + 1, 21)
                                  : DosQueryPathInfo(path, FIL_STANDARD, buffer + 1, 21);
        CHECK(result == ERROR_BUFFER_OVERFLOW);
        canaries(buffer, 0);
        for (unsigned size = 22; size <= 24; size++)
        {
            memset(buffer, 0xcc, sizeof buffer);
            result = by_handle ? DosQueryFileInfo(handle, FIL_STANDARD, buffer + 1, size)
                               : DosQueryPathInfo(path, FIL_STANDARD, buffer + 1, size);
            CHECK(!result);
            canaries(buffer, size);
            memset(&normal, 0, sizeof normal);
            memcpy(&normal, buffer + 1, size);
            CHECK(normal.cbFile == 5 && !(normal.attrFile & FILE_DIRECTORY));
        }
        memset(buffer, 0xcc, sizeof buffer);
        result = by_handle ? DosQueryFileInfo(handle, FIL_STANDARD, buffer + 1, 30)
                           : DosQueryPathInfo(path, FIL_STANDARD, buffer + 1, 30);
        CHECK(!result);
        canaries(buffer, sizeof normal);
    }
}
static void priority(void)
{
    LxTIB *thread = GLoaderState.getOs2Tib();
    LxTIB2 *details = (LxTIB2 *)(thread + 1);
    uint32 saved_id = details->tib2_ultid, saved_priority = details->tib2_ulpri;
    details->tib2_ultid = 7;
    details->tib2_ulpri = 0x205;
    CHECK(DosSetPriority(3, 2, 0, 0) == ERROR_INVALID_SCOPE);
    CHECK(DosSetPriority(2, 5, 0, 0) == ERROR_INVALID_PCLASS);
    CHECK(DosSetPriority(2, 2, -32, 0) == ERROR_INVALID_PDELTA);
    CHECK(DosSetPriority(2, 2, 32, 0) == ERROR_INVALID_PDELTA);
    CHECK(DosSetPriority(2, 2, 0, 8) == ERROR_INVALID_PROCID);
    CHECK(details->tib2_ulpri == 0x205);
    CHECK(!DosSetPriority(2, 0, 3, 0) && details->tib2_ulpri == 0x208);
    CHECK(!DosSetPriority(2, 0, -2, 7) && details->tib2_ulpri == 0x206);
    CHECK(!DosSetPriority(2, 0, 31, 0) && details->tib2_ulpri == 0x21f);
    CHECK(!DosSetPriority(2, 0, -31, 0) && details->tib2_ulpri == 0x200);
    CHECK(!DosSetPriority(2, 0, -31, 0) && details->tib2_ulpri == 0x200);
    CHECK(!DosSetPriority(2, 3, 0, 7) && details->tib2_ulpri == 0x300);
    details->tib2_ultid = saved_id;
    details->tib2_ulpri = saved_priority;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    GLoaderState.makeUnixPath = host_path;
    GLoaderState.allocSegment = segment_alloc;
    GLoaderState.freeSelector = segment_free;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
        fprintf(stderr, "%s\n", dlerror());
    CHECK(library);
    call_open = dlsym(library, "DosOpen");
    call_close = dlsym(library, "DosClose");
    call_file_info = dlsym(library, "DosQueryFileInfo");
    call_path_info = dlsym(library, "DosQueryPathInfo");
    call_priority = dlsym(library, "DosSetPriority");
    CHECK(call_open && call_close && call_file_info && call_path_info && call_priority);
    char directory[] = "/tmp/twoine-files-XXXXXX", path[256];
    CHECK(mkdtemp(directory));
    CHECK(snprintf(path, sizeof path, "%s/sample", directory) > 0);
    FILE *stream = fopen(path, "wb");
    CHECK(stream && fwrite("hello", 1, 5, stream) == 5 && !fclose(stream));
    ULONG action = 0xdeadbeef;
    HFILE handle = 0xdeadbeef;
    for (unsigned i = 0; i < 40; i++)
    {
        CHECK(DosOpen("", &handle, &action, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
                      OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL) == ERROR_INVALID_NAME);
        CHECK(handle == 0xdeadbeef && action == 0xdeadbeef);
        CHECK(DosOpen(directory, &handle, &action, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
                      OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL) == ERROR_ACCESS_DENIED);
        CHECK(handle == 0xdeadbeef && action == 0xdeadbeef);
    }
    CHECK(!DosOpen(path, &handle, &action, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
                   OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL));
    CHECK(handle == 3 && action == FILE_EXISTED);
    file_status(path, handle);
    CHECK(!DosClose(handle));
    priority();
    CHECK(!unlink(path) && !rmdir(directory));
    CHECK(!dlclose(library));
    CHECK(!segments[0] && !segments[1]);
    puts("file open, bounded status and self-thread priority checks passed");
    return 0;
}
