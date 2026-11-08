/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/loader/internal.h"

void *generateMissingTrampoline(const char *m, const char *n)
{
    return lx_execution_missing_entry(GProcessState.execution, m, n, 32, NULL);
}

void *generateMissingTrampoline16(const char *m, const char *n, const LxExport **out)
{
    LxExecutionBridge bridge;
    void *address = lx_execution_missing_entry(GProcessState.execution, m, n, 16, &bridge);
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

static void *resolveModuleOrdinal(const LxModule *module, uint32 ordinal, const LxExport **exported,
                                  int want16bit, int native, int missing);

static const LxExport *resolveExport(const LxExport *entry)
{
    const LxExport *visited[1024];
    unsigned count = 0;
    while (entry && entry->indirect == LX_EXPORT_FORWARDER)
    {
        if (count == sizeof visited / sizeof *visited)
        {
            fprintf(stderr, "LX forwarder chain exceeds 1024 entries\n");
            return NULL;
        }
        for (unsigned i = 0; i < count; i++)
            if (visited[i] == entry)
            {
                fprintf(stderr, "Circular LX forwarder chain\n");
                return NULL;
            }
        visited[count++] = entry;
        const LxForwarder *forward = entry->addr;
        /* Native forwarders name only their target module. */
        const LxModule *target =
            forward->owner ? forward->owner->dependencies[forward->module_ordinal - 1] : NULL;
        /* Circular DLL imports publish exports before filling every dependency slot. */
        if (!target)
            for (target = GProcessState.loaded_modules; target; target = target->next)
                if (!target->load_failed && !strcasecmp(target->name, forward->module_name))
                    break;
        if (!target)
            return NULL;
        entry = NULL;
        for (uint32 i = 0; i < target->num_exports; i++)
        {
            const LxExport *candidate = &target->exports[i];
            if (forward->ordinal ? candidate->ordinal == forward->ordinal
                                 : candidate->name && !strcmp(candidate->name, forward->name))
            {
                entry = candidate;
                break;
            }
        }
        if (!entry && forward->ordinal && target->nativelib)
            resolveModuleOrdinal(target, forward->ordinal, &entry, 1, 0, 0);
    }
    return entry;
}

int resolveLxForwarders(LxModule *module)
{
    for (uint32 i = 0; i < module->num_exports; i++)
    {
        LxExport *entry = (LxExport *)&module->exports[i];
        if (entry->indirect != LX_EXPORT_FORWARDER)
            continue;
        const LxExport *target = resolveExport(entry);
        if (!target)
        {
            fprintf(stderr, "Cannot resolve LX forwarder %s@%u\n", module->name,
                    (unsigned)entry->ordinal);
            return 0;
        }
        void *forward = entry->addr;
        entry->addr = target->addr;
        entry->object = target->object;
        entry->indirect = target->indirect;
        free(forward);
    }
    return 1;
}

static void *resolveModuleOrdinal(const LxModule *module, const uint32 ordinal,
                                  const LxExport **_lxexp, const int want16bit, const int native,
                                  const int missing)
{

    if (module->nativelib && !strcasecmp(module->name, "DOSCALLS") && ordinal == 295)
    {
        static LxExport exported;
        exported =
            (LxExport){295, "Thk32R3DS",
                       (void *)(uintptr_t)lx_execution_flat_data_selector(GProcessState.execution),
                       NULL, LX_EXPORT_ABSOLUTE};
        if (_lxexp)
            *_lxexp = &exported;
        return exported.addr;
    }
    const LxExport *lxexp = module->exports;
    for (uint32 i = 0; i < module->num_exports; i++, lxexp++)
    {
        if (ordinal && lxexp->ordinal == ordinal)
        {
            lxexp = resolveExport(lxexp);
            if (_lxexp)
                *_lxexp = lxexp;
            if (!lxexp)
                return NULL;
            void *retval = lxexp->addr;
            if (lxexp->indirect ==
                LX_EXPORT_INDIRECT) /* Bridge exports store an indirect host entry pointer. */
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
        if (ordinal == LX_ORDINAL_THK16_UNITHUNK || ordinal == LX_ORDINAL_DOS16SELTOFLAT)
        {
            static LxMmaps objects[2];
            static LxExport exports[2];
            unsigned index = ordinal == LX_ORDINAL_DOS16SELTOFLAT;
            LxMmaps *object = &objects[index];
            LxExport *exported = &exports[index];
            if (!object->addr)
            {
                LxExecutionBridge bridge;
                if (!lx_execution_universal_thunk(GProcessState.execution, ordinal, &bridge))
                    return generateMissingTrampoline16(module->name, "THK16_UNITHUNK", _lxexp);
                object->mapped = object->addr = lx_execution_memory(
                    GProcessState.execution, bridge.segment_base, bridge.size, LX_GUEST_READ);
                object->size = bridge.size;
                object->alias = bridge.selector_index;
                object->guest_base = bridge.segment_base;
                *exported = (LxExport){ordinal,
                                       ordinal == LX_ORDINAL_DOS16SELTOFLAT ? "Dos16SelToFlat"
                                                                            : "THK16_UNITHUNK",
                                       bridge.host, object, 0};
            }
            if (_lxexp)
                *_lxexp = exported;
            return exported->addr;
        }
        const char *target = NULL;
        uint32 number = 0;
        if (ordinal == 674 || ordinal == 675 || ordinal == 681)
        {
            target = "QUECALLS";
            number = ordinal - 673;
        }
        else if (ordinal == 693 || ordinal == 699 || ordinal == 702)
        {
            target = "SESMGR";
            number = ordinal - 685;
        }
        else if (ordinal >= 745 && ordinal <= 769)
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
            LxModule *forward = GProcessState.loadModule(target);
            if (forward)
                return resolveModuleOrdinal(forward, number, _lxexp, want16bit, native, missing);
        }
    }

    if (_lxexp)
        *_lxexp = NULL;
    if (!missing)
        return NULL;

#if 1
    char entry[128];
    snprintf(entry, sizeof(entry), "ordinal #%u", (uint)ordinal);
    return want16bit ? generateMissingTrampoline16(module->name, entry, _lxexp)
                     : generateMissingTrampoline(module->name, entry);
#else
    return NULL;
#endif
}

void *getModuleProcAddrByOrdinal(const LxModule *module, const uint32 ordinal,
                                 const LxExport **exported, const int want16bit, const int native)
{
    return resolveModuleOrdinal(module, ordinal, exported, want16bit, native, 1);
}

const LxExport *findModuleOrdinal(const LxModule *module, uint32 ordinal)
{
    const LxExport *exported = NULL;
    resolveModuleOrdinal(module, ordinal, &exported, 1, 0, 0);
    return exported;
}

void *getModuleProcAddrByName(const LxModule *module, const char *name, const LxExport **_lxexp,
                              const int want16bit)
{
    if (getenv("TWOINE_TRACE_FIXUPS"))
        fprintf(stderr, "2INE FIXNAME: %s@'%s' want16=%d\n", module->name, name, want16bit);

    const LxExport *lxexp = module->exports;
    for (uint32 i = 0; i < module->num_exports; i++, lxexp++)
    {
        if (lxexp->name && strcmp(lxexp->name, name) == 0)
        {
            lxexp = resolveExport(lxexp);
            if (_lxexp)
                *_lxexp = lxexp;
            if (!lxexp)
                return NULL;
            void *retval = lxexp->addr;
            if (lxexp->indirect ==
                LX_EXPORT_INDIRECT) /* Bridge exports store an indirect host entry pointer. */
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
