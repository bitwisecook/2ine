/* Public domain. No rights reserved. Wholly AI-authored. */
/* Both CPU workers enter the same wire callbacks. Float/double arguments and
   pointers are decoded independently of the host ABI, including native i386. */
#include "internal.h"
#include "guest/bindings/opengl.h"

static const GlEntry GlEntries[] = {
#include "os2libs/generated/opengl-entries.h"
};
static LxMmaps Bridges;
static LxExport *Exports;
static void **EntryAddresses;

void lxNativeModuleDeinit(void)
{
    gl_pgl_shutdown();
    if (Bridges.mapping)
        lx_execution_unmap(GLoaderState.execution, Bridges.mapping);
    memset(&Bridges, 0, sizeof Bridges);
    free(Exports);
    free(EntryAddresses);
    Exports = NULL;
    EntryAddresses = NULL;
}
const LxExport *lxNativeModuleInit(uint32 *count)
{
    const char *profile = getenv("TWOINE_OPENGL_ABI");
    int gold = profile && !strcmp(profile, "gold11");
    if (profile && strcmp(profile, "warp") && !gold)
    {
        fprintf(stderr, "2ine OpenGL: TWOINE_OPENGL_ABI must be warp or gold11\n");
        return NULL;
    }
    size_t core_count = sizeof GlEntries / sizeof GlEntries[0];
    size_t total = core_count + GlPglEntryCount;
    Exports = calloc(total, sizeof *Exports);
    EntryAddresses = calloc(total, sizeof *EntryAddresses);
    if (!Exports || !EntryAddresses || !gl_pgl_initialize())
        goto failed;
    Bridges.alias = 0xffff;
    Bridges.mapping =
        lx_execution_map(GLoaderState.execution, 0, 65536, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!Bridges.mapping)
        goto failed;
    Bridges.addr = Bridges.mapped = lx_execution_mapping_host(Bridges.mapping);
    Bridges.guest_base = lx_execution_mapping_address(Bridges.mapping);
    Bridges.size = 65536;
    uint8_t *p = Bridges.addr;
    for (size_t i = 0; i < total; i++)
    {
        const GlEntry *entry = i < core_count ? &GlEntries[i] : &GlPglEntries[i - core_count];
        const LxExecutionApi32 binding = {entry->call, entry->argument_bytes, 0, LX_ABI_STACK32};
        size_t size = lx_execution_emit_bridge32(
            GLoaderState.execution, p, 65536 - (size_t)(p - (uint8_t *)Bridges.addr), &binding);
        if (!size)
            goto failed;
        EntryAddresses[i] = p;
        /* Ordinal zero marks an additional name-only API in this profile. */
        Exports[i] = (LxExport){gold ? entry->gold_ordinal : entry->warp_ordinal, entry->name,
                                &EntryAddresses[i], &Bridges, 1};
        p += size;
    }
    if (!lx_execution_protect(GLoaderState.execution, Bridges.mapping,
                              LX_GUEST_READ | LX_GUEST_EXECUTE))
        goto failed;
    *count = (uint32)total;
    return Exports;
failed:
    lxNativeModuleDeinit();
    return NULL;
}
