/* Public domain. No rights reserved. Wholly AI-authored. */
/* Resource tables own copied bytes, are process-wide, and require no video context. */
#include "os2libs/pmwinx.h"
#include "runtime/runtime.h"
#include "emulator/abi.h"
#include "emulator/handles.h"
#include "host/thread.h"
#include <SDL3/SDL.h>
#include <dlfcn.h>
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
static UCHAR resource[16] = {2, 0, 0xb5, 1, 2, 0, 0x20, 0, 7, 0, 0x13, 0, 'o', 0, 8, 0};
static unsigned released;
static ULONG shared_table;

static uint32 query(uint32 module, uint32 type, uint32 name, uint32 *size)
{
    if (module != 123 || type != 8 || name != 91)
        return 2;
    *size = sizeof resource;
    return 0;
}

static uint32 get(uint32 module, uint32 type, uint32 name, void **data)
{
    uint32 size;
    if (query(module, type, name, &size))
        return 2;
    *data = resource;
    return 0;
}

static uint32 release(void *data)
{
    CHECK(data == resource);
    released++;
    return 0;
}

static void *worker(void *unused)
{
    (void)unused;
    UCHAR copy[12];
    CHECK(CopyAcceleratorTable(shared_table, copy, 2) == 2);
    CHECK(copy[0] == 1 && lx_guest_load_u16(copy + 2) == 0x70);
    ULONG made = CreateAcceleratorTable(copy, 2);
    CHECK(made && DestroyAcceleratorTable(made));
    return NULL;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    GProcessState.queryResourceSize = query;
    GProcessState.getResource = get;
    GProcessState.freeResource = release;
    shared_table = LoadAccelerators(123, (PSZ)(uintptr_t)91);
    CHECK(shared_table && released == 1);
    CHECK(CopyAcceleratorTable(shared_table, NULL, 0) == 2);
    UCHAR guarded[14];
    memset(guarded, 0xa5, sizeof guarded);
    CHECK(CopyAcceleratorTable(shared_table, guarded + 1, 1) == 1);
    CHECK(guarded[0] == 0xa5 && guarded[7] == 0xa5 && guarded[13] == 0xa5);
    CHECK(lx_guest_load_u16(guarded + 3) == 0x70 && lx_guest_load_u16(guarded + 5) == 7);
    resource[8] = 99;
    CHECK(CopyAcceleratorTable(shared_table, guarded + 1, 1) == 1);
    CHECK(lx_guest_load_u16(guarded + 5) == 7);
    LxHostThread *thread = lx_host_thread_create(worker, NULL, NULL);
    CHECK(thread && lx_host_thread_join(thread, NULL) == 0);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    CHECK(DestroyAcceleratorTable(shared_table));
    CHECK(!DestroyAcceleratorTable(shared_table) && GetLastError() == 1403);
    CHECK(CopyAcceleratorTable(shared_table, NULL, 0) == 0 && GetLastError() == 1403);
    CHECK(!LoadAccelerators(123, (PSZ)(uintptr_t)92) && GetLastError() == 1814);
    CHECK(!LoadAccelerators(123, "MISSING") && GetLastError() == 1814);
    ULONG named = LoadAccelerators(123, "#91");
    CHECK(named && DestroyAcceleratorTable(named));
    resource[0] = 3;
    CHECK(!LoadAccelerators(123, (PSZ)(uintptr_t)91) && GetLastError() == 13);
    resource[0] = 2;
    CHECK(!CreateAcceleratorTable(resource, 0) && GetLastError() == 87);
    UCHAR invalid[6] = {0x80};
    CHECK(!CreateAcceleratorTable(invalid, 1) && GetLastError() == 87);
    ULONG retained = LoadAccelerators(123, (PSZ)(uintptr_t)91);
    CHECK(retained);
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK(module);
    void (*shutdown)(void) = dlsym(module, "lxNativeModuleDeinit");
    CHECK(shutdown);
    shutdown();
    CHECK(CopyAcceleratorTable(retained, NULL, 0) == 0);
    retained = LoadAccelerators(123, (PSZ)(uintptr_t)91);
    CHECK(retained && DestroyAcceleratorTable(retained));
    CHECK(dlclose(module) == 0);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    puts("Open32 accelerator resources, cross-thread ownership, bounds and shutdown passed");
    return 0;
}
