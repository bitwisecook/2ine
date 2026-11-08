/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native compatibility checks cover USE16 widths, pipes, process state and resources. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "os2libs/doscalls.h"
#include "os2libs/os2errors.h"
#include "emulator/runtime.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void *segments[32];
static unsigned released;

static void *segment_alloc(uint16 *selector, int executable)
{
    (void)executable;
    for (unsigned i = 1; i < 32; i++)
        if (!segments[i])
        {
            *selector = i;
            return segments[i] = calloc(1, 65536);
        }
    *selector = 0xffff;
    return NULL;
}

static void segment_free(uint16 selector)
{
    CHECK(selector < 32);
    free(segments[selector]);
    segments[selector] = NULL;
    released++;
}

static ULONG host_to_far(void *pointer)
{
    for (unsigned i = 1; i < 32; i++)
        if (segments[i] && (uintptr_t)pointer >= (uintptr_t)segments[i] &&
            (uintptr_t)pointer < (uintptr_t)segments[i] + 65536)
            return ((ULONG)((i << 3) | 7) << 16) | ((uintptr_t)pointer - (uintptr_t)segments[i]);
    return 0;
}

static char *host_path(const char *path, uint32 *error)
{
    *error = NO_ERROR;
    return strdup(path);
}

static LxModule module;

static LxModule *load_module(const char *name)
{
    return !strcasecmp(name, "LegacyFixture") ? &module : NULL;
}

static void free_module(LxModule *value)
{
    CHECK(value == &module);
    CHECK(lx_handle_close(value->handle, LX_HANDLE_MODULE));
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    GProcessState.makeUnixPath = host_path;
    GProcessState.allocSegment = segment_alloc;
    GProcessState.freeSegment = segment_free;
    GProcessState.freeSelector = segment_free;
    GProcessState.convert32to1616 = host_to_far;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(library);
#define LOAD(function)                                                                             \
    __typeof__(function) *call_##function = dlsym(library, #function);                             \
    CHECK(call_##function)
    LOAD(Dos16MakePipe);
    LOAD(Dos16Read);
    LOAD(Dos16Write);
    LOAD(DosClose);
    LOAD(DosOpen);
    LOAD(Dos16QVerify);
    LOAD(Dos16SetVerify);
    LOAD(Dos16SetDateTime);
    LOAD(Dos16GetDateTime);
    LOAD(Dos16SetCp);
    LOAD(Dos16GetCp);
    LOAD(DosQueryCp);
    LOAD(Dos16AllocSeg);
    LOAD(Dos16ReallocSeg);
    LOAD(Dos16SizeSeg);
    LOAD(Dos16FreeSeg);
    LOAD(Dos16LoadModule);
    LOAD(Dos16GetModHandle);
    LOAD(Dos16GetModName);
    LOAD(Dos16GetProcAddr);
    LOAD(Dos16FreeModule);

    struct
    {
        USHORT before, value, after;
    } word = {0xa55a, 0, 0x5aa5};

    CHECK(!call_Dos16SetVerify(1));
    CHECK(!call_Dos16QVerify(&word.value) && word.value == 1);
    CHECK(word.before == 0xa55a && word.after == 0x5aa5);
    CHECK(call_Dos16SetVerify(2) == ERROR_INVALID_PARAMETER);
    CHECK(!call_Dos16QVerify(&word.value) && word.value == 1);
    USHORT reader, writer;
    CHECK(!call_Dos16MakePipe(&reader, &writer, 4096));
    const char payload[] = "pipe bytes";
    CHECK(!call_Dos16Write(writer, (PVOID)payload, sizeof payload, &word.value));
    CHECK(word.value == sizeof payload && word.before == 0xa55a && word.after == 0x5aa5);
    char received[sizeof payload];
    CHECK(!call_Dos16Read(reader, received, sizeof received, &word.value));
    CHECK(word.value == sizeof payload && !memcmp(received, payload, sizeof payload));
    CHECK(!call_DosClose(writer));
    CHECK(!call_Dos16Read(reader, received, sizeof received, &word.value) && !word.value);
    CHECK(!call_DosClose(reader));
    CHECK(!call_Dos16SetVerify(0));
    char path[] = "/tmp/twoine-legacy-files-XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0 && !close(descriptor));
    HFILE files[100];
    unsigned count = 0;
    ULONG action;
    APIRET result;
    while (count < 100 &&
           !(result = call_DosOpen(path, &files[count], &action, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
                                   OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL)))
        count++;
    CHECK(count > 1 && count < 100 && result == ERROR_TOO_MANY_OPEN_FILES);
    HFILE first_slot = files[--count];
    CHECK(!call_DosClose(first_slot));
    reader = 0xaaaa;
    writer = 0xbbbb;
    CHECK(call_Dos16MakePipe(&reader, &writer, 0) == ERROR_TOO_MANY_OPEN_FILES);
    CHECK(reader == 0xaaaa && writer == 0xbbbb);
    HFILE reopened;
    CHECK(!call_DosOpen(path, &reopened, &action, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
                        OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL));
    CHECK(reopened == first_slot);
    CHECK(!call_DosClose(reopened));
    CHECK(!call_DosClose(files[--count]));
    CHECK(!call_Dos16MakePipe(&reader, &writer, 0));
    CHECK(!call_DosClose(reader) && !call_DosClose(writer));
    while (count)
        CHECK(!call_DosClose(files[--count]));
    CHECK(!unlink(path));
    CHECK(!call_Dos16SetCp(850, 0));

    struct
    {
        USHORT values[3], after;
    } cp16 = {{0, 0xaaaa, 0xbbbb}, 0xcccc};

    CHECK(call_Dos16GetCp(3, cp16.values, &word.value) == ERROR_CPLIST_TOO_SMALL);
    CHECK(word.value == 2 && cp16.values[0] == 850 && cp16.values[1] == 0xaaaa);
    CHECK(word.before == 0xa55a && word.after == 0x5aa5);
    CHECK(!call_Dos16GetCp(6, cp16.values, &word.value));
    CHECK(word.value == 6 && cp16.values[1] == 437 && cp16.values[2] == 850 &&
          cp16.after == 0xcccc);

    struct
    {
        ULONG values[3], after;
    } cp32 = {{0, 0xaaaaaaaa, 0xbbbbbbbb}, 0xcccccccc};

    ULONG actual;
    CHECK(call_DosQueryCp(6, cp32.values, &actual) == ERROR_CPLIST_TOO_SMALL);
    CHECK(actual == 4 && cp32.values[0] == 850 && cp32.values[1] == 0xaaaaaaaa);
    CHECK(!call_DosQueryCp(12, cp32.values, &actual));
    CHECK(actual == 12 && cp32.values[1] == 437 && cp32.values[2] == 850 &&
          cp32.after == 0xcccccccc);
    CHECK(call_Dos16SetCp(437, 1) == ERROR_INVALID_PARAMETER);
    CHECK(!call_Dos16SetCp(437, 0));
    time_t host_before = time(NULL);
    DATETIME date = {12, 34, 56, 0, 29, 2, 2024, -120, 0};
    CHECK(!call_Dos16SetDateTime(&date));
    DATETIME queried;
    CHECK(!call_Dos16GetDateTime(&queried));
    CHECK(queried.year == 2024 && queried.month == 2 && queried.day == 29 && queried.hours == 12);
    CHECK(time(NULL) >= host_before && time(NULL) - host_before < 10);
    date.year = 2023;
    CHECK(call_Dos16SetDateTime(&date) == ERROR_TS_DATETIME);
    date.year = 2024;
    date.timezone = -781;
    CHECK(call_Dos16SetDateTime(&date) == ERROR_TS_DATETIME);
    date.timezone = 781;
    CHECK(call_Dos16SetDateTime(&date) == ERROR_TS_DATETIME);
    USHORT selector;
    CHECK(!call_Dos16AllocSeg(17, &selector, 0));
    ULONG size;
    CHECK(!call_Dos16SizeSeg(selector, &size) && size == 17);
    CHECK(!call_Dos16ReallocSeg(0, selector));
    CHECK(!call_Dos16SizeSeg(selector, &size) && size == 65536);
    unsigned before_free = released;
    CHECK(!call_Dos16FreeSeg(selector) && released == before_free + 1);
    CHECK(call_Dos16SizeSeg(selector, &size) == ERROR_INVALID_SEGMENT_NUMBER);
    CHECK(call_Dos16FreeSeg(selector) == ERROR_INVALID_SEGMENT_NUMBER);
    CHECK(!call_Dos16AllocSeg(0, &selector, 0));
    CHECK(!call_Dos16SizeSeg(selector, &size) && size == 65536);
    CHECK(!call_Dos16FreeSeg(selector));
    uint16 slot;
    void *entry = segment_alloc(&slot, 1);
    CHECK(entry);
    LxMmaps object = {0};
    object.addr = entry;
    object.alias = slot;
    LxExport exported = {1, "LegacyEntry", entry, &object, 0};
    strcpy(module.name, "LEGACYFIXTURE");
    module.os2path = "C:\\OS2\\DLL\\LEGACYFIXTURE.DLL";
    module.exports = &exported;
    module.num_exports = 1;
    module.handle = lx_handle_create(LX_HANDLE_MODULE, &module, NULL);
    CHECK(module.handle);
    GProcessState.loaded_modules = &module;
    GProcessState.loadModule = load_module;
    GProcessState.freeModule = free_module;
    char failure[32], name[64];
    CHECK(!call_Dos16LoadModule(failure, sizeof failure, "LegacyFixture", &word.value));
    CHECK(word.value == (USHORT)module.handle && !failure[0]);
    CHECK(word.before == 0xa55a && word.after == 0x5aa5);
    CHECK(!call_Dos16GetModHandle("legacyfixture.dll", &word.value));
    CHECK(!call_Dos16GetModName(word.value, sizeof name, name) && !strcmp(name, module.os2path));
    ULONG procedure = 0;
    CHECK(!call_Dos16GetProcAddr(word.value, 1, &procedure) && procedure == host_to_far(entry));
    CHECK(call_Dos16GetProcAddr(word.value, 999, &procedure) == ERROR_INVALID_ORDINAL);
    CHECK(!call_Dos16FreeModule(word.value));
    CHECK(call_Dos16GetModName(word.value, sizeof name, name) == ERROR_INVALID_HANDLE);
    GProcessState.loaded_modules = NULL;
    segment_free(slot);
    CHECK(!dlclose(library));
    for (unsigned i = 1; i < 32; i++)
        if (segments[i])
            segment_free(i);
    puts("legacy pipes, state, codepage widths, segment lifetime and module IDs passed");
    return 0;
}
