/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_OS2NATIVE16_H_
#define _INCL_OS2NATIVE16_H_

#include "os2native.h"
#include "../guest/abi.h"
#include "../guest/arguments.h"

// !!! FIXME: _lots_ of macro salsa in here.

#define LX_NATIVE_MODULE_16BIT_SUPPORT() \
    static LxMmaps obj16;

// These are the 16-bit entry points, which are exported to the LX loader
//  (not when linking directly to this shared library).
#define LX_NATIVE_MODULE_16BIT_API(fn) \
    static void *fn##16 = NULL;

#define LX_NATIVE_MODULE_16BIT_SUPPORT_END()

#define LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT() \
    if (obj16.alias != 0xFFFF) GLoaderState.freeSelector(obj16.alias); \
    if (obj16.mapping) lx_execution_unmap(GLoaderState.execution, obj16.mapping); \
    memset(&obj16, 0, sizeof obj16); \
    obj16.alias = 0xFFFF;

#define LX_NATIVE_MODULE_INIT_16BIT_SUPPORT() \
    memset(&obj16, 0, sizeof obj16); \
    obj16.alias = 0xFFFF; \
    const size_t vsize = 0x20000; \
    obj16.mapping = lx_execution_map(GLoaderState.execution, 0, vsize, \
                                    LX_GUEST_READ | LX_GUEST_WRITE, 0); \
    if (!obj16.mapping) return 0; \
    obj16.mapped = lx_execution_mapping_host(obj16.mapping); \
    obj16.size = vsize; \
    const uint32 guestbase = lx_execution_mapping_address(obj16.mapping); \
    const uint32 aligned = (guestbase + 0xFFFF) & ~0xFFFFu; \
    void *mmapaddr = (uint8 *) obj16.mapped + (aligned - guestbase); \
    obj16.addr = mmapaddr; obj16.guest_base = aligned; \
    uint16 offset = 0; \
    if (!GLoaderState.findSelector(aligned, &obj16.alias, &offset, 1)) { \
        lx_execution_unmap(GLoaderState.execution, obj16.mapping); \
        obj16.mapping = NULL; \
        return 0; \
    } \
    assert(offset == 0); \
    uint8 *ptr = (uint8 *) mmapaddr;

#define LX_NATIVE_INIT_16BIT_BRIDGE(fn, argbytes) { \
    fn##16 = ptr; \
    size_t emitted = lx_execution_emit_bridge16(GLoaderState.execution, ptr, \
        0x10000 - (size_t) (ptr - (uint8 *) mmapaddr), (uint8 *) mmapaddr, \
        obj16.alias, bridge16to32_##fn, argbytes); \
    if (!emitted) return 0; \
    ptr += emitted; \
}

#define LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END() { \
    assert((size_t) (ptr - (uint8 *) mmapaddr) < 0x10000); \
    if (!lx_execution_protect(GLoaderState.execution, obj16.mapping, \
                              LX_GUEST_READ | LX_GUEST_EXECUTE)) { \
        LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT(); \
        return 0; \
    } \
}

#define LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(typ, var) \
    const typ var = (typ) (sizeof(typ) == 2 ? lx_guest_load_u16(args) : lx_guest_load_u32(args)); args += sizeof (typ)

#define LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(typ, var) \
    const uint32 var##_guest = lx_guest_load_u32(args); \
    typ var = (typ) GLoaderState.convert1616to32(var##_guest); args += sizeof (uint32)

#define LX_NATIVE_EXPORT16(fn, ord) { ord, #fn, &fn##16, &obj16, 1 }
#define LX_NATIVE_EXPORT16_DIFFERENT_NAME(fn, fnname, ord) { ord, fnname, &fn##16, &obj16, 1 }

#endif

// end of os2native16.h ...

