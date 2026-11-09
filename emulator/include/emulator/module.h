/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef TWOINE_EMULATOR_MODULE_H
#define TWOINE_EMULATOR_MODULE_H
#include <stddef.h>
#include "formats/executable/lx.h"
#include "formats/executable/ne.h"
#include "emulator/execution.h"
#include "host/library.h"

typedef struct LxMmaps
{
    LxGuestMapping *mapping;
    uint32 guest_base;
    void *mapped;
    void *addr;
    size_t size;
    int prot;
    uint16 alias;  // 16:16 alias selector, or LX_NO_ALIAS.
    unsigned bits; // Default operand/address width of this object.
} LxMmaps;

/* LxMmaps.alias value for an object without a 16:16 alias selector. */
enum
{
    LX_NO_ALIAS = 0xFFFF
};

typedef struct LxExport
{
    uint32 ordinal;
    const char *name; // can be NULL
    void *addr;
    const LxMmaps *object;
    int indirect;
} LxExport;

/* Absolute exports carry an immediate value rather than a mapped guest entry. */
enum
{
    LX_EXPORT_DIRECT = 0,
    LX_EXPORT_INDIRECT = 1,
    LX_EXPORT_ABSOLUTE = 2,
    LX_EXPORT_FORWARDER = 3
};

typedef struct LxModule LxModule;

typedef struct LxForwarder
{
    LxModule *owner;
    uint32 module_ordinal;
    uint32 ordinal;
    char module_name[128];
    char name[256];
} LxForwarder;

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
    /* A native module with forwarder exports uses this heap copy of its table,
       with the forwarders resolved; otherwise NULL. */
    LxExport *native_exports;
    uint32 eip;
    uint32 esp;
    uint16 autodatasize; // only used for NE binaries.
    int initialized;
    int listed;
    int loading;
    int load_failed;
    int init_started;
    uint32 incoming_references;
    int reachable;
    char *os2path;
    uint32 signal_exception_focus_count;
    LxModule *prev;
    LxModule *next;
    uint64 initialization_order;
};

#endif
