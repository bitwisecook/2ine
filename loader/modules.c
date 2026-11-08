/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
#include "emulator/features.h"

typedef struct ModuleFailure
{
    uint32 code;
    char module[256];
} ModuleFailure;

static _Thread_local ModuleFailure LastModuleFailure;
static _Thread_local ModuleFailure *ActiveModuleFailure;

static void moduleLoadError(uint32 code, const char *name)
{
    ModuleFailure *failure = ActiveModuleFailure ? ActiveModuleFailure : &LastModuleFailure;
    if (!failure->code)
    {
        failure->code = code;
        snprintf(failure->module, sizeof failure->module, "%s", name);
    }
}

static int moduleLoadFailed(void)
{
    return ActiveModuleFailure && ActiveModuleFailure->code;
}

/* EXEPACK2 overlap expands newly written bytes; memmove would preserve stale input. */

/* this algorithm is from lxlite 138u. */

void freeLxModule(LxModule *lxmod);
void destroyLxModule(void *resource);

static LxModule *loadGuestLibraryPath(const char *paths, const char *name, int depth)
{
    if (!paths)
        return NULL;
    while (*paths)
    {
        const char *end = strchr(paths, ';');
        size_t length = end ? (size_t)(end - paths) : strlen(paths);
        char path[2048];
        if (length && length + strlen(name) + 6 < sizeof path)
        {
            snprintf(path, sizeof path, "%.*s\\%s.dll", (int)length, paths, name);
            uint32 error = 0;
            char *host = lxMakeUnixPath(path, &error);
            if (host)
            {
                LxModule *module =
                    lx_host_path_exists(host) ? loadModuleByPathInternal(host, depth) : NULL;
                free(host);
                if (module || moduleLoadFailed())
                    return module;
            }
        }
        paths = end ? end + 1 : paths + length;
    }
    return NULL;
}

LxModule *loadModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth);

static uint64 ModuleInitializationOrder;

static int initializeModuleGraph(LxModule *module)
{
    if (!module)
        return 0;
    if (module->initialized || module->init_started)
        return 1;
    module->init_started = 1;
    /* OS/2 starts this system service before applications. Bring up the
       original service only when an original REXX API image needs it. */
    const int rexx_api = !module->nativelib && !strcasecmp(module->name, "REXXAPI");
    for (uint32 i = 0; i < module->num_dependencies; i++)
        if (!initializeModuleGraph(module->dependencies[i]))
            goto failed;
    if (rexx_api && !ensureRexxService())
        goto failed;
    if (module != GProcessState.main_module)
    {
        if (module->is_lx)
            runLxLibraryInit(module);
        else
            runNeLibraryInit(module);
    }
    module->initialization_order = ++ModuleInitializationOrder;
    module->initialized = 1;
    return 1;
failed:
    module->init_started = 0;
    return 0;
}

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
    state->tib_mapping = lx_execution_map(GProcessState.execution, 0, 0x20000, 3, 0);
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
    return lx_execution_attach_tib(GProcessState.execution, tib);
}

LxTIB *lxGetOs2Tib(void)
{
    return lx_execution_current_tib(GProcessState.execution);
}

void lxDeinitOs2Tib(uint16 token)
{
    lxHostThreadCleanup();
    lx_execution_detach_tib(GProcessState.execution, token);
    LxHostThreadState *state = lxHostThreadState();
    if (state->tib_mapping)
        lx_execution_unmap(GProcessState.execution, state->tib_mapping);
    state->tib_mapping = NULL;
}

void *lxAllocSegment(uint16 *s, int x)
{
    return lx_execution_alloc_segment(GProcessState.execution, s, x);
}

void lxFreeSegment(uint16 s)
{
    lx_execution_free_segment(GProcessState.execution, s);
}

int lxCreateCodeAlias(uint16 s, uint16 *out)
{
    return lx_execution_code_alias(GProcessState.execution, s, out);
}

int lxFindSelector(uint32 a, uint16 *s, uint16 *o, int x)
{
    return lx_execution_find_selector(GProcessState.execution, a, s, o, x);
}

void lxFreeSelector(uint16 s)
{
    lx_execution_free_selector(GProcessState.execution, s);
}

void *lxConvert1616to32(uint32 p)
{
    return lx_execution_far_to_host(GProcessState.execution, p);
}

uint32 lxConvert32to1616(void *p)
{
    return lx_execution_host_to_far(GProcessState.execution, p);
}

void initOs2StackSegments(uint32 a, uint32 n, int release)
{
    lx_execution_stack_segments(GProcessState.execution, a, n, release);
}

void fixupLinearCodeSegmentReferences(uint8 *p, size_t n)
{
    lx_execution_prepare_code(GProcessState.execution, p, n);
}

static void terminateModule(LxModule *lxmod)
{
    if ((lxmod->initialized) && (lxmod != GProcessState.main_module))
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
}

void registerLxModule(LxModule *module)
{
    module->next = GProcessState.loaded_modules;
    if (module->next)
        module->next->prev = module;
    GProcessState.loaded_modules = module;
    module->listed = 1;
}

static void unregisterModule(LxModule *module)
{
    if (!module->listed)
        return;
    if (module->next)
        module->next->prev = module->prev;
    if (module->prev)
        module->prev->next = module->next;
    if (GProcessState.loaded_modules == module)
        GProcessState.loaded_modules = module->next;
    module->next = module->prev = NULL;
    module->listed = 0;
}

static void collectModules(void)
{
    static int collecting;
    if (collecting)
        return;
    collecting = 1;
    int removed;
    do
    {
        removed = 0;
        for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
        {
            m->incoming_references = 0;
            m->reachable = m->loading;
        }
        for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
            for (uint32 i = 0; i < m->num_dependencies; i++)
                if (m->dependencies[i])
                    m->dependencies[i]->incoming_references++;
        for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
            if (m->refcount > m->incoming_references)
                m->reachable = 1;
        int changed;
        do
        {
            changed = 0;
            for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
                if (m->reachable)
                    for (uint32 i = 0; i < m->num_dependencies; i++)
                    {
                        LxModule *dependency = m->dependencies[i];
                        if (dependency && !dependency->reachable)
                        {
                            dependency->reachable = 1;
                            changed = 1;
                        }
                    }
        } while (changed);

        /* A DLL's termination entry can still call its dependencies, including
           native API bridges. Retire dependents before their dependencies. */
        for (;;)
        {
            LxModule *selected = NULL, *cycle = NULL;
            for (LxModule *candidate = GProcessState.loaded_modules; candidate;
                 candidate = candidate->next)
            {
                if (candidate->reachable || !candidate->initialized)
                    continue;
                if (!cycle || (!candidate->nativelib &&
                               (cycle->nativelib ||
                                candidate->initialization_order > cycle->initialization_order)))
                    cycle = candidate;
                int dependent = 0;
                for (LxModule *m = GProcessState.loaded_modules; m && !dependent; m = m->next)
                    if (m != candidate && !m->reachable && m->initialized)
                        for (uint32 i = 0; i < m->num_dependencies; i++)
                            if (m->dependencies[i] == candidate)
                            {
                                dependent = 1;
                                break;
                            }
                if (!dependent)
                {
                    selected = candidate;
                    break;
                }
            }
            /* Circular guests retire in reverse initialization order. Keep
               native services until every guest callback has returned. */
            if (!selected)
                selected = cycle;
            if (!selected)
                break;
            terminateModule(selected);
        }
        for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
            if (!m->reachable)
            {
                for (uint32 i = 0; i < m->num_dependencies; i++)
                    if (m->dependencies[i])
                        __atomic_sub_fetch(&m->dependencies[i]->refcount, 1, __ATOMIC_ACQ_REL);
                m->num_dependencies = 0;
            }
        LxModule *next;
        for (LxModule *m = GProcessState.loaded_modules; m; m = next)
        {
            next = m->next;
            if (!m->reachable)
            {
                removed = 1;
                unregisterModule(m);
                lx_handle_close(m->handle, LX_HANDLE_MODULE);
            }
        }
    } while (removed);
    collecting = 0;
}

void unloadAllModules(void)
{
    while (GProcessState.loaded_modules)
    {
        for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
        {
            m->loading = 0;
            m->refcount = 0;
        }
        for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
            for (uint32 i = 0; i < m->num_dependencies; i++)
                if (m->dependencies[i])
                    m->dependencies[i]->refcount++;
        collectModules();
    }
}

void freeLxModule(LxModule *lxmod)
{
    if (!lxmod)
        return;
    uint32 remaining = __atomic_sub_fetch(&lxmod->refcount, 1, __ATOMIC_ACQ_REL);
    if (lxmod->listed)
    {
        collectModules();
        return;
    }
    if (remaining)
        return;
    terminateModule(lxmod);
    if (lxmod->handle)
        lx_handle_close(lxmod->handle, LX_HANDLE_MODULE);
    else
        destroyLxModule(lxmod);
}

void destroyLxModule(void *resource)
{
    LxModule *lxmod = resource;
    unregisterModule(lxmod);

    /* DLL termination still needs the process stack and main-module entry state. */
    for (uint32 i = lxmod->num_dependencies; i > 0; i--)
    {
        freeLxModule(lxmod->dependencies[i - 1]);
    }
    free(lxmod->dependencies);

    if (GProcessState.main_module == lxmod)
    {
        GProcessState.main_module = NULL;
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
                lx_execution_unmap(GProcessState.execution, lxmod->mmaps[i].mapping);
            else
                lx_host_memory_free(lxmod->mmaps[i].mapped, lxmod->mmaps[i].size);
        }
    }
    free(lxmod->mmaps);
    free(lxmod->resources);
    free(lxmod->os2path);

    if (lxmod->nativelib)
        lx_host_library_close(lxmod->nativelib);
    else
    {
        for (uint32 i = 0; i < lxmod->num_exports; i++)
        {
            free((void *)lxmod->exports[i].name);
            if (lxmod->exports[i].indirect == LX_EXPORT_FORWARDER)
                free(lxmod->exports[i].addr);
        }
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
    const char *base = strrchr(fname, '/');
    base = base ? base + 1 : fname;
    if (!strcasecmp(base, "SSLEAY32.DLL") && lxRuntimeTlsUpgrade())
        return loadModuleByModuleNameInternal("SSLEAY32", dependency_tree_depth);
    if (((!strcasecmp(base, "OPENGL.DLL") || !strcasecmp(base, "PGL.DLL")) &&
         lxRuntimeOpenGLProvider() != LX_OPENGL_ORIGINAL) ||
        (!strcasecmp(base, "DIVE.DLL") && !lxRuntimeFeatureEnabled(LX_FEATURE_DIVE)))
    {
        fprintf(stderr, "2ine: original %s is excluded by this ball's Graphics & Media settings\n",
                base);
        moduleLoadError(126, base);
        return NULL;
    }
    char *os2path = makeOS2Path(fname);
    if (os2path)
    {
        for (LxModule *module = GProcessState.loaded_modules; module; module = module->next)
            if (!module->load_failed && !strcasecmp(module->os2path, os2path))
            {
                __atomic_add_fetch(&module->refcount, 1, __ATOMIC_ACQ_REL);
                free(os2path);
                return module;
            }
        free(os2path);
    }
    size_t size;
    LxHostFsError error;
    uint8 *module = lx_host_file_snapshot(fname, UINT32_MAX, &size, &error);
    if (!module)
    {
        moduleLoadError(error == LX_HOST_FS_MISSING ? 2 : 5, fname);
        if (GProcessState.trace_native)
            fprintf(stderr, "Cannot read module '%s' (host filesystem result %u)\n", fname,
                    (unsigned)error);
        return NULL;
    }
    LxModule *result = loadModule(fname, module, (uint32)size, dependency_tree_depth);
    free(module);
    if (!result)
        moduleLoadError(11, fname);
    return result;
}

LxModule *loadModuleByPath(const char *fname)
{
    ModuleFailure failure = {0}, *previous = ActiveModuleFailure;
    ActiveModuleFailure = &failure;
    LxModule *module = loadModuleByPathInternal(fname, 0);
    if (!initializeModuleGraph(module))
    {
        moduleLoadError(11, fname);
        module = NULL;
    }
    ActiveModuleFailure = previous;
    LastModuleFailure = failure;
    return module;
}

LxModule *loadNativeModule(const char *fname, const char *modname)
{
    /* dyld may resolve a missing relative filename through an executable's
       rpaths. Keep the explicit search order so module names describe the
       actual file, including the ball's guest-visible runtime snapshot. */
    if (!lx_host_path_exists(fname))
        return NULL;

    LxModule *retval = (LxModule *)malloc(sizeof(LxModule));
    LxHostLibrary *lib = NULL;
    LxNativeModuleInitEntryPoint fn = NULL;
    const LxExport *exports = NULL;
    uint32 num_exports = 0;
    char *os2path = makeOS2Path(fname);
    /* Native API modules can live outside every guest drive. */
    if (!os2path && modname)
        os2path = strdup(modname);

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

    const int gl_module = !strcasecmp(modname, "OPENGL") || !strcasecmp(modname, "PGL");
    const int provider = lxRuntimeOpenGLProvider();
    if ((gl_module && provider == LX_OPENGL_OFF) ||
        (!strcasecmp(modname, "DIVE") && !lxRuntimeFeatureEnabled(LX_FEATURE_DIVE)))
    {
        fprintf(stderr, "2ine: %s is disabled in this ball's Graphics & Media settings\n", modname);
        moduleLoadError(126, modname);
        return NULL;
    }
    for (LxModule *i = GProcessState.loaded_modules; i != NULL; i = i->next)
    {
        if (!i->load_failed && (!GProcessState.libpath_strict || i->nativelib) &&
            strcasecmp(i->name, modname) == 0)
        {
            i->refcount++;

            return i;
        }
    }

    char fname[1024];
    snprintf(fname, sizeof(fname), "%s.dll", modname);
    locatePathCaseInsensitive(fname);

    LxModule *retval = NULL;
    /* A selected provider is exclusive: guest DLLs cannot silently replace Mesa. */
    if (gl_module && provider == LX_OPENGL_MESA)
        goto native_provider;
    /* An opted-in upgrade must replace the guest TLS DLL as a whole. */
    if (!strcasecmp(modname, "SSLEAY32") && lxRuntimeTlsUpgrade())
        goto native_provider;
    retval = loadGuestLibraryPath(GProcessState.begin_libpath, modname, dependency_tree_depth);
    if (!retval && moduleLoadFailed())
        return NULL;
    if (!retval && lx_host_path_exists(fname))
        retval = loadModuleByPathInternal(fname, dependency_tree_depth);
    if (!retval && moduleLoadFailed())
        return NULL;

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
                if (!retval && moduleLoadFailed())
                    return NULL;
                ptr = end ? end + 1 : ptr + len;
            }
        }
    }

    if (!retval)
        retval = loadGuestLibraryPath(GProcessState.libpath, modname, dependency_tree_depth);
    if (!retval)
        retval = loadGuestLibraryPath(GProcessState.end_libpath, modname, dependency_tree_depth);

    if (!retval && moduleLoadFailed())
        return NULL;

    /* DOSCALL1 is the implementation behind the system DOSCALLS forwarder. */
    if (!retval && strcasecmp(modname, "DOSCALL1") == 0)
        return loadModuleByModuleNameInternal("DOSCALLS", dependency_tree_depth);

native_provider:
    if (!retval && !(gl_module && provider == LX_OPENGL_ORIGINAL))
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
            registerLxModule(retval);
        }
    }
    if (!retval && gl_module)
        fprintf(stderr, "2ine: selected %s OpenGL provider is unavailable; %s\n",
                provider == LX_OPENGL_MESA ? "Mesa" : "original software",
                provider == LX_OPENGL_MESA
                    ? "choose a runtime with Mesa OpenGL support"
                    : "extract original OS/2 OPENGL.DLL and its dependencies");
    if (!retval)
        moduleLoadError(126, modname);
    return retval;
}

LxModule *loadModuleByModuleName(const char *modname)
{
    ModuleFailure failure = {0}, *previous = ActiveModuleFailure;
    ActiveModuleFailure = &failure;
    LxModule *module = loadModuleByModuleNameInternal(modname, 0);
    if (!initializeModuleGraph(module))
    {
        moduleLoadError(11, modname);
        module = NULL;
    }
    ActiveModuleFailure = previous;
    LastModuleFailure = failure;
    return module;
}

LxModule *loadModuleWithError(const char *modname, uint32 *error, char *name, size_t capacity)
{
    LxModule *module = loadModuleByPathOrModuleName(modname);
    if (error)
        *error = module ? 0 : LastModuleFailure.code;
    if (name && capacity)
        snprintf(name, capacity, "%s", module ? "" : LastModuleFailure.module);
    return module;
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
        {
            memset(&LastModuleFailure, 0, sizeof LastModuleFailure);
            LastModuleFailure.code = err ? err : 2;
            snprintf(LastModuleFailure.module, sizeof LastModuleFailure.module, "%s", modname);
            return NULL;
        }
        retval = loadModuleByPath(path);
        free(path);
    }
    return retval;
}

int describeModuleAddress(const void *ptr, char *buf, const size_t buflen)
{
    const uint32 addr = (uint32)(size_t)ptr;

    for (LxModule *lxmod = GProcessState.loaded_modules; lxmod; lxmod = lxmod->next)
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
    for (LxModule *m = GProcessState.loaded_modules; m; m = m->next)
        for (uint32 i = 0; i < m->num_mmaps; i++)
        {
            const LxMmaps *p = &m->mmaps[i];
            uintptr_t base = (uintptr_t)p->addr;
            if (address >= base && address - base < p->size)
                return !!(p->prot & LX_GUEST_EXECUTE);
        }
    return -1;
}

/* Runs in the fatal reporter, possibly from a signal handler: no locks or allocation. */
void lxWriteFatalModules(void)
{
    lx_host_fatal_write("\n-- OS/2 modules --\n");
    for (const LxModule *module = GProcessState.loaded_modules; module; module = module->next)
    {
        lx_host_fatal_write(module->name);
        lx_host_fatal_write(module->is_lx ? " LX " : " NE ");
        lx_host_fatal_write(module->os2path ? module->os2path : "(no OS/2 path)");
        for (uint32 i = 0; i < module->num_mmaps; i++)
        {
            lx_host_fatal_write(" obj");
            lx_host_fatal_write_decimal(i + 1);
            lx_host_fatal_write("=");
            lx_host_fatal_write_hex(module->mmaps[i].guest_base, 8);
            lx_host_fatal_write("+");
            lx_host_fatal_write_hex(module->mmaps[i].size, 8);
        }
        lx_host_fatal_write("\n");
    }
}

void lxDescribeModuleFailure(const char *program, char *buffer, size_t size)
{
    const uint32 code = LastModuleFailure.code;
    const char *reason = code == 2 || code == 3       ? "the file was not found"
                         : code == 5                  ? "access was denied"
                         : code == 11                 ? "it is not a valid OS/2 module"
                         : code == 126                ? "a required module was not found"
                         : code == 127 || code == 182 ? "a required function was not found"
                                                      : "it could not be loaded";
    if (*LastModuleFailure.module && strcmp(LastModuleFailure.module, program))
        snprintf(buffer, size, "Cannot load %s: %s (%s, OS/2 error %u)", program, reason,
                 LastModuleFailure.module, (uint)code);
    else
        snprintf(buffer, size, "Cannot load %s: %s (OS/2 error %u)", program, reason, (uint)code);
}

int describeExecutionAddress(void *unused, uint32 address, char *buffer, size_t size)
{
    (void)unused;
    return describeModuleAddress((void *)(uintptr_t)address, buffer, size);
}
