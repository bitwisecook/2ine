/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* Shared process services used by OS/2 library implementations. */
#ifndef TWOINE_RUNTIME_H
#define TWOINE_RUNTIME_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "formats/types.h"
#include "runtime/diagnostics.h"
#include "runtime/process.h"
#include "loader/module.h"
#include "guest/abi.h"
#include "guest/handles.h"
#include "runtime/audio.h"

#define LXTIBSIZE (sizeof(LxTIB) + sizeof(LxTIB2) + sizeof(LxPostTIB))

#define LX_MAX_LDT_SLOTS 8192

typedef struct LxLoaderState
{
    LxExecution *execution;
    uint32 ne_threads_started;
    LxModule *loaded_modules;
    LxModule *main_module;
    uint8 main_tibspace[LXTIBSIZE];
    LxPIB pib;
    LxGuestMapping *guest_pib;
    int using_lx_loader;
    int subprocess;
    int running;
    int trace_native;
    int trace_events;
    char *disks[26];       // mount points, A: through Z: ... NULL if unmounted.
    char *current_dir[26]; // current directory, per-disk, A: through Z: ... NULL if unmounted.
    int current_disk;      // 1==A:\\, 2==B:\\, etc.
    uint32 diskmap;        // 1<<0==drive A mounted, 1<<1==drive B mounted, etc.
    float beep_volume;
    uint16 main_tib_selector;
    uint32 mainstacksize;
    char *libpath;
    uint32 libpathlen;
    uint32 *tlspage;
    uint32 tlsmask; // one bit for each TLS slot in use.
    uint8 tlsallocs
        [32]; // number of TLS slots allocated in one block, indexed by starting block (zero if not the starting block).
    void (*dosExit)(uint32 action, uint32 result);
    uint32 (*writeFile)(uint32 handle, void *buffer, uint32 size, uint32 *actual);
    uint32 (*getResource)(uint32 module, uint32 type, uint32 name, void **output);
    uint32 (*queryResourceSize)(uint32 module, uint32 type, uint32 name, uint32 *size);
    uint32 (*freeResource)(void *resource);
    void (*initOs2Tib)(uint8 *tibspace, void *_topOfStack, const size_t stacklen, const uint32 tid);
    uint16 (*setOs2Tib)(uint8 *tibspace);
    LxTIB *(*getOs2Tib)(void);
    void (*deinitOs2Tib)(const uint16 selector);
    void *(*allocSegment)(uint16 *selector, const int iscode);
    void (*freeSegment)(const uint16 selector);
    int (*createCodeAlias)(const uint16 selDS, uint16 *outselector);
    int (*findSelector)(const uint32 addr, uint16 *outselector, uint16 *outoffset, int iscode);
    void (*freeSelector)(const uint16 selector);
    void *(*convert1616to32)(const uint32 addr1616);
    uint32 (*convert32to1616)(void *addr32);
    LxModule *(*loadModule)(const char *modname);
    void (*freeModule)(LxModule *lxmod);
    char *(*makeUnixPath)(const char *os2path, uint32 *err);
    void __attribute__((noreturn)) (*terminate)(const uint32 exitcode);
    int (*registerAudioGenerator)(LxAudioGeneratorFn fn, void *data, const int singleton);
    void (*lib2ine_shutdown)(void);
    int (*registerStereoAudioGenerator)(LxAudioGeneratorFn fn, void *data, const int singleton);
} LxLoaderState;

typedef const LxExport *(*LxNativeModuleInitEntryPoint)(uint32 *lx_num_exports);
typedef void (*LxNativeModuleDeinitEntryPoint)(void);

extern __attribute__((visibility("default"))) LxLoaderState GLoaderState;

#define LX_NATIVE_CONSTRUCTOR(modname)                                                             \
    void __attribute__((constructor)) lxNativeConstructor_##modname(void)
#define LX_NATIVE_DESTRUCTOR(modname)                                                              \
    void __attribute__((destructor)) lxNativeDestructor_##modname(void)

#endif
