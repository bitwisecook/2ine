/* Derived from original 2ine; zlib-style license. See ../LICENSE.txt. */
/* Flat guest API entries backed by explicit wire marshalling functions. */
#ifndef TWOINE_OS2NATIVE32_H
#define TWOINE_OS2NATIVE32_H
#include "os2libs/os2native.h"
#ifndef LX_NATIVE_MODULE_SHUTDOWN
#define LX_NATIVE_MODULE_SHUTDOWN()
#endif
#define LX_NATIVE_MODULE_32BIT_SUPPORT() static LxMmaps obj32;
#define LX_NATIVE_MODULE_32BIT_API(fn) static void *bridge_address_##fn;
#define LX_NATIVE_MODULE_INIT_32BIT_SUPPORT()                                                      \
    memset(&obj32, 0, sizeof obj32);                                                               \
    obj32.bits = 32;                                                                               \
    obj32.alias = 0xffff;                                                                          \
    obj32.mapping = lx_execution_map(GProcessState.execution, 0, 0x10000, 3, 0);                   \
    if (!obj32.mapping)                                                                            \
        return 0;                                                                                  \
    obj32.addr = obj32.mapped = lx_execution_mapping_host(obj32.mapping);                          \
    obj32.guest_base = lx_execution_mapping_address(obj32.mapping);                                \
    obj32.size = 0x10000;                                                                          \
    uint8 *ptr = obj32.addr;
#define LX_NATIVE_INIT_32BIT_BRIDGE(fn, bytes, pop, abi)                                           \
    {                                                                                              \
        const LxExecutionApi32 api = {bridge32_##fn, bytes, pop, abi};                             \
        bridge_address_##fn = ptr;                                                                 \
        size_t n = lx_execution_emit_bridge32(                                                     \
            GProcessState.execution, ptr, 0x10000 - (size_t)(ptr - (uint8 *)obj32.addr), &api);    \
        if (!n)                                                                                    \
            return 0;                                                                              \
        ptr += n;                                                                                  \
    }
#define LX_NATIVE_MODULE_INIT_32BIT_SUPPORT_END()                                                  \
    if (!lx_execution_protect(GProcessState.execution, obj32.mapping, 5))                          \
        return 0;
#define LX_NATIVE_MODULE_DEINIT_BRIDGED_SUPPORT()                                                  \
    LX_NATIVE_MODULE_SHUTDOWN();                                                                   \
    if (obj32.mapping)                                                                             \
        lx_execution_unmap(GProcessState.execution, obj32.mapping);
/* Mutable data exports share one zero-filled guest object. Import fixups resolve
   them through the execute-checked entry translation, as for DosAllocMem pages. */
#define LX_NATIVE_MODULE_DATA_SUPPORT() static LxMmaps objdata;
#define LX_NATIVE_MODULE_DATA(name) static void *data_address_##name;
#define LX_NATIVE_MODULE_INIT_DATA_SUPPORT(bytes)                                                  \
    memset(&objdata, 0, sizeof objdata);                                                           \
    objdata.bits = 32;                                                                             \
    objdata.alias = 0xffff;                                                                        \
    objdata.size = ((size_t)(bytes) + 0xfff) & ~(size_t)0xfff;                                     \
    objdata.mapping = lx_execution_map(GProcessState.execution, 0, objdata.size,                   \
                                       LX_GUEST_READ | LX_GUEST_WRITE | LX_GUEST_EXECUTE, 0);      \
    if (!objdata.mapping)                                                                          \
        return 0;                                                                                  \
    objdata.addr = objdata.mapped = lx_execution_mapping_host(objdata.mapping);                    \
    objdata.guest_base = lx_execution_mapping_address(objdata.mapping);
#define LX_NATIVE_INIT_DATA(name, offset) data_address_##name = (uint8 *)objdata.addr + (offset);
#define LX_NATIVE_MODULE_DEINIT_DATA_SUPPORT()                                                     \
    if (objdata.mapping)                                                                           \
        lx_execution_unmap(GProcessState.execution, objdata.mapping);
#define LX_NATIVE_EXPORT_DATA(name, ord) {ord, #name, &data_address_##name, &objdata, 1}
#define LX_NATIVE_EXPORT32_BRIDGED(fn, ord) {ord, #fn, &bridge_address_##fn, &obj32, 1}
#define LX_NATIVE_EXPORT32_BRIDGED_DIFFERENT_NAME(fn, name, ord)                                   \
    {ord, name, &bridge_address_##fn, &obj32, 1}
#if TWOINE_PORTABLE_LOADER
#define LX_NATIVE_MODULE_DEINIT_32BIT_SUPPORT() LX_NATIVE_MODULE_DEINIT_BRIDGED_SUPPORT()
#define LX_NATIVE_INIT_32BIT_MODULE(module)                                                        \
    if (!init32_##module())                                                                        \
        return 0;
#define LX_NATIVE_EXPORT32(fn, ord) {ord, #fn, &bridge_address_##fn, &obj32, 1}
#define LX_NATIVE_EXPORT32_DIFFERENT_NAME(fn, name, ord)                                           \
    {ord, name, &bridge_address_##fn, &obj32, 1}
#else
#define LX_NATIVE_MODULE_DEINIT_32BIT_SUPPORT() LX_NATIVE_MODULE_SHUTDOWN()
#define LX_NATIVE_INIT_32BIT_MODULE(module)
#define LX_NATIVE_EXPORT32(fn, ord) LX_NATIVE_EXPORT(fn, ord)
#define LX_NATIVE_EXPORT32_DIFFERENT_NAME(fn, name, ord)                                           \
    LX_NATIVE_EXPORT_DIFFERENT_NAME(fn, name, ord)
#endif
#endif
