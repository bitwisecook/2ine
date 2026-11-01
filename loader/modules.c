/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"

/* EXEPACK2 overlap expands newly written bytes; memmove would preserve stale input. */

/* this algorithm is from lxlite 138u. */

void freeLxModule(LxModule *lxmod);
void destroyLxModule(void *resource);

LxModule *loadModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth);

/* LX ("Linear Executable") modules are 32-bit binaries that can contain 16-bit segments. */

int sanityCheckModule(uint8 **_exe, uint32 *_exelen, int *_is_lx)
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

uint16 lxSelectorToSegment(uint16 index)
{
    return (index << 3) | 7;
}

uint16 lxSetOs2Tib(uint8 *source)
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

LxTIB *lxGetOs2Tib(void)
{
    return lx_execution_current_tib(GLoaderState.execution);
}

void lxDeinitOs2Tib(uint16 token)
{
    lx_execution_detach_tib(GLoaderState.execution, token);
    LxHostThreadState *state = lxHostThreadState();
    if (state->tib_mapping)
        lx_execution_unmap(GLoaderState.execution, state->tib_mapping);
    state->tib_mapping = NULL;
}

void *lxAllocSegment(uint16 *s, int x)
{
    return lx_execution_alloc_segment(GLoaderState.execution, s, x);
}

void lxFreeSegment(uint16 s)
{
    lx_execution_free_segment(GLoaderState.execution, s);
}

int lxCreateCodeAlias(uint16 s, uint16 *out)
{
    return lx_execution_code_alias(GLoaderState.execution, s, out);
}

int lxFindSelector(uint32 a, uint16 *s, uint16 *o, int x)
{
    return lx_execution_find_selector(GLoaderState.execution, a, s, o, x);
}

void lxFreeSelector(uint16 s)
{
    lx_execution_free_selector(GLoaderState.execution, s);
}

void *lxConvert1616to32(uint32 p)
{
    return lx_execution_far_to_host(GLoaderState.execution, p);
}

uint32 lxConvert32to1616(void *p)
{
    return lx_execution_host_to_far(GLoaderState.execution, p);
}

void initOs2StackSegments(uint32 a, uint32 n, int release)
{
    lx_execution_stack_segments(GLoaderState.execution, a, n, release);
}

void fixupLinearCodeSegmentReferences(uint8 *p, size_t n)
{
    lx_execution_prepare_code(GLoaderState.execution, p, n);
}

void freeLxModule(LxModule *lxmod)
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

void destroyLxModule(void *resource)
{
    LxModule *lxmod = resource;
    if (lxmod->next)
        lxmod->next->prev = lxmod->prev;

    if (lxmod->prev)
        lxmod->prev->next = lxmod->next;

    if (lxmod == GLoaderState.loaded_modules)
        GLoaderState.loaded_modules = lxmod->next;

    /* DLL termination still needs the process stack and main-module entry state. */
    for (uint32 i = lxmod->num_dependencies; i > 0; i--)
    {
        freeLxModule(lxmod->dependencies[i - 1]);
    }
    free(lxmod->dependencies);

    if (GLoaderState.main_module == lxmod)
    {
        GLoaderState.main_module = NULL;
        LxMmaps *lxmmap = lxmod->is_lx ? &lxmod->mmaps[lxmod->header.lx.esp_object - 1]
                                       : &lxmod->mmaps[lxmod->header.ne.reg_ss - 1];
        const uint32 stackbase = lxmmap->guest_base;
        initOs2StackSegments(stackbase, lxmmap->size, 1);
        lxmmap->mapped = NULL; /* The current thread may still be using this stack. */
    }

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
    free(lxmod->resources);

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

void dumpLoadedModule(const LxModule *lxmod)
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

LxModule *loadModule(const char *fname, uint8 *exe, uint32 exelen, int dependency_tree_depth)
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

LxModule *loadModuleByPathInternal(const char *fname, const int dependency_tree_depth)
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

LxModule *loadModuleByPath(const char *fname)
{
    return loadModuleByPathInternal(fname, 0);
}

LxModule *loadNativeModule(const char *fname, const char *modname)
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

LxModule *loadModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth)
{
    /* LX import module names may include the DLL filename suffix. */
    char canonical[256];
    size_t name_length = strlen(modname);
    if (name_length > 4 && name_length < sizeof canonical &&
        strcasecmp(modname + name_length - 4, ".dll") == 0)
    {
        memcpy(canonical, modname, name_length - 4);
        canonical[name_length - 4] = '\0';
        modname = canonical;
    }

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

    /* DOSCALL1 is the implementation behind the system DOSCALLS forwarder. */
    if (!retval && strcasecmp(modname, "DOSCALL1") == 0)
        return loadModuleByModuleNameInternal("DOSCALLS", dependency_tree_depth);

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

LxModule *loadModuleByModuleName(const char *modname)
{
    return loadModuleByModuleNameInternal(modname, 0);
}

LxModule *loadModuleByPathOrModuleName(const char *modname)
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

int describeModuleAddress(const void *ptr, char *buf, const size_t buflen)
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

int executableAt(void *unused, uint32 address)
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

int describeExecutionAddress(void *unused, uint32 address, char *buffer, size_t size)
{
    (void)unused;
    return describeModuleAddress((void *)(uintptr_t)address, buffer, size);
}
