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
        if (ordinal && lxexp->ordinal == ordinal)
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

    /* DOSCALLS publishes the system keyboard, video, NLS and message entries
       at additional ordinals. Their callable implementation lives in the
       owning native module. */
    if (want16bit && module->nativelib && !strcasecmp(module->name, "DOSCALLS"))
    {
        if (ordinal == 576 || ordinal == 424)
        {
            static LxMmaps objects[2];
            static LxExport exports[2];
            unsigned index = ordinal == 424;
            LxMmaps *object = &objects[index];
            LxExport *exported = &exports[index];
            if (!object->addr)
            {
                LxExecutionBridge bridge;
                if (!lx_execution_universal_thunk(GLoaderState.execution, ordinal, &bridge))
                    return generateMissingTrampoline16(module->name, "THK16_UNITHUNK", _lxexp);
                object->mapped = object->addr = lx_execution_memory(
                    GLoaderState.execution, bridge.segment_base, bridge.size, LX_GUEST_READ);
                object->size = bridge.size;
                object->alias = bridge.selector_index;
                object->guest_base = bridge.segment_base;
                *exported =
                    (LxExport){ordinal, ordinal == 424 ? "Dos16SelToFlat" : "THK16_UNITHUNK",
                               bridge.host, object, 0};
            }
            if (_lxexp)
                *_lxexp = exported;
            return exported->addr;
        }
        const char *target = NULL;
        uint32 number = 0;
        if (ordinal >= 745 && ordinal <= 769)
        {
            target = "KBDCALLS";
            number = ordinal - 744;
        }
        else if (ordinal >= 770 && ordinal <= 838)
        {
            target = "VIOCALLS";
            number = ordinal - (ordinal < 808 ? 769 : 768);
        }
        else if (ordinal >= 391 && ordinal <= 394)
        {
            target = "NLS";
            number = ordinal - 390;
        }
        else if (ordinal == 383 || ordinal == 384)
        {
            target = "MSG";
            number = ordinal - 382;
        }
        if (target)
        {
            LxModule *forward = GLoaderState.loadModule(target);
            if (forward)
                return getModuleProcAddrByOrdinal(forward, number, _lxexp, want16bit, native);
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
