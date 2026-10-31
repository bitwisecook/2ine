/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
void *generateMissingTrampoline(const char *m, const char *n)
{
    return lx_execution_missing_entry(GLoaderState.execution, m, n, 32, NULL);
}

void *generateMissingTrampoline16(const char *m, const char *n, const LxExport **out)
{
    LxExecutionBridge bridge;
    void *address = lx_execution_missing_entry(GLoaderState.execution, m, n, 16, &bridge);
    static LxMmaps object;
    static LxExport exported;
    object.mapped = object.addr = bridge.host;
    object.size = bridge.size;
    object.alias = bridge.selector_index;
    object.guest_base = bridge.segment_base;
    exported.addr = address;
    exported.object = &object;
    *out = &exported;
    return address;
}

void *getModuleProcAddrByOrdinal(const LxModule *module, const uint32 ordinal,
                                 const LxExport **_lxexp, const int want16bit, const int native)
{

    const LxExport *lxexp = module->exports;
    for (uint32 i = 0; i < module->num_exports; i++, lxexp++)
    {
        if (lxexp->ordinal == ordinal)
        {
            if (_lxexp)
                *_lxexp = lxexp;
            void *retval = lxexp->addr;
            if (lxexp->indirect) /* Bridge exports store an indirect host entry pointer. */
                retval = *((void **)retval);
            if (getenv("TWOINE_TRACE_FIXUPS"))
                fprintf(stderr, "2INE FIXUP: %s@%u want16=%d native=%d -> %p\n", module->name,
                        (uint)ordinal, want16bit, native, retval);
            return retval;
        }
    }

    if (_lxexp)
        *_lxexp = NULL;

#if 1
    char entry[128];
    snprintf(entry, sizeof(entry), "ordinal #%u", (uint)ordinal);
    return want16bit ? generateMissingTrampoline16(module->name, entry, _lxexp)
                     : generateMissingTrampoline(module->name, entry);
#else
    return NULL;
#endif
}

void *getModuleProcAddrByName(const LxModule *module, const char *name, const LxExport **_lxexp,
                              const int want16bit)
{
    if (getenv("TWOINE_TRACE_FIXUPS"))
        fprintf(stderr, "2INE FIXNAME: %s@'%s' want16=%d\n", module->name, name, want16bit);

    const LxExport *lxexp = module->exports;
    for (uint32 i = 0; i < module->num_exports; i++, lxexp++)
    {
        if (strcmp(lxexp->name, name) == 0)
        {
            if (_lxexp)
                *_lxexp = lxexp;
            void *retval = lxexp->addr;
            if (lxexp->indirect) /* Bridge exports store an indirect host entry pointer. */
                retval = *((void **)retval);
            if (getenv("TWOINE_TRACE_FIXUPS"))
                fprintf(stderr, "2INE FIXNAME: %s@'%s' -> %p\n", module->name, name, retval);
            return retval;
        }
    }

    if (_lxexp)
        *_lxexp = NULL;

#if 1
    return want16bit ? generateMissingTrampoline16(module->name, name, _lxexp)
                     : generateMissingTrampoline(module->name, name);
#else
    return NULL;
#endif
}
