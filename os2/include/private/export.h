/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_OS2NATIVE_H_
#define _INCL_OS2NATIVE_H_

#ifndef LX_LEGACY
#define LX_LEGACY 1
#endif

#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 199309
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "host/thread.h"
#include <errno.h>

#include "os2/include/os2def.h"
#include "os2/include/bseerr.h"
#include "emulator/runtime.h"
#include "emulator/module.h"

#if 1
#define TRACE_NATIVE(...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (GProcessState.trace_native)                                                            \
        {                                                                                          \
            fprintf(stderr, "2INE TRACE [%lu]: ", (unsigned long)lx_host_thread_id());             \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, ";\n");                                                                \
        }                                                                                          \
    } while (0)
#else
#define TRACE_NATIVE(...)                                                                          \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

#if 1
#define TRACE_EVENT(...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (GProcessState.trace_events)                                                            \
        {                                                                                          \
            fprintf(stderr, "2INE TRACE [%lu]: ", (unsigned long)lx_host_thread_id());             \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while (0)
#else
#define TRACE_EVENT(...)                                                                           \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

OS2EXPORT const LxExport *lxNativeModuleInit(uint32 *lx_num_exports);
OS2EXPORT void lxNativeModuleDeinit(void);

#define LX_NATIVE_MODULE_DEINIT(deinitcode)                                                        \
    void lxNativeModuleDeinit(void)                                                                \
    {                                                                                              \
        deinitcode;                                                                                \
    }

#define LX_NATIVE_MODULE_INIT(initcode)                                                            \
    const LxExport *lxNativeModuleInit(uint32 *lx_num_exports)                                     \
    {                                                                                              \
        initcode;                                                                                  \
        static const LxExport lx_native_exports[] = {

#define LX_NATIVE_EXPORT(fn, ord) {ord, #fn, fn, NULL}
#define LX_NATIVE_EXPORT_DIFFERENT_NAME(fn, fnname, ord) {ord, fnname, fn, NULL}

/* A forwarder export resolves to another module's entry when it is imported,
   as an LX forwarder does. A zero target ordinal selects the entry by name.
   The loader loads every target module with the forwarding module. */
#define LX_NATIVE_FORWARDER(id, module, target_ordinal, target_name)                               \
    static const LxForwarder id = {NULL, 0, target_ordinal, module, target_name}
#define LX_NATIVE_EXPORT_FORWARDER(id, name, ord)                                                  \
    {ord, name, (void *)&id, NULL, LX_EXPORT_FORWARDER}

#define LX_NATIVE_MODULE_INIT_END()                                                                \
    }                                                                                              \
    ;                                                                                              \
    *lx_num_exports = sizeof(lx_native_exports) / sizeof(lx_native_exports[0]);                    \
    return lx_native_exports;                                                                      \
    }

/* TIB2 follows TIB contiguously in guest memory. */
#define LX_GETTIB() (GProcessState.getOs2Tib())
#define LX_GETTIB2() ((LxGuestTib2 *)(LX_GETTIB() + 1))

#endif
