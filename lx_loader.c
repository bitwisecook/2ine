/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include "runtime/host_fs.h"

#include "lib2ine.h"
#include "runtime/execution.h"
#include "runtime/host.h"

static int sanityCheckLxModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(LxHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const LxHeader *lx = (const LxHeader *)exe;
    if ((lx->byte_order != 0) || (lx->word_order != 0))
    {
        fprintf(stderr, "Module is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != 0)
    {
        fprintf(stderr, "Module is unknown LX version (%u)\n", (uint)lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > 3)
    { // 1==286, 2==386, 3==486
        fprintf(stderr, "Module needs unknown CPU type (%u)\n", (uint)lx->cpu_type);
        return 0;
    }

    if (lx->os_type != 1)
    { // 1==OS/2, others: dos4, windows, win386, unknown.
        fprintf(stderr, "Module needs unknown OS type (%u)\n", (uint)lx->os_type);
        return 0;
    }

    if (lx->page_size != 4096)
    {
        fprintf(stderr, "Module page size isn't 4096 (%u)\n", (uint)lx->page_size);
        return 0;
    }

    if (lx->module_flags & 0x2000)
    {
        fprintf(stderr, "Module is flagged as not-loadable\n");
        return 0;
    }

    return 1;
}

static int sanityCheckNeModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof(NeHeader) >= exelen)
    {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const NeHeader *ne = (const NeHeader *)exe;
    if (ne->exe_type > 1)
    {
        fprintf(stderr, "Not an OS/2 NE module file (exe_type is %d, not 1)\n", (int)ne->exe_type);
        return 0;
    }

    return 1;
}

static int sanityCheckModule(uint8 **_exe, uint32 *_exelen, int *_is_lx)
{
    if (*_exelen < 62)
    {
        fprintf(stderr, "not an OS/2 module\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *)(*_exe + 0x3C));

    *_exe += header_offset;
    *_exelen -= header_offset;

    const uint8 *magic = *_exe;

    if ((magic[0] == 'L') && (magic[1] == 'X'))
    {
        *_is_lx = 1;
        return sanityCheckLxModule(*_exe, *_exelen);
    }
    else if ((magic[0] == 'N') && (magic[1] == 'E'))
    {
        *_is_lx = 0;
        return sanityCheckNeModule(*_exe, *_exelen);
    }

    fprintf(stderr, "not an OS/2 module\n");
    return 0;
}

static char *makeOS2Path(const char *fname)
{
    char *full = lx_host_path_canonical(fname);
    if (!full)
        return NULL;

    char *retval = (char *)malloc(strlen(full) + 3);
    if (!retval)
        return NULL;

    retval[0] = 'C';
    retval[1] = ':';
    strcpy(retval + 2, full);
    free(full);
    for (char *ptr = retval + 2; *ptr; ptr++)
    {
        if (*ptr == '/')
            *ptr = '\\';
    }

    return retval;
}

static int locateOneElement(char *buf)
{
    if (lx_host_path_exists(buf))
        return 1;

    LxHostDirectory *directory;
    char *ptr = strrchr(buf, '/');
    if (!ptr)
    {
        directory = lx_host_directory_open(".", NULL);
        ptr = buf;
    }
    else if (ptr == buf)
    {
        directory = lx_host_directory_open("/", NULL);
        ptr++;
    }
    else
    {
        *ptr = 0;
        directory = lx_host_directory_open(buf, NULL);
        *ptr++ = '/';
    }
    if (!directory)
        return 0;
    const char *name;
    while (lx_host_directory_read(directory, &name, NULL, NULL) == 1)
    {
        if (!strcasecmp(name, ptr))
        {
            strcpy(ptr, name);
            lx_host_directory_close(directory);
            return 1;
        }
    }
    lx_host_directory_close(directory);
    return 0;
}

static int locatePathCaseInsensitive(char *buf)
{
    int rc;
    char *ptr = buf;

    if (*ptr == '\0')
        return 0;

    if (lx_host_path_exists(buf))
        return 0;

    while ((ptr = strchr(ptr + 1, '/')) != NULL)
    {
        *ptr = '\0';
        rc = locateOneElement(buf);
        *ptr = '/';
        if (!rc)
            return -2;
    }

    return locateOneElement(buf) ? 0 : -1;
}

static char *lxMakeUnixPath(const char *os2path, uint32 *err)
{
    const char *mountpoint = NULL;
    const char *cwd = NULL;
    const char *fallback_cwd = NULL;
    const char *rootrel = NULL;
    int driveidx = 0;

    FIXME("map devices in 2ine.cfg");
    if ((strcasecmp(os2path, "NUL") == 0) || (strcasecmp(os2path, "\\DEV\\NUL") == 0))
        os2path = "/dev/null";

    else
    {
        char drive = os2path[0];
        if ((drive >= 'a') && (drive <= 'z'))
        {
            drive += 'A' - 'a';
        }

        if (((drive >= 'A') && (drive <= 'Z')) && (os2path[1] == ':'))
        {
            os2path += 2;
        }
        else
        {
            drive = (GLoaderState.current_disk - 1) + 'A';
        }

        driveidx = drive - 'A';
        mountpoint = GLoaderState.disks[driveidx];
        if (!mountpoint)
        {
            *err = 26;
            return NULL;
        }

        fallback_cwd = GLoaderState.current_dir[driveidx];
        if ((*os2path != '\\') && (*os2path != '/'))
        {
            cwd = fallback_cwd;
        }
        else
        {
            rootrel = os2path + 1;
        }
    }

    const size_t len =
        (mountpoint ? strlen(mountpoint) : 0) + (cwd ? strlen(cwd) : 0) + strlen(os2path) + 3;
    char *retval = (char *)malloc(len);
    if (!retval)
    {
        *err = 8;
        return NULL;
    }

    snprintf(retval, len, "%s%s%s%s%s", mountpoint ? mountpoint : "", cwd ? "/" : "",
             cwd ? cwd : "", ((!*os2path) || (*os2path == '/')) ? "" : "/", os2path);

    for (char *ptr = strchr(retval, '\\'); ptr; ptr = strchr(ptr + 1, '\\'))
        *ptr = '/';

    FIXME("this could be more efficient");
    char *ptr = retval;
    while ((ptr = strstr(ptr, "//")) != NULL)
    {
        memmove(ptr, ptr + 1, strlen(ptr));
    }

    locatePathCaseInsensitive(retval);

    /* Microsoft linker response files may use a root-relative name for a cwd file.
   Prefer the drive-root path, then try the current directory. */
    if (rootrel && (!lx_host_path_exists(retval)))
    {
        char *roots = strdup(rootrel);
        size_t rootslen = roots ? strlen(roots) : 0;
        while (rootslen && ((roots[rootslen - 1] == '\\') || (roots[rootslen - 1] == '/')))
        {
            roots[--rootslen] = '\0';
        }
        const char *dir = fallback_cwd;
        const size_t altlen = (mountpoint ? strlen(mountpoint) : 0) + (dir ? strlen(dir) : 0) +
                              (roots ? strlen(roots) : 0) + 4;
        char *alt = (char *)malloc(altlen);
        if (alt && roots)
        {
            snprintf(alt, altlen, "%s%s%s%s%s", mountpoint ? mountpoint : "", dir ? "/" : "",
                     dir ? dir : "", *roots ? "/" : "", roots);
            for (char *p = strchr(alt, '\\'); p; p = strchr(p + 1, '\\'))
                *p = '/';
            char *pp = alt;
            while ((pp = strstr(pp, "//")) != NULL)
                memmove(pp, pp + 1, strlen(pp));
            locatePathCaseInsensitive(alt);
            if (lx_host_path_exists(alt))
            {
                free(retval);
                retval = alt;
                alt = NULL;
            }
            free(alt);
        }
        free(roots);
    }

    return retval;
}

/* EXEPACK2 overlap expands newly written bytes; memmove would preserve stale input. */
static void linearmove(uint8 *dst, const uint8 *src, uint8 len)
{
    while (len--)
    {
        *dst = *src;
        dst++;
        src++;
    }
}

/* this algorithm is from lxlite 138u. */
static int decompressExePack2(uint8 *dst, const uint32 dstlen, const uint8 *src,
                              const uint32 srclen)
{
    sint32 sOf = 0;
    sint32 dOf = 0;
    sint32 bOf = 0;
    uint8 b1 = 0;
    uint8 b2 = 0;

#define SRCAVAIL(n) ((sOf + (n)) <= srclen)
#define DSTAVAIL(n) ((dOf + (n)) <= dstlen)

    do
    {
        if (!SRCAVAIL(1))
            break;

        b1 = src[sOf];
        switch (b1 & 3)
        {
            case 0:
                if (b1 == 0)
                {
                    if (SRCAVAIL(2))
                    {
                        if (src[sOf + 1] == 0)
                        {
                            sOf += 2;
                            break;
                        }
                        else if (SRCAVAIL(3) && DSTAVAIL(src[sOf + 1]))
                        {
                            memset(dst + dOf, src[sOf + 2], src[sOf + 1]);
                            sOf += 3;
                            dOf += src[sOf - 2];
                        }
                        else
                        {
                            return 0;
                        }
                    }
                    else
                    {
                        return 0;
                    }
                }
                else if (SRCAVAIL((b1 >> 2) + 1) && DSTAVAIL(b1 >> 2))
                {
                    memcpy(dst + dOf, src + (sOf + 1), b1 >> 2);
                    dOf += b1 >> 2;
                    sOf += (b1 >> 2) + 1;
                }
                else
                {
                    return 0;
                }
                break;

            case 1:
                if (!SRCAVAIL(2))
                    return 0;
                bOf = (*((const uint16 *)(src + sOf))) >> 7;
                b2 = ((b1 >> 4) & 7) + 3;
                b1 = ((b1 >> 2) & 3);
                sOf += 2;
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && ((dOf + b1 - bOf) >= 0))
                {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    linearmove(dst + dOf, dst + (dOf - bOf), b2);
                    dOf += b2;
                }
                else
                {
                    return 0;
                }
                break;

            case 2:
                if (!SRCAVAIL(2))
                    return 0;
                bOf = (*((const uint16 *)(src + sOf))) >> 4;
                b1 = ((b1 >> 2) & 3) + 3;
                if (DSTAVAIL(b1) && (dOf - bOf >= 0))
                {
                    linearmove(dst + dOf, dst + (dOf - bOf), b1);
                    dOf += b1;
                    sOf += 2;
                }
                else
                {
                    return 0;
                }
                break;

            case 3:
                if (!SRCAVAIL(3))
                    return 0;
                b2 = ((*((const uint16 *)(src + sOf))) >> 6) & 0x3F;
                b1 = (src[sOf] >> 2) & 0x0F;
                bOf = (*((const uint16 *)(src + (sOf + 1)))) >> 4;
                sOf += 3;
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && ((dOf + b1 - bOf) >= 0))
                {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    linearmove(dst + dOf, dst + (dOf - bOf), b2);
                    dOf += b2;
                }
                else
                {
                    return 0;
                }
                break;
        }
    } while (dOf < dstlen);
#undef SRCAVAIL
#undef DSTAVAIL

    if ((dstlen - dOf) > 0)
        memset(dst + dOf, '\0', dstlen - dOf);

    return 1;
}

static int decompressIterated(uint8 *dst, uint32 dstlen, const uint8 *src, uint32 srclen)
{
    while (srclen)
    {
        if (srclen < 4)
            return 0;
        const uint16 iterations = *((uint16 *)src);
        src += 2;
        const uint16 len = *((uint16 *)src);
        src += 2;
        srclen -= 4;
        if (dstlen < (iterations * len))
            return 0;
        else if (srclen < len)
            return 0;

        for (uint16 i = 0; i < iterations; i++)
        {
            memcpy(dst, src, len);
            dst += len;
            dstlen -= len;
        }

        src += len;
        srclen -= len;
    }

    if (dstlen > 0)
        memset(dst, '\0', dstlen);

    return 1;
}

static inline uint16 lxSelectorToSegment(uint16 index)
{
    return (index << 3) | 7;
}
static uint16 lxSetOs2Tib(uint8 *source)
{
    LxHostThreadState *state = lxHostThreadState();
    if (state->tib_mapping)
    {
        fprintf(stderr, "TIB is already attached\n");
        abort();
    }
    state->tib_mapping = lx_execution_map(GLoaderState.execution, 0, 0x20000, 3, 0);
    if (!state->tib_mapping)
    {
        fprintf(stderr, "Cannot allocate guest TIB\n");
        abort();
    }
    uint32 base = lx_execution_mapping_address(state->tib_mapping);
    uint32 address = (base + 0xffff) & ~0xffffu;
    uint8 *tib = (uint8 *)lx_execution_mapping_host(state->tib_mapping) + (address - base);
    memcpy(tib, source, LXTIBSIZE);
    lx_guest_store_u32(tib + offsetof(LxTIB, tib_ptib2), address + sizeof(LxTIB));
    return lx_execution_attach_tib(GLoaderState.execution, tib);
}
static LxTIB *lxGetOs2Tib(void)
{
    return lx_execution_current_tib(GLoaderState.execution);
}
static void lxDeinitOs2Tib(uint16 token)
{
    lx_execution_detach_tib(GLoaderState.execution, token);
    LxHostThreadState *state = lxHostThreadState();
    if (state->tib_mapping)
        lx_execution_unmap(GLoaderState.execution, state->tib_mapping);
    state->tib_mapping = NULL;
}
static void *lxAllocSegment(uint16 *s, int x)
{
    return lx_execution_alloc_segment(GLoaderState.execution, s, x);
}
static void lxFreeSegment(uint16 s)
{
    lx_execution_free_segment(GLoaderState.execution, s);
}
static int lxCreateCodeAlias(uint16 s, uint16 *out)
{
    return lx_execution_code_alias(GLoaderState.execution, s, out);
}
static int lxFindSelector(uint32 a, uint16 *s, uint16 *o, int x)
{
    return lx_execution_find_selector(GLoaderState.execution, a, s, o, x);
}
static void lxFreeSelector(uint16 s)
{
    lx_execution_free_selector(GLoaderState.execution, s);
}
static void *lxConvert1616to32(uint32 p)
{
    return lx_execution_far_to_host(GLoaderState.execution, p);
}
static uint32 lxConvert32to1616(void *p)
{
    return lx_execution_host_to_far(GLoaderState.execution, p);
}
static void initOs2StackSegments(uint32 a, uint32 n, int release)
{
    lx_execution_stack_segments(GLoaderState.execution, a, n, release);
}
static void fixupLinearCodeSegmentReferences(uint8 *p, size_t n)
{
    lx_execution_prepare_code(GLoaderState.execution, p, n);
}
static void *generateMissingTrampoline(const char *m, const char *n)
{
    return lx_execution_missing_entry(GLoaderState.execution, m, n, 32, NULL);
}
static void *generateMissingTrampoline16(const char *m, const char *n, const LxExport **out)
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
static uint32 guestEntryAddress(void *host)
{
    uint32 address;
    if (!lx_execution_entry_address(GLoaderState.execution, host, &address))
    {
        fprintf(stderr, "%s\n", lx_execution_error(GLoaderState.execution));
        abort();
    }
    return address;
}
static LxExecutionEntry executionEntry(const LxModule *module)
{
    LxExecutionEntry e = {0};
    e.entry = module->eip;
    e.stack = module->esp;
    e.module = module->handle;
    if (!lx_execution_address(GLoaderState.execution, GLoaderState.pib.pib_pchcmd, 1, LX_GUEST_READ,
                              &e.command) ||
        !lx_execution_address(GLoaderState.execution, GLoaderState.pib.pib_pchenv, 1, LX_GUEST_READ,
                              &e.environment))
    {
        fprintf(stderr, "Process parameters are outside guest memory\n");
        abort();
    }
    e.stack_bytes = GLoaderState.mainstacksize;
    e.data_bytes = module->autodatasize;
    if (!module->is_lx && module->header.ne.auto_data_segment)
        e.data_selector =
            lxSelectorToSegment(module->mmaps[module->header.ne.auto_data_segment - 1].alias);
    return e;
}
static void runModule(LxModule *module)
{
    LxExecutionEntry e = executionEntry(module);
    GLoaderState.running = 1;
    lx_execution_program(GLoaderState.execution, &e, module->is_lx ? 32 : 16);
}
static void runLxLibraryInitOrTerm(LxModule *module, int termination)
{
    LxExecutionEntry e = executionEntry(module);
    e.stack = GLoaderState.running ? 0 : GLoaderState.main_module->esp;
    lx_execution_library(GLoaderState.execution, &e, termination);
}

static void freeLxModule(LxModule *lxmod);
static void destroyLxModule(void *resource);

static __attribute__((noreturn)) void lxTerminate(const uint32 exitcode)
{
    /* Guest threads may still execute these mappings; process exit releases them. */
    if (__atomic_load_n(&GLoaderState.ne_threads_started, __ATOMIC_ACQUIRE))
        lx_host_process_exit((int)(exitcode & 0xff));

    freeLxModule(GLoaderState.main_module);

    /* Dependencies precede their dependents in the load list; teardown starts at its head. */
    while (GLoaderState.loaded_modules)
    {
        LxModule *lxmod = GLoaderState.loaded_modules;
        lxmod->refcount = 1;
        freeLxModule(lxmod);
    }

    GLoaderState.lib2ine_shutdown();

    if (!GLoaderState.running)
        lx_execution_destroy(GLoaderState.execution);
    GLoaderState.execution = NULL;

    if (exitcode > 255)
        FIXME("deal with process exit codes > 255. We clamped this one!");

    lx_host_process_exit((int)(exitcode & 0xFF));
}

static void runLxLibraryInit(LxModule *lxmod)
{

    if (1)
    {
        if (lxmod->header.lx.eip_object != 0)
            runLxLibraryInitOrTerm(lxmod, 0);
    }
}

static void runLxLibraryTerm(LxModule *lxmod)
{
    // See notes about global initialization in runLxLibraryInit(); it
    //  applies to deinit here, too.
    if (1)
    {
        if (lxmod->header.lx.eip_object != 0)
            runLxLibraryInitOrTerm(lxmod, 1);
    }
}

static void runNeLibraryInit(LxModule *lxmod)
{
    FIXME("write me");
}

static void runNeLibraryTerm(LxModule *lxmod)
{
    FIXME("write me");
}

static void runLibraryTerm(LxModule *lxmod)
{
    if (lxmod->is_lx)
    {
        runLxLibraryTerm(lxmod);
    }
    else
    {
        runNeLibraryTerm(lxmod);
    }
}

static void freeLxModule(LxModule *lxmod)
{
    if (!lxmod || __atomic_sub_fetch(&lxmod->refcount, 1, __ATOMIC_ACQ_REL))
        return;
    if ((lxmod->initialized) && (lxmod != GLoaderState.main_module))
    {
        if (!lxmod->nativelib)
        {
            runLibraryTerm(lxmod);
        }
        else
        {
            LxNativeModuleDeinitEntryPoint fn =
                (LxNativeModuleDeinitEntryPoint)lx_host_library_symbol(lxmod->nativelib,
                                                                       "lxNativeModuleDeinit");
            if (fn)
                fn();
        }
    }

    lxmod->initialized = 0;
    if (lxmod->handle)
        lx_handle_close(lxmod->handle, LX_HANDLE_MODULE);
    else
        destroyLxModule(lxmod);
}
static void destroyLxModule(void *resource)
{
    LxModule *lxmod = resource;
    if (lxmod->next)
        lxmod->next->prev = lxmod->prev;

    if (lxmod->prev)
        lxmod->prev->next = lxmod->next;

    if (lxmod == GLoaderState.loaded_modules)
        GLoaderState.loaded_modules = lxmod->next;

    if (GLoaderState.main_module == lxmod)
    {
        GLoaderState.main_module = NULL;
        LxMmaps *lxmmap = lxmod->is_lx ? &lxmod->mmaps[lxmod->header.lx.esp_object - 1]
                                       : &lxmod->mmaps[lxmod->header.ne.reg_ss - 1];
        const uint32 stackbase = lxmmap->guest_base;
        initOs2StackSegments(stackbase, lxmmap->size, 1);
        lxmmap->mapped = NULL; /* The current thread may still be using this stack. */
    }

    for (uint32 i = lxmod->num_dependencies; i > 0; i--)
    {
        freeLxModule(lxmod->dependencies[i - 1]);
    }
    free(lxmod->dependencies);

    for (uint32 i = 0; i < lxmod->num_mmaps; i++)
    {
        if (lxmod->mmaps[i].alias != 0xFFFF)
            lxFreeSelector(lxmod->mmaps[i].alias);
        if (lxmod->mmaps[i].mapped)
        {
            if (lxmod->mmaps[i].mapping)
                lx_execution_unmap(GLoaderState.execution, lxmod->mmaps[i].mapping);
            else
                lx_host_memory_free(lxmod->mmaps[i].mapped, lxmod->mmaps[i].size);
        }
    }
    free(lxmod->mmaps);

    if (lxmod->nativelib)
        lx_host_library_close(lxmod->nativelib);
    else
    {
        for (uint32 i = 0; i < lxmod->num_exports; i++)
            free((void *)lxmod->exports[i].name);
        free((void *)lxmod->exports);
    }

    free(lxmod);
}

static LxModule *loadModuleByModuleNameInternal(const char *modname,
                                                const int dependency_tree_depth);

static void *getModuleProcAddrByOrdinal(const LxModule *module, const uint32 ordinal,
                                        const LxExport **_lxexp, const int want16bit,
                                        const int native)
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

static void *getModuleProcAddrByName(const LxModule *module, const char *name,
                                     const LxExport **_lxexp, const int want16bit)
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

static void doFixup(uint8 *page, const sint16 offset, const uint32 finalval, const uint16 finalval2,
                    const uint32 finalsize)
{
#if 0
    if (finalsize == 6) {
        printf("fixing up %p to 0x%X:0x%X (6 bytes)...\n", page + offset, (uint) finalval2, (uint) finalval);
    } else {
        printf("fixing up %p to 0x%X (%u bytes)...\n", page + offset, (uint) finalval, (uint) finalsize);
    }
    fflush(stdout);
#endif

    switch (finalsize)
    {
        case 1:
        {
            uint8 *dst = (uint8 *)(page + offset);
            *dst = (uint8)finalval;
        }
        break;
        case 2:
        {
            uint16 *dst = (uint16 *)(page + offset);
            *dst = (uint16)finalval;
        }
        break;
        case 4:
        {
            uint32 *dst = (uint32 *)(page + offset);
            *dst = (uint32)finalval;
        }
        break;
        case 6:
        {
            uint32 *dst1 = (uint32 *)(page + offset);
            *dst1 = (uint32)finalval;
            uint16 *dst2 = (uint16 *)(page + offset + 4);
            *dst2 = (uint16)finalval2;
            break;
        }
        default:
            fprintf(stderr, "BUG! Unexpected fixup final size of %u\n", (uint)finalsize);
            exit(1);
    }
}

static void fixupPage(const uint8 *exe, LxModule *lxmod, const LxObjectTableEntry *obj,
                      const uint32 pagenum, uint8 *page)
{
    uint32 guest_page;
    if (!lx_execution_address(GLoaderState.execution, page, 0, 0, &guest_page))
        abort();

    assert(lxmod->is_lx);
    const LxHeader *lx = &lxmod->header.lx;

    const uint32 *fixuppage = (((const uint32 *)(exe + lx->fixup_page_table_offset)) +
                               ((obj->page_table_index - 1) + pagenum));
    const uint32 fixupoffset = *fixuppage;
    const uint32 fixuplen = fixuppage[1] - fixuppage[0];
    const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
    const uint8 *fixupend = fixup + fixuplen;

    while (fixup < fixupend)
    {

        const uint8 srctype = *(fixup++);
        const uint8 fixupflags = *(fixup++);
        uint8 srclist_count = 0;
        sint16 srcoffset = 0;

        uint32 finalval = 0;
        uint16 finalval2 = 0;
        uint32 finalsize = 0;
        int allow_fixup_to_alias = 0;

        switch (srctype & 0xF)
        {
            case 0x0: // byte fixup
                finalsize = 1;
                break;
            case 0x2: // 16-bit selector fixup
                finalsize = 2;
                allow_fixup_to_alias = 1;
                break;
            case 0x5: // 16-bit offset fixup
                finalsize = 2;
                break;
            case 0x3: // 16:16 pointer fixup
                finalsize = 4;
                allow_fixup_to_alias = 1;
                break;
            case 0x7: // 32-bit offset fixup
            case 0x8: // 32-bit self-relative offset fixup
                finalsize = 4;
                break;
            case 0x6: // 16:32 pointer fixup
                finalval2 = lx_execution_flat_code_selector(GLoaderState.execution);
                finalsize = 6;
                allow_fixup_to_alias = 1;
                break;
        }

        const int fixup_to_alias = ((srctype & 0x10) != 0);
        if (fixup_to_alias)
        { // fixup to alias flag.
            if (!allow_fixup_to_alias)
                fprintf(stderr, "WARNING: Bogus fixup srctype (%u) with fixup-to-alias flag!\n",
                        (uint)(srctype & 0xF));
        }

        if (srctype & 0x20)
        { // contains a source list
            srclist_count = *(fixup++);
        }
        else
        {
            srcoffset = *((sint16 *)fixup);
            fixup += 2;
        }

        switch (fixupflags & 0x3)
        {
            case 0x0:
            { // Internal fixup record
                uint16 objectid = 0;
                if (fixupflags & 0x40)
                {
                    objectid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    objectid = (uint16) * (fixup++);
                }

                uint32 targetoffset = 0;

                if ((srctype & 0xF) != 2)
                { // not used for 16-bit selector fixups.
                    if (fixupflags & 0x10)
                    { // 32-bit target offset
                        targetoffset = *((uint32 *)fixup);
                        fixup += 4;
                    }
                    else
                    { // 16-bit target offset
                        targetoffset = (uint32)(*((uint16 *)fixup));
                        fixup += 2;
                    }
                }

                if (!fixup_to_alias)
                {
                    const uint32 base = lxmod->mmaps[objectid - 1].guest_base;
                    finalval = base + targetoffset;
                }
                else
                {
                    if (lxmod->mmaps[objectid - 1].alias == 0xFFFF)
                    {
                        fprintf(
                            stderr,
                            "uhoh, need a 16-bit alias fixup, but object has no 16:16 alias!\n");
                    }
                    else
                    {
                        const uint16 segment =
                            lxSelectorToSegment(lxmod->mmaps[objectid - 1].alias);
                        switch (srctype & 0xF)
                        {
                            case 0x2:
                                finalval = segment;
                                break; // 16-bit selector fixup?
                            case 0x3:
                                finalval = (((uint32)segment) << 16) | targetoffset;
                                break; // 16:16 pointer fixup
                            case 0x6:
                                finalval = targetoffset;
                                finalval2 = segment;
                                break; // 16:32 pointer fixup
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case 0x1:
            {                        // Import by ordinal fixup record
                uint16 moduleid = 0; // module ordinal
                if (fixupflags & 0x40)
                {
                    moduleid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    moduleid = (uint16) * (fixup++);
                }

                uint32 importid = 0; // import ordinal
                if (fixupflags & 0x80)
                {
                    importid = (uint32) * (fixup++);
                }
                else if (fixupflags & 0x10)
                {
                    importid = *((uint32 *)fixup);
                    fixup += 4;
                }
                else
                {
                    importid = (uint32) * ((uint16 *)fixup);
                    fixup += 2;
                }

                const LxExport *lxexp = NULL;
                if (getenv("TWOINE_TRACE_FIXUPS"))
                    fprintf(stderr, "2INE FIXORD: mod=%u ord=%u srcoff=%X\n", (uint)moduleid,
                            (uint)importid, (uint)(uint16)srcoffset);
                if (moduleid == 0)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                }
                else if (moduleid > lx->num_import_mod_entries)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n",
                            (uint)moduleid, (uint)lx->num_import_mod_entries);
                }
                else
                {

                    finalval = guestEntryAddress(getModuleProcAddrByOrdinal(
                        lxmod->dependencies[moduleid - 1], importid, &lxexp, fixup_to_alias, 1));
                }

                if (fixup_to_alias)
                {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == 0xFFFF))
                    {
                        fprintf(stderr,
                                "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    }
                    else
                    {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & 0xF)
                        {
                            case 0x2:
                                finalval = segment;
                                break;
                            case 0x3:
                                finalval = (((uint32)segment) << 16) |
                                           (finalval - lxexp->object->guest_base);
                                break; // 16:16 pointer fixup
                            case 0x6:
                                finalval -= lxexp->object->guest_base;
                                finalval2 = segment;
                                break; // 16:32 pointer fixup
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case 0x2:
            {                        // Import by name fixup record
                uint16 moduleid = 0; // module ordinal
                if (fixupflags & 0x40)
                {
                    moduleid = *((uint16 *)fixup);
                    fixup += 2;
                }
                else
                {
                    moduleid = (uint16) * (fixup++);
                }

                uint32 name_offset = 0;
                if (fixupflags & 0x10)
                {
                    name_offset = *((uint32 *)fixup);
                    fixup += 4;
                }
                else
                {
                    name_offset = *((uint16 *)fixup);
                    fixup += 2;
                }

                const LxExport *lxexp = NULL;
                if (moduleid == 0)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                }
                else if (moduleid > lx->num_import_mod_entries)
                {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n",
                            (uint)moduleid, (uint)lx->num_import_mod_entries);
                }
                else
                {
                    const uint8 *import_name = (exe + lx->import_proc_table_offset) + name_offset;
                    char name[128];
                    const uint8 namelen = *(import_name++) & 0x7F; // the top bit is reserved.
                    memcpy(name, import_name, namelen);
                    name[namelen] = '\0';

                    finalval = guestEntryAddress(getModuleProcAddrByName(
                        lxmod->dependencies[moduleid - 1], name, &lxexp, fixup_to_alias));
                }

                if (fixup_to_alias)
                {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == 0xFFFF))
                    {
                        fprintf(stderr,
                                "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    }
                    else
                    {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & 0xF)
                        {
                            case 0x2:
                                finalval = segment;
                                break;
                            case 0x3:
                                finalval = (((uint32)segment) << 16) |
                                           (finalval - lxexp->object->guest_base);
                                break; // 16:16 pointer fixup
                            case 0x6:
                                finalval -= lxexp->object->guest_base;
                                finalval2 = segment;
                                break; // 16:32 pointer fixup
                            default:
                                assert(!"shouldn't hit this code");
                                break;
                        }
                    }
                }
                break;
            }

            case 0x3:
            { // Internal entry table fixup record
                FIXME("FIXUP 0x3 WRITE ME");
                assert(!"write me");
                lx_host_process_exit(1);
                break;
            }
        }

        if (getenv("TWOINE_TRACE_FIXUPS") && (srcoffset >= 0x700) && (srcoffset < 0x800))
            fprintf(stderr,
                    "2INE FIXREC: page=%p srctype=%X flags=%X srcoffset=%X final=%X size=%u\n",
                    page, (uint)srctype, (uint)fixupflags, (uint)(uint16)srcoffset, (uint)finalval,
                    (uint)finalsize);

        if (fixupflags & 0x4)
        { // Has additive.
            uint32 additive = 0;
            if (fixupflags & 0x20)
            {
                additive = *((uint32 *)fixup);
                fixup += 4;
            }
            else
            {
                additive = (uint32) * ((uint16 *)fixup);
                fixup += 2;
            }
            finalval += additive;
        }

        if (srctype & 0x20)
        { // source list
            for (uint8 i = 0; i < srclist_count; i++)
            {
                const sint16 offset = *((sint16 *)fixup);
                fixup += 2;
                if ((srctype & 0xF) == 0x08)
                { // self-relative fixup?
                    assert(finalval2 == 0);
                    doFixup(page, offset, finalval - (guest_page + offset + 4), 0, finalsize);
                }
                else
                {
                    doFixup(page, offset, finalval, finalval2, finalsize);
                }
            }
        }
        else
        {
            if ((srctype & 0xF) == 0x08)
            { // self-relative fixup?
                assert(finalval2 == 0);
                doFixup(page, srcoffset, finalval - (guest_page + srcoffset + 4), 0, finalsize);
            }
            else
            {
                doFixup(page, srcoffset, finalval, finalval2, finalsize);
            }
        }
    }
}

static int loadLxNameTable(LxModule *lxmod, const uint8 *name_table)
{
    for (uint32 i = 0; *name_table; i++)
    {
        const uint8 namelen = *(name_table++);
        const uint8 *nameptr = name_table;
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *)name_table);
        name_table += 2;

        if (ordinal == 0)
            continue; // skip this one.

        LxExport *export = (LxExport *)lxmod->exports;
        const uint32 num_exports = lxmod->num_exports;
        for (uint32 i = 0; i < num_exports; i++, export ++)
        {
            if (export->ordinal == ordinal)
            {
                char *name = (char *)malloc(namelen + 1);
                if (!name)
                {
                    fprintf(stderr, "Out of memory!\n");
                    return 0;
                }
                memcpy(name, nameptr, namelen);
                name[namelen] = '\0';
                export->name = name;
                break;
            }
        }
    }

    return 1;
}

/* LX ("Linear Executable") modules are 32-bit binaries that can contain 16-bit segments. */

static void dumpLoadedModule(const LxModule *lxmod)
{
    const char *dumpdir = getenv("TWOINE_DUMP_MODULES");

    if (getenv("TWOINE_DUMP_MAPS") && lxmod)
    {
        fprintf(stderr, "2INE MAPS: %s", lxmod->name);
        for (uint32 i = 0; i < lxmod->num_mmaps; i++)
            fprintf(stderr, " obj%u=%p+%X", (uint)i + 1, lxmod->mmaps[i].addr,
                    (uint)lxmod->mmaps[i].size);
        fprintf(stderr, "\n");
    }

    if ((!dumpdir) || (!*dumpdir) || (!lxmod))
        return;

    for (uint32 i = 0; i < lxmod->num_mmaps; i++)
    {
        const LxMmaps *mmap = &lxmod->mmaps[i];
        char fname[1024];
        snprintf(fname, sizeof(fname), "%s/%s.obj%u.bin", dumpdir, lxmod->name, (uint)i + 1);
        FILE *io = fopen(fname, "wb");
        if (!io)
        {
            fprintf(stderr, "Couldn't open '%s' for module dump: %s\n", fname, strerror(errno));
            continue;
        }
        fwrite(mmap->addr, mmap->size, 1, io);
        fclose(io);
    }
}

static LxModule *loadLxModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen,
                              int dependency_tree_depth)
{
    LxModule *retval = NULL;
    const LxHeader *lx = (const LxHeader *)exe;
    const uint32 module_type = lx->module_flags & 0x00038000;

    if ((module_type != 0x0000) && (module_type != 0x8000))
    {
        fprintf(stderr, "This is not a standard .exe or .dll (maybe device driver?)\n");
        goto loadlx_failed;
    }
    else if (lx->module_flags & 0x2000)
    {
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadlx_failed;
    }

    const int isDLL = (module_type == 0x8000);

    if (isDLL && !GLoaderState.main_module)
    {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadlx_failed;
    }
    else if (!isDLL && GLoaderState.main_module)
    {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadlx_failed;
    }

    retval = (LxModule *)malloc(sizeof(LxModule));
    if (!retval)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    }
    memset(retval, '\0', sizeof(*retval));
    retval->is_lx = 1;
    retval->refcount = 1;
    retval->handle = lx_handle_create(LX_HANDLE_MODULE, retval, destroyLxModule);
    if (!retval->handle)
    {
        fprintf(stderr, "Cannot allocate module handle\n");
        goto loadlx_failed;
    }
    retval->dependencies = (LxModule **)malloc(sizeof(LxModule *) * lx->num_import_mod_entries);
    retval->mmaps = (LxMmaps *)malloc(sizeof(LxMmaps) * lx->module_num_objects);
    if (!retval->dependencies || !retval->mmaps)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    }
    memset(retval->dependencies, '\0', sizeof(LxModule *) * lx->num_import_mod_entries);
    memset(retval->mmaps, '\0', sizeof(LxMmaps) * lx->module_num_objects);
    retval->num_dependencies = lx->num_import_mod_entries;
    retval->num_mmaps = lx->module_num_objects;
    for (int i = 0; i < retval->num_mmaps; i++)
        retval->mmaps[i].alias = 0xFFFF;

    memcpy(&retval->header.lx, lx, sizeof(*lx));

    if (lx->resident_name_table_offset)
    {
        const uint8 *name_table = exe + lx->resident_name_table_offset;
        const uint8 namelen = *(name_table++);
        char *ptr = retval->name;
        for (uint32 i = 0; i < namelen; i++)
        {
            const char ch = *(name_table++);
            *(ptr++) = ((ch >= 'a') && (ch <= 'z')) ? (ch + ('A' - 'a')) : ch;
        }
        *ptr = '\0';
    }

    if (!isDLL)
    {
        GLoaderState.main_module = retval;
        GLoaderState.pib.pib_hmte = retval->handle;
        if (GLoaderState.guest_pib)
        {
            LxGuestPib *pib = lx_execution_mapping_host(GLoaderState.guest_pib);
            lx_guest_store_u32(&pib->module, retval->handle);
        }
    }

    const char *modname = retval->name;

    if (++dependency_tree_depth > 32)
    {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
        goto loadlx_failed;
    }

    int uses_aliases = 0;
    const LxObjectTableEntry *obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
    {
        if (obj->object_flags & 0x8)
            continue;

        uint32 vsize = obj->virtual_size;
        if ((vsize % lx->page_size) != 0)
            vsize += lx->page_size - (vsize % lx->page_size);

        const int needs_1616_alias =
            ((obj->object_flags & 0x1000) != 0); // Object needs a 16:16 alias.
        if (needs_1616_alias)
        {
            if (vsize > 0x10000)
            {
                fprintf(stderr, "Module '%s' object #%u needs a 16:16 alias, but is > 64k\n",
                        modname, (uint)(i + 1));
                goto loadlx_failed;
            }
            uses_aliases = 1;

            if (isDLL) // (specifically: if !MAP_FIXED)
                vsize += 0x10000;
        }

        void *base = isDLL ? NULL : (void *)((size_t)obj->reloc_base_addr);
        LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, (uint32)(uintptr_t)base,
                                                   vsize, LX_GUEST_READ | LX_GUEST_WRITE, !isDLL);
        void *mmapaddr = mapping ? lx_execution_mapping_host(mapping) : NULL;

        if (!mmapaddr)
        {
            fprintf(stderr, "mmap(%p, %u, RW-, ANON|PRIVATE%s, -1, 0) failed (%d): %s\n", base,
                    (uint)vsize, isDLL ? "" : "|FIXED", errno, strerror(errno));
            goto loadlx_failed;
        }

        retval->mmaps[i].mapping = mapping;
        retval->mmaps[i].mapped = mmapaddr;
        retval->mmaps[i].size = vsize;

        const uint32 guest_mapping_base = lx_execution_mapping_address(mapping);
        if (needs_1616_alias && (guest_mapping_base % 0x10000) != 0)
        {
            assert(isDLL);
            const uint32 diff = 0x10000 - (guest_mapping_base % 0x10000);
            vsize -= diff;
            mmapaddr = (uint8 *)mmapaddr + diff;
        }

        FIXME("Can we just unmap the adjusted pieces?");
        retval->mmaps[i].addr = mmapaddr;
        retval->mmaps[i].guest_base =
            lx_execution_mapping_address(mapping) +
            (uint32)((uint8 *)mmapaddr - (uint8 *)lx_execution_mapping_host(mapping));

        const LxObjectPageTableEntry *objpage =
            ((const LxObjectPageTableEntry *)(exe + lx->object_page_table_offset));
        objpage += obj->page_table_index - 1;

        uint8 *dst = (uint8 *)mmapaddr;
        const uint32 numPageTableEntries = obj->num_page_table_entries;
        for (uint32 pagenum = 0; pagenum < numPageTableEntries; pagenum++, objpage++)
        {
            const uint8 *src;
            switch (objpage->flags)
            {
                case 0x00:
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    memcpy(dst, src, objpage->data_size);
                    if (objpage->data_size < lx->page_size)
                    {
                        memset(dst + objpage->data_size, '\0', lx->page_size - objpage->data_size);
                    }
                    break;

                case 0x01: /* OS/2 iterated LX pages use data_pages_offset as their data origin. */
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressIterated(dst, lx->page_size, src, objpage->data_size))
                    {
                        fprintf(
                            stderr,
                            "Failed to decompress iterated object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    }
                    break;

                case 0x02: // INVALID, just zero it out.
                case 0x03: // ZEROFILL, just zero it out.
                    memset(dst, '\0', lx->page_size);
                    break;

                case 0x05:
                    src = origexe + lx->data_pages_offset +
                          (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressExePack2(dst, lx->page_size, src, objpage->data_size))
                    {
                        fprintf(stderr,
                                "Failed to decompress object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    }
                    break;

                case 0x08:
                default:
                    fprintf(stderr, "Don't know how to load an object page of type %u!\n",
                            (uint)objpage->flags);
                    goto loadlx_failed;
            }

            dst += lx->page_size;
        }

        const uint32 remain = vsize - ((uint32)(dst - ((uint8 *)mmapaddr)));
        if (remain)
            memset(dst, '\0', remain);
    }

    retval->eip = lx->eip;
    if (lx->eip_object != 0)
    {
        const uint32 base = retval->mmaps[lx->eip_object - 1].guest_base;
        retval->eip += base;
    }

    if (!isDLL)
    {
        assert(lx->esp_object != 0);
        const LxMmaps *lxmmap = &retval->mmaps[lx->esp_object - 1];
        const uint32 stackbase = lxmmap->guest_base;
        const uint32 stacksize = (uint32)lxmmap->size;
        retval->esp = lx->esp + stackbase;
        GLoaderState.mainstacksize = stacksize;

        /* Library initialization requires the process stack to be installed. */
        GLoaderState.initOs2Tib(GLoaderState.main_tibspace, (uint8 *)lxmmap->addr + lx->esp,
                                stacksize, 1);
        GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);
        initOs2StackSegments(stackbase, stacksize, 0);
    }

    /* Install stack tiles before object aliases so startup selector numbers stay stable. */
    if (uses_aliases)
    {
        obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
        for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
        {
            if ((obj->object_flags & 0x1000) == 0)
                continue;
            uint16 offset = 0;
            const int iscode = (obj->object_flags & 0x4) ? 1 : 0;
            if (!lxFindSelector(retval->mmaps[i].guest_base, &retval->mmaps[i].alias, &offset,
                                iscode))
            {
                fprintf(stderr,
                        "Ran out of LDT entries getting a 16:16 alias for '%s' object #%u!\n",
                        modname, (uint)(i + 1));
                goto loadlx_failed;
            }
            assert(offset == 0);

            if (obj->object_flags & 0x4)
            { // 16:16 segment with code.
                fixupLinearCodeSegmentReferences((uint8 *)(size_t)retval->mmaps[i].mapped,
                                                 retval->mmaps[i].size);
            }
        }
    }

    uint32 total_ordinals = 0;
    const uint8 *entryptr = exe + lx->entry_table_offset;
    while (*entryptr)
    {
        const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
        const uint8 bundletype = *(entryptr++) & ~0x80;
        if (bundletype != 0x00)
        {
            total_ordinals += numentries;
            entryptr += 2 + ((bundletype == 0x01) ? 3 : 5) * numentries;
        }
    }

    LxExport *lxexp = (LxExport *)calloc(1, sizeof(LxExport) * total_ordinals);
    if (!lxexp)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    }
    memset(lxexp, '\0', sizeof(LxExport) * total_ordinals);
    retval->exports = lxexp;

    uint32 ordinal = 1;
    entryptr = exe + lx->entry_table_offset;
    while (*entryptr)
    {
        const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
        const uint8 bundletype = *(entryptr++) & ~0x80;
        uint16 objidx = 0;

        switch (bundletype)
        {
            case 0x00:
                ordinal += numentries;
                break;

            case 0x01:
                objidx = *((const uint16 *)entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    entryptr++;
                    lxexp->ordinal = ordinal++;
                    lxexp->addr =
                        ((uint8 *)retval->mmaps[objidx].addr) + *((const uint16 *)entryptr);
                    lxexp->object = &retval->mmaps[objidx];
                    lxexp++;
                    entryptr += 2;
                }
                break;

            case 0x03:
                objidx = *((const uint16 *)entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++)
                {
                    entryptr++;
                    lxexp->ordinal = ordinal++;
                    lxexp->addr =
                        ((uint8 *)retval->mmaps[objidx].addr) + *((const uint32 *)entryptr);
                    lxexp->object = &retval->mmaps[objidx];
                    lxexp++;
                    entryptr += 4;
                }
                break;

            case 0x02:
            case 0x04:
                fprintf(stderr, "WRITE ME %s:%d\n", __FILE__, __LINE__);
                goto loadlx_failed;

            default:
                fprintf(stderr, "UNKNOWN ENTRY TYPE (%u)\n\n", (uint)bundletype);
                goto loadlx_failed;
        }
    }

    retval->num_exports = (uint32)(lxexp - retval->exports);

    if (lx->resident_name_table_offset)
    {
        if (!loadLxNameTable(retval, exe + lx->resident_name_table_offset))
            goto loadlx_failed;
    }

    if (lx->non_resident_name_table_offset && lx->non_resident_name_table_len)
    {
        if (!loadLxNameTable(retval, origexe + lx->non_resident_name_table_offset))
            goto loadlx_failed;
    }

    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++)
    {
        const uint8 namelen = *(import_modules_table++);
        if (namelen > 127)
        {
            fprintf(stderr, "Import module %u name is > 127 chars (%u). Corrupt file or bug!\n",
                    (uint)(i + 1), (uint)namelen);
            goto loadlx_failed;
        }
        char name[128];
        memcpy(name, import_modules_table, namelen);
        import_modules_table += namelen;
        name[namelen] = '\0';
        retval->dependencies[i] = loadModuleByModuleNameInternal(name, dependency_tree_depth);
        if (!retval->dependencies[i])
        {
            fprintf(stderr, "Failed to load dependency '%s' for module '%s'\n", name, modname);
            goto loadlx_failed;
        }
    }

    obj = ((const LxObjectTableEntry *)(exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++)
    {
        if (obj->object_flags & 0x8)
            continue;

        uint8 *dst = (uint8 *)retval->mmaps[i].addr;
        const uint32 numPageTableEntries = obj->num_page_table_entries;
        for (uint32 pagenum = 0; pagenum < numPageTableEntries; pagenum++)
        {
            fixupPage(exe, retval, obj, pagenum, dst);
            dst += lx->page_size;
        }

        if ((i == 1) && (strcasecmp(modname, "EMX") == 0))
        {
            /* Suppress the guest signal installer; signal delivery belongs to the host worker. */
            uint8 *ptr = ((uint8 *)retval->mmaps[1].addr) + 28596;
            for (uint32 j = 0; j < 37; j++)
                *(ptr++) = 0x90;
        }

        uint32 object_flags = obj->object_flags;
        if ((i == 4) && (strcasecmp(modname, "HPI") == 0))
        {
            fprintf(stderr, "fixed page permissions to workaround bug in Java DLL\n");
            object_flags |= 0x4;
        }

        /* 386 readable pages are executable; some LX runtimes put callable code in READ-only objects. */
        int prot = ((object_flags & 0x1) ? LX_GUEST_READ : 0) |
                   ((object_flags & 0x2) ? LX_GUEST_WRITE : 0) |
                   ((object_flags & 0x4) ? LX_GUEST_EXECUTE : 0);
        if (prot & LX_GUEST_READ)
            prot |= LX_GUEST_EXECUTE;

        retval->mmaps[i].prot = prot;

        if (!(retval->mmaps[i].mapping
                  ? lx_execution_protect(GLoaderState.execution, retval->mmaps[i].mapping,
                                         (unsigned)prot)
                  : lx_host_vm_protect(retval->mmaps[i].mapped, retval->mmaps[i].size,
                                       (unsigned)prot)))
        {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s, ANON|PRIVATE|FIXED, -1, 0) failed (%d): %s\n",
                    retval->mmaps[i].addr, (uint)retval->mmaps[i].size,
                    (prot & LX_GUEST_READ) ? "R" : "-", (prot & LX_GUEST_WRITE) ? "W" : "-",
                    (prot & LX_GUEST_EXECUTE) ? "X" : "-", errno, strerror(errno));
            goto loadlx_failed;
        }
    }

    dumpLoadedModule(retval);

    retval->os2path = makeOS2Path(fname);
    if (!retval->os2path)
        goto loadlx_failed;

    if (!isDLL)
    {
        retval->initialized = 1;
    }
    else
    {

        assert(GLoaderState.main_module != NULL);
        assert(GLoaderState.main_module != retval);
        runLxLibraryInit(retval);

        retval->initialized = 1;

        if (GLoaderState.loaded_modules)
        {
            retval->next = GLoaderState.loaded_modules;
            GLoaderState.loaded_modules->prev = retval;
        }
        GLoaderState.loaded_modules = retval;
    }

    return retval;

loadlx_failed:
    freeLxModule(retval);
    return NULL;
}

static int fixupNeSegment(const NeSegmentTableEntry *seg, LxModule *lxmod, uint8 *dst,
                          const uint32 sector_shift, const uint8 *origexe, uint8 *exe,
                          uint32 exelen)
{
    assert(!lxmod->is_lx);

    if ((seg->segment_flags & 0x100) == 0)
    {
        return 1; // no fixups here.
    }

    const uint8 *segptr = origexe + (seg->offset << sector_shift);
    const uint8 *fixupptr = segptr + (seg->size ? seg->size : 0xFFFF);
    const uint32 num_fixups = (uint32) * ((const uint16 *)fixupptr);
    fixupptr += 2;
    /* NE chains address the virtual segment, including its zero-filled tail. */
    const uint32 seg_fixup_limit =
        seg->minimum_allocation ? (uint32)seg->minimum_allocation : 0x10000;

    for (uint32 i = 0; i < num_fixups; i++)
    {
        const uint8 srctype = *(fixupptr++);
        const uint8 flags = *(fixupptr++);
        uint32 srcchain_offset = (uint32) * ((const uint16 *)fixupptr);
        fixupptr += 2;
        const uint8 *targetptr = fixupptr;
        fixupptr += 4;
        const int additive = (flags & 0x4) != 0;
        // NE names the patched location "source" and its resolved value "target".
        uint32 target = 0;

        switch (flags & 0x3)
        {
            case 0:
            {
                const LxExport *lxexp = NULL;
                const uint8 segment = targetptr[0];

                const uint16 offset = *((const uint16 *)&targetptr[2]);
                if (segment == 0xFF)
                { // this is like IMPORTORDINAL with ourselves as the module.
                    void *addr = getModuleProcAddrByOrdinal(lxmod, offset, &lxexp, 1, 0);
                    if (addr)
                    {
                        target = lxConvert32to1616(addr);
                    }
                }
                else
                {
                    target = (lxSelectorToSegment(lxmod->mmaps[segment - 1].alias) << 16) | offset;
                }
            }
            break;

            case 1:
            {
                const uint16 module = ((const uint16 *)targetptr)[0];
                const uint16 ordinal = ((const uint16 *)targetptr)[1];
                const LxExport *lxexp = NULL;
                void *addr = getModuleProcAddrByOrdinal(lxmod->dependencies[module - 1], ordinal,
                                                        &lxexp, 1, 1);
                if (addr)
                {
                    target = lxConvert32to1616(addr);
                }
            }
            break;

            case 2:
            {
                const uint16 module = ((const uint16 *)targetptr)[0];
                const uint16 offset = ((const uint16 *)targetptr)[1];
                const LxExport *lxexp = NULL;
                const uint8 *import_name =
                    (exe + lxmod->header.ne.imported_names_table_offset) + offset;
                char name[256];
                const uint8 namelen = *(import_name++);
                memcpy(name, import_name, namelen);
                name[namelen] = '\0';
                void *addr =
                    getModuleProcAddrByName(lxmod->dependencies[module - 1], name, &lxexp, 1);
                if (addr)
                {
                    target = lxConvert32to1616(addr);
                }
            }
            break;

            case 3:
            {

                continue; // just skip this fixup.
            }
            break;

            default:
                fprintf(stderr, "unknown fixup target type %u\n", (uint)(flags & 0x3));
                break;
        }

        if (additive)
        {
            uint16 *source = (uint16 *)(dst + srcchain_offset);
            switch (srctype & 0xF)
            {
                case 0:
                    *((uint8 *)source) += target & 0xFF;
                    break;
                case 2:
                    *source += target >> 16;
                    break;
                case 3:
                    source[0] += target & 0xFFFF;
                    source[1] += target >> 16;
                    break; // FAR_ADDR  ... this adds to the offset, not segment.
                case 5:
                    *source += target & 0xFFFF;
                    break;
                default:
                    fprintf(stderr, "unknown fixup type %u\n", (uint)(srctype & 0xF));
                    break;
            }
        }
        else
        {
            /* Offset zero is a valid first fixup; only subsequent chain links terminate at zero. */
            if (srcchain_offset < seg_fixup_limit)
            {
                do
                {
                    uint16 *source = (uint16 *)(dst + srcchain_offset);
                    const uint32 next_offset = (uint32)*source;
                    switch (srctype & 0xF)
                    {
                        case 0:
                            *((uint8 *)source) = target & 0xFF;
                            break;
                        case 2:
                            *source = target >> 16;
                            break;
                        case 3:
                            source[0] = target & 0xFFFF;
                            source[1] = target >> 16;
                            break;
                        case 5:
                            *source = target & 0xFFFF;
                            break;
                        default:
                            fprintf(stderr, "unknown fixup type %u\n", (uint)(srctype & 0xF));
                            break;
                    }
                    srcchain_offset = next_offset;
                } while ((srcchain_offset != 0) && (srcchain_offset != 0xFFFF) &&
                         (srcchain_offset < seg_fixup_limit));
            }
        }
    }

    return 1;
}

static LxModule *loadNeModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen,
                              int dependency_tree_depth)
{
    LxModule *retval = NULL;
    FIXME("All of this needs bounds checking and corruption testing");
    const NeHeader *ne = (const NeHeader *)exe;
    const uint16 module_type = ne->module_flags & 0x8000;

    if (ne->module_flags & 0x2000)
    {
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadne_failed;
    }

    const int isDLL = (module_type == 0x8000);

    if (isDLL && !GLoaderState.main_module)
    {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadne_failed;
    }
    else if (!isDLL && GLoaderState.main_module)
    {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadne_failed;
    }

    retval = (LxModule *)malloc(sizeof(LxModule));
    if (!retval)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadne_failed;
    }
    memset(retval, '\0', sizeof(*retval));
    retval->is_lx = 0;
    retval->refcount = 1;
    retval->handle = lx_handle_create(LX_HANDLE_MODULE, retval, destroyLxModule);
    if (!retval->handle)
    {
        fprintf(stderr, "Cannot allocate module handle\n");
        free(retval);
        return NULL;
    }
    retval->dependencies =
        (LxModule **)malloc(sizeof(LxModule *) * ne->num_module_ref_table_entries);
    retval->mmaps = (LxMmaps *)malloc(sizeof(LxMmaps) * ne->num_segment_table_entries);
    if (!retval->dependencies || !retval->mmaps)
    {
        fprintf(stderr, "Out of memory!\n");
        goto loadne_failed;
    }
    memset(retval->dependencies, '\0', sizeof(LxModule *) * ne->num_module_ref_table_entries);
    memset(retval->mmaps, '\0', sizeof(LxMmaps) * ne->num_segment_table_entries);
    retval->num_dependencies = ne->num_module_ref_table_entries;
    retval->num_mmaps = ne->num_segment_table_entries;
    for (int i = 0; i < retval->num_mmaps; i++)
        retval->mmaps[i].alias = 0xFFFF;

    memcpy(&retval->header.ne, ne, sizeof(*ne));

    if (ne->resident_name_table_offset)
    {
        const uint8 *name_table = exe + ne->resident_name_table_offset;
        const uint8 namelen = *(name_table++);
        char *ptr = retval->name;
        for (uint32 i = 0; i < namelen; i++)
        {
            const char ch = *(name_table++);
            *(ptr++) = ((ch >= 'a') && (ch <= 'z')) ? (ch + ('A' - 'a')) : ch;
        }
        *ptr = '\0';
    }

    if (!isDLL)
    {
        GLoaderState.main_module = retval;
        GLoaderState.pib.pib_hmte = retval->handle;
        if (GLoaderState.guest_pib)
        {
            LxGuestPib *pib = lx_execution_mapping_host(GLoaderState.guest_pib);
            lx_guest_store_u32(&pib->module, retval->handle);
        }
    }

    const char *modname = retval->name;

    if (++dependency_tree_depth > 32)
    {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
        goto loadne_failed;
    }

    if ((!ne->reg_ss) || (ne->reg_ss > ne->num_segment_table_entries))
    {
        fprintf(stderr, "Bogus reg_ss in module '%s'\n", modname);
        goto loadne_failed;
    }

    FIXME("What do we do if ne->NOAUTODATA doesn't agree with ne->auto_data_segment?");
    const NeSegmentTableEntry *seg = (const NeSegmentTableEntry *)(exe + ne->segment_table_offset);
    const NeSegmentTableEntry *autodataseg =
        ne->auto_data_segment ? (seg + ne->auto_data_segment) : NULL;
    if (autodataseg)
    {
        retval->autodatasize = autodataseg->size;
    }

    for (uint32 i = 0; i < ne->num_segment_table_entries; i++, seg++)
    {
        /* Allocate full 64-KiB NE segments so tiled selectors retain a fixed extent. */
        const uint32 vsize = 0x10000;
        /* NE segments remain fixed for their mapping lifetime. */
        const int iscode = ((seg->segment_flags & 0x7) == 0) ? 1 : 0;
        const int isiterated = (seg->segment_flags & 0x8) ? 1 : 0;
        uint16 selector = 0xFFFF;
        void *mmapaddr = lxAllocSegment(&selector, iscode);

        if (mmapaddr == NULL)
        {
            fprintf(stderr, "lxAllocSegment(%d) failed\n", iscode);
            goto loadne_failed;
        }
        else if (selector == 0xFFFF)
        {
            /* The execution instance retains ownership of this failed allocation. */
            mmapaddr = NULL;
            fprintf(stderr, "Failed to get a selector for a segment\n");
            goto loadne_failed;
        }

        if (getenv("TWOINE_TRACE_SEGMENTS"))
            fprintf(stderr, "2INE NESEG %s #%u addr=%p selector=%04X iscode=%d\n", retval->name,
                    (uint)i + 1, mmapaddr, (uint)selector, iscode);
        uint32 guest_address;
        if (!lx_execution_address(GLoaderState.execution, mmapaddr, vsize, 0, &guest_address))
        {
            fprintf(stderr, "NE segment is outside guest memory\n");
            goto loadne_failed;
        }
        retval->mmaps[i].mapping = lx_execution_mapping_at(GLoaderState.execution, guest_address);
        retval->mmaps[i].guest_base = guest_address;
        retval->mmaps[i].mapped = mmapaddr;
        retval->mmaps[i].size = vsize;
        retval->mmaps[i].addr = mmapaddr;
        retval->mmaps[i].alias = selector;

        uint8 *dst = (uint8 *)mmapaddr;
        const size_t segfiledataoffset = seg->offset;
        const size_t segfiledatalen = seg->size ? seg->size : 0x10000;
        assert(segfiledatalen <= vsize);
        const uint8 *src = origexe + (segfiledataoffset << ne->sector_alignment_shift_count);

        if (isiterated)
        {
            if (!decompressIterated(dst, vsize, src, segfiledatalen))
            {
                fprintf(stderr, "Failed to decompress iterated segment (corrupt file or bug).\n");
                goto loadne_failed;
            }
        }
        else
        {
            size_t leftover = vsize;
            if (segfiledataoffset)
            {
                memcpy(dst, src, segfiledatalen);

                leftover -= segfiledatalen;
                dst += segfiledatalen;
            }
            if (leftover)
            {
                memset(dst, '\0', leftover);
            }
        }
    }

    if (!isDLL && ne->auto_data_segment && (ne->auto_data_segment <= ne->num_segment_table_entries))
    {
        uint32 data_address;
        if (!lx_execution_address(GLoaderState.execution,
                                  retval->mmaps[ne->auto_data_segment - 1].addr, 1, 0,
                                  &data_address))
            abort();
        lx_execution_near_base(GLoaderState.execution, data_address);
    }

    retval->eip = (lxSelectorToSegment(retval->mmaps[ne->reg_cs - 1].alias) << 16) | ne->reg_ip;

    if (!isDLL)
    {
        /* "If SS equals the automatic data segment and SP equals
            zero, the stack pointer is set to the top of the automatic data
            segment just below the additional heap area." */
        uint16 sp = ne->reg_sp;
        if ((ne->reg_ss == ne->auto_data_segment) && (sp == 0))
        {
            if (!autodataseg)
            {
                goto loadne_failed;
            }
            /* Reserve stack space below autodata; the declared minimum can understate runtime usage. */
            uint32 stacktop = (uint32)autodataseg->size + ne->stack_size;
            if (stacktop < ((uint32)autodataseg->size + 0x8000))
                stacktop = (uint32)autodataseg->size + 0x8000;
            if (stacktop >= 0xFFF0)
                stacktop = 0xFFF0;
            sp = (uint16)stacktop;
        }

        retval->esp =
            (lxSelectorToSegment(retval->mmaps[ne->reg_ss - 1].alias) << 16) | (sp ? sp : 0xFFFF);

        uint16 stacksize = ne->stack_size;
        if (stacksize == 0)
        {
            stacksize = sp;
            if (sp && (ne->reg_ss == ne->auto_data_segment))
            {
                stacksize -= autodataseg->size;
            }
        }
        GLoaderState.mainstacksize = stacksize;

        /* Library initialization requires the process stack to be installed. */
        void *topofstack = (void *)(((size_t)retval->mmaps[ne->reg_ss - 1].addr) + sp);
        GLoaderState.initOs2Tib(GLoaderState.main_tibspace, topofstack, stacksize, 1);
        GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);
    }

    if (ne->entry_table_size > 0)
    {
        uint32 total_ordinals = 0;
        const uint8 *entryptr = exe + ne->entry_table_offset;
        while (*entryptr)
        {
            const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
            const uint8 bundletype = *(entryptr++);
            total_ordinals += numentries;
            if (bundletype != 0x00)
            {
                entryptr += ((bundletype == 0xFF) ? 6 : 3) * numentries;
            }
        }

        if (total_ordinals > 0)
        {
            LxExport *lxexp = (LxExport *)calloc(1, sizeof(LxExport) * total_ordinals);
            if (!lxexp)
            {
                fprintf(stderr, "Out of memory!\n");
                goto loadne_failed;
            }
            memset(lxexp, '\0', sizeof(LxExport) * total_ordinals);
            retval->exports = lxexp;

            uint32 ordinal = 1;
            entryptr = exe + ne->entry_table_offset;
            while (*entryptr)
            {
                const uint8 numentries = *(entryptr++); /* number of entries in this bundle */
                const uint8 bundletype = *(entryptr++);

                switch (bundletype)
                {
                    case 0x00:
                        ordinal += numentries;
                        break;

                    case 0xFF:
                        for (uint8 i = 0; i < numentries; i++)
                        {
                            const uint8 flags = *(entryptr++);
                            FIXME("deal with flags");
                            (void)flags;
                            entryptr += 2;
                            const uint8 segment = (*(entryptr++)) - 1;
                            const uint16 offset = *((const uint16 *)entryptr);
                            entryptr += 2;
                            lxexp->ordinal = ordinal++;
                            lxexp->addr = ((uint8 *)retval->mmaps[segment].addr) + offset;
                            lxexp->object = &retval->mmaps[segment];
                            lxexp++;
                        }
                        break;

                    default:
                        for (uint8 i = 0; i < numentries; i++)
                        {
                            const uint8 segment = bundletype - 1;
                            const uint8 flags = *(entryptr++);
                            FIXME("deal with flags");
                            (void)flags;
                            const uint16 offset = *((const uint16 *)entryptr);
                            entryptr += 2;
                            lxexp->ordinal = ordinal++;
                            lxexp->addr = ((uint8 *)retval->mmaps[segment].addr) + offset;
                            lxexp->object = &retval->mmaps[segment];
                            lxexp++;
                        }
                        break;
                }
            }

            retval->num_exports = (uint32)(lxexp - retval->exports);
        }
    }

    if (ne->resident_name_table_offset)
    {
        if (!loadLxNameTable(retval, exe + ne->resident_name_table_offset))
            goto loadne_failed;
    }

    if (ne->non_resident_name_table_offset && ne->non_resident_name_table_size)
    {
        if (!loadLxNameTable(retval, origexe + ne->non_resident_name_table_offset))
            goto loadne_failed;
    }

    const uint16 *import_modules_table = (const uint16 *)(exe + ne->module_reference_table_offset);
    for (uint32 i = 0; i < ne->num_module_ref_table_entries; i++)
    {
        const uint8 *nameptr = (exe + ne->imported_names_table_offset) + *(import_modules_table++);
        const uint8 namelen = *(nameptr++);
        char name[256];
        memcpy(name, nameptr, namelen);
        name[namelen] = '\0';
        retval->dependencies[i] = loadModuleByModuleNameInternal(name, dependency_tree_depth);
        if (!retval->dependencies[i])
        {
            fprintf(stderr, "Failed to load dependency '%s' for module '%s'\n", name, modname);
            goto loadne_failed;
        }
    }

    seg = (const NeSegmentTableEntry *)(exe + ne->segment_table_offset);
    for (uint32 i = 0; i < ne->num_segment_table_entries; i++, seg++)
    {
        if (!fixupNeSegment(seg, retval, (uint8 *)retval->mmaps[i].addr,
                            ne->sector_alignment_shift_count, origexe, exe, exelen))
        {
            goto loadne_failed;
        }

        // Now set all the pages of this object to the proper final permissions...
        const int iscode = ((seg->segment_flags & 0x7) == 0) ? 1 : 0;
        const int preload =
            (seg->segment_flags & 0x40) ? 1 : 0; // PRELOAD implies read-only, according to spec.
        const int prot = ((preload && !iscode) ? 0 : LX_GUEST_WRITE) |
                         (iscode ? LX_GUEST_EXECUTE : 0) | LX_GUEST_READ;
        retval->mmaps[i].prot = prot;

        if (!(retval->mmaps[i].mapping
                  ? lx_execution_protect(GLoaderState.execution, retval->mmaps[i].mapping,
                                         (unsigned)prot)
                  : lx_host_vm_protect(retval->mmaps[i].mapped, retval->mmaps[i].size,
                                       (unsigned)prot)))
        {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s) failed (%d): %s\n", retval->mmaps[i].mapped,
                    (uint)retval->mmaps[i].size, (prot & LX_GUEST_READ) ? "R" : "-",
                    (prot & LX_GUEST_WRITE) ? "W" : "-", (prot & LX_GUEST_EXECUTE) ? "X" : "-",
                    errno, strerror(errno));
            goto loadne_failed;
        }
    }

    retval->os2path = makeOS2Path(fname);
    if (!retval->os2path)
        goto loadne_failed;

    if (!isDLL)
    {
        retval->initialized = 1;
    }
    else
    {

        assert(GLoaderState.main_module != NULL);
        assert(GLoaderState.main_module != retval);
        runNeLibraryInit(retval);

        retval->initialized = 1;

        if (GLoaderState.loaded_modules)
        {
            retval->next = GLoaderState.loaded_modules;
            GLoaderState.loaded_modules->prev = retval;
        }
        GLoaderState.loaded_modules = retval;
    }

    return retval;

loadne_failed:
    freeLxModule(retval);
    return NULL;
}

static LxModule *loadModule(const char *fname, uint8 *exe, uint32 exelen, int dependency_tree_depth)
{

    const uint8 *origexe = exe;
    int is_lx = 0;

    if (!sanityCheckModule(&exe, &exelen, &is_lx))
    {
        return NULL;
    }

    return is_lx ? loadLxModule(fname, origexe, exe, exelen, dependency_tree_depth)
                 : loadNeModule(fname, origexe, exe, exelen, dependency_tree_depth);
}

static LxModule *loadModuleByPathInternal(const char *fname, const int dependency_tree_depth)
{
    size_t size;
    LxHostFsError error;
    uint8 *module = lx_host_file_snapshot(fname, UINT32_MAX, &size, &error);
    if (!module)
    {
        fprintf(stderr, "Cannot read module '%s' (host filesystem result %u)\n", fname,
                (unsigned)error);
        return NULL;
    }
    LxModule *result = loadModule(fname, module, (uint32)size, dependency_tree_depth);
    free(module);
    return result;
}

static inline LxModule *loadModuleByPath(const char *fname)
{
    return loadModuleByPathInternal(fname, 0);
}

static LxModule *loadNativeModule(const char *fname, const char *modname)
{
    LxModule *retval = (LxModule *)malloc(sizeof(LxModule));
    LxHostLibrary *lib = NULL;
    LxNativeModuleInitEntryPoint fn = NULL;
    const LxExport *exports = NULL;
    uint32 num_exports = 0;
    char *os2path = makeOS2Path(fname);

    if (!retval || !modname || !os2path)
        goto loadnative_failed;

    memset(retval, '\0', sizeof(*retval));

    char error[512];
    lib = lx_host_library_open(fname, error, sizeof error);
    if (lib == NULL)
    {
        fprintf(stderr, "Host library '%s' failed: %s\n", fname, error);
        goto loadnative_failed;
    }

    fn = (LxNativeModuleInitEntryPoint)lx_host_library_symbol(lib, "lxNativeModuleInit");
    if (!fn)
        goto loadnative_failed;

    exports = fn(&num_exports);
    if (!exports)
        goto loadnative_failed;
    retval->refcount = 1;
    retval->handle = lx_handle_create(LX_HANDLE_MODULE, retval, destroyLxModule);
    if (!retval->handle)
    {
        fprintf(stderr, "Cannot allocate module handle\n");
        goto loadnative_failed;
    }
    strcpy(retval->name, modname);
    retval->nativelib = lib;
    retval->os2path = os2path;
    retval->exports = exports;
    retval->num_exports = num_exports;
    retval->initialized = 1;

    return retval;

loadnative_failed:
    if (exports)
    {
        LxNativeModuleDeinitEntryPoint cleanup =
            (LxNativeModuleDeinitEntryPoint)lx_host_library_symbol(lib, "lxNativeModuleDeinit");
        if (cleanup)
            cleanup();
    }
    if (lib)
        lx_host_library_close(lib);
    free(os2path);
    free(retval);
    return NULL;
}

static LxModule *loadModuleByModuleNameInternal(const char *modname,
                                                const int dependency_tree_depth)
{

    for (LxModule *i = GLoaderState.loaded_modules; i != NULL; i = i->next)
    {
        if (strcasecmp(i->name, modname) == 0)
        {
            i->refcount++;

            return i;
        }
    }

    char fname[1024];
    snprintf(fname, sizeof(fname), "%s.dll", modname);
    locatePathCaseInsensitive(fname);

    LxModule *retval = NULL;
    if (lx_host_path_exists(fname))
        retval = loadModuleByPathInternal(fname, dependency_tree_depth);

    if (!retval)
    {
        const char *libdirs = getenv("TWOINE_LIBPATH");
        if (libdirs)
        {
            const char *ptr = libdirs;
            while ((!retval) && *ptr)
            {
                const char *end = strchr(ptr, ':');
                const size_t len = end ? (size_t)(end - ptr) : strlen(ptr);
                char dllpath[1024];
                snprintf(dllpath, sizeof(dllpath), "%.*s%s%s.dll", (int)len, ptr,
                         (len == 0) ? "./" : "/", modname);
                locatePathCaseInsensitive(dllpath);
                if (lx_host_path_exists(dllpath))
                    retval = loadModuleByPathInternal(dllpath, dependency_tree_depth);
                ptr = end ? end + 1 : ptr + len;
            }
        }
    }

    if (!retval)
    {
        snprintf(fname, sizeof(fname), "./lib%s.so", modname);
        for (char *ptr = fname; *ptr; ptr++)
        {
            *ptr = (((*ptr >= 'A') && (*ptr <= 'Z')) ? (*ptr - ('A' - 'a')) : *ptr);
        }
        retval = loadNativeModule(fname, modname);

        if ((!retval) && getenv("TWOINE_LIBPATH"))
        {
            const char *libdirs = getenv("TWOINE_LIBPATH");
            const char *ptr = libdirs;
            while ((!retval) && *ptr)
            {
                const char *end = strchr(ptr, ':');
                const size_t len = end ? (size_t)(end - ptr) : strlen(ptr);
                char soname[1200];
                snprintf(soname, sizeof(soname), "%.*s%s%s.so", (int)len, ptr,
                         (len == 0) ? "./lib" : "/lib", modname);
                const char *libbase = strrchr(soname, '/') + 1;
                for (char *p = (char *)libbase; *p; p++)
                {
                    *p = (((*p >= 'A') && (*p <= 'Z')) ? (*p - ('A' - 'a')) : *p);
                }
                retval = loadNativeModule(soname, modname);
                ptr = end ? end + 1 : ptr + len;
            }
        }

        if (retval != NULL)
        {

            if (GLoaderState.loaded_modules)
            {
                retval->next = GLoaderState.loaded_modules;
                GLoaderState.loaded_modules->prev = retval;
            }
            GLoaderState.loaded_modules = retval;
        }
    }
    return retval;
}

static LxModule *loadModuleByModuleName(const char *modname)
{
    return loadModuleByModuleNameInternal(modname, 0);
}

static LxModule *loadModuleByPathOrModuleName(const char *modname)
{
    LxModule *retval = NULL;
    if (!strchr(modname, '/') && !strchr(modname, '\\'))
    {
        char *str = (char *)alloca(strlen(modname) + 1);
        if (str)
        {
            strcpy(str, modname);
            char *ptr = strrchr(str, '.');
            if (ptr)
            {
                *ptr = '\0';
            }
            retval = loadModuleByModuleName(str);
        }
    }
    else
    {
        uint32 err = 0;
        char *path = lxMakeUnixPath(modname, &err);
        if (!path)
            return NULL;
        retval = loadModuleByPath(path);
        free(path);
    }
    return retval;
}

static int describeModuleAddress(const void *ptr, char *buf, const size_t buflen)
{
    const uint32 addr = (uint32)(size_t)ptr;

    for (LxModule *lxmod = GLoaderState.loaded_modules; lxmod; lxmod = lxmod->next)
    {
        for (uint32 i = 0; i < lxmod->num_mmaps; i++)
        {
            const LxMmaps *mmap = &lxmod->mmaps[i];
            const uint32 base = mmap->guest_base;
            const uint32 end = base + (uint32)mmap->size;
            if ((addr >= base) && (addr < end))
            {
                snprintf(buf, buflen, "%s object #%u +0x%X [base=%08X size=%X]", lxmod->name,
                         (uint)i + 1, (uint)(addr - base), base, (uint)mmap->size);
                return 1;
            }
        }
    }

    *buf = '\0';
    return 0;
}

static int executableAt(void *unused, uint32 address)
{
    (void)unused;
    for (LxModule *m = GLoaderState.loaded_modules; m; m = m->next)
        for (uint32 i = 0; i < m->num_mmaps; i++)
        {
            const LxMmaps *p = &m->mmaps[i];
            uintptr_t base = (uintptr_t)p->addr;
            if (address >= base && address - base < p->size)
                return !!(p->prot & LX_GUEST_EXECUTE);
        }
    return -1;
}
static int describeExecutionAddress(void *unused, uint32 address, char *buffer, size_t size)
{
    (void)unused;
    return describeModuleAddress((void *)(uintptr_t)address, buffer, size);
}
static void executionReturned(void *unused, uint32 result)
{
    (void)unused;
    if (GLoaderState.dosExit)
        GLoaderState.dosExit(1, result);
    lxTerminate(result);
}

int main(int argc, char **argv, char **envp)
{
    if (argc < 2)
    {
        fprintf(stderr, "USAGE: %s <program.exe> [...programargs...]\n", argv[0]);
        return 1;
    }

    GLoaderState.using_lx_loader = 1;
    GLoaderState.running = 0;
    GLoaderState.deinitOs2Tib(GLoaderState.main_tib_selector);
    GLoaderState.main_tib_selector = 0;

    const LxExecutionServices services = {
        NULL,      executableAt,  describeExecutionAddress,      executionReturned,
        LXTIBSIZE, sizeof(LxTIB), sizeof(LxTIB) + sizeof(LxTIB2)};
    char error[256];
    GLoaderState.execution =
        lx_execution_create(getenv("TWOINE_BACKEND"), &services, error, sizeof error);
    if (!GLoaderState.execution)
    {
        fprintf(stderr, "2ine: %s\n", error);
        return 1;
    }

    /* Keep the command and environment in one 64-KiB guest segment. */
    const size_t command_offset =
        (size_t)(GLoaderState.pib.pib_pchcmd - GLoaderState.pib.pib_pchenv);
    LxGuestMapping *parameters =
        lx_execution_map(GLoaderState.execution, 0x07000000, 0x10000, 3, 1);
    if (!parameters)
    {
        fprintf(stderr, "Cannot allocate guest process parameters\n");
        return 1;
    }
    char *environment = lx_execution_mapping_host(parameters);
    memcpy(environment, GLoaderState.pib.pib_pchenv, 0x10000);
    free(GLoaderState.pib.pib_pchenv);
    GLoaderState.pib.pib_pchenv = environment;
    GLoaderState.pib.pib_pchcmd = environment + command_offset;
    GLoaderState.guest_pib = lx_execution_map(GLoaderState.execution, 0x07010000, 0x1000, 3, 1);
    if (!GLoaderState.guest_pib)
    {
        fprintf(stderr, "Cannot allocate guest PIB\n");
        return 1;
    }
    LxGuestPib *pib = lx_execution_mapping_host(GLoaderState.guest_pib);
    lx_guest_store_u32(&pib->process_id, GLoaderState.pib.pib_ulpid);
    lx_guest_store_u32(&pib->parent_id, GLoaderState.pib.pib_ulppid);
    lx_guest_store_u32(&pib->command,
                       lx_execution_mapping_address(parameters) + (uint32)command_offset);
    lx_guest_store_u32(&pib->environment, lx_execution_mapping_address(parameters));
    lx_guest_store_u32(&pib->status, GLoaderState.pib.pib_flstatus);
    lx_guest_store_u32(&pib->type, GLoaderState.pib.pib_ultype);

    GLoaderState.setOs2Tib = lxSetOs2Tib;
    GLoaderState.getOs2Tib = lxGetOs2Tib;
    GLoaderState.deinitOs2Tib = lxDeinitOs2Tib;
    GLoaderState.allocSegment = lxAllocSegment;
    GLoaderState.freeSegment = lxFreeSegment;
    GLoaderState.createCodeAlias = lxCreateCodeAlias;
    GLoaderState.findSelector = lxFindSelector;
    GLoaderState.freeSelector = lxFreeSelector;
    GLoaderState.convert1616to32 = lxConvert1616to32;
    GLoaderState.convert32to1616 = lxConvert32to1616;
    GLoaderState.loadModule = loadModuleByPathOrModuleName;
    GLoaderState.freeModule = freeLxModule;
    GLoaderState.makeUnixPath = lxMakeUnixPath;
    GLoaderState.terminate = lxTerminate;

    if (getenv("TWOINE_TRACE_ARGS"))
    {
        fprintf(stderr, "2INE ARGS:");
        for (int i = 0; i < argc; i++)
            fprintf(stderr, " [%d]='%s'", i, argv[i]);
        fprintf(stderr, "\n2INE CMD='%s'\n",
                GLoaderState.pib.pib_pchcmd ? GLoaderState.pib.pib_pchcmd : "(null)");
    }

    const char *modulename = GLoaderState.subprocess ? getenv("IS_2INE") : argv[1];
    LxModule *lxmod = loadModuleByPath(modulename);
    if (lxmod != NULL)
        runModule(lxmod);

    return 1;
}
