/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_LOADER_MODULE_H
#define TWOINE_LOADER_MODULE_H
#include <stddef.h>
#include "formats/lx.h"
#include "formats/ne.h"
#include "runtime/execution.h"
#include "hal/host_library.h"
typedef struct LxMmaps
{
    LxGuestMapping *mapping;
    uint32 guest_base;
    void *mapped;
    void *addr;
    size_t size;
    int prot;
    uint16 alias; // 16:16 alias, if one.
} LxMmaps;

typedef struct LxExport
{
    uint32 ordinal;
    const char *name; // can be NULL
    void *addr;
    const LxMmaps *object;
    int indirect;
} LxExport;

struct LxModule;
typedef struct LxModule LxModule;
struct LxModule
{
    uint32 refcount;
    uint32 handle;
    int is_lx; // 1 if an LX module, 0 if NE.
    int entry16;
    union
    {
        LxHeader lx;
        NeHeader ne;
    } header;

    char name[256];

    LxModule **dependencies;
    uint32 num_dependencies;

    LxMmaps *mmaps;
    uint32 num_mmaps;
    LxResourceTableEntry *resources;
    uint32 num_resources;

    const LxExport *exports;
    uint32 num_exports;
    LxHostLibrary *nativelib;
    uint32 eip;
    uint32 esp;
    uint16 autodatasize; // only used for NE binaries.
    int initialized;
    char *os2path;
    uint32 signal_exception_focus_count;
    LxModule *prev;
    LxModule *next;
};

#endif
