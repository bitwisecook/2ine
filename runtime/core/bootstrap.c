/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "runtime/core/internal.h"
#include "host/fatal.h"

LxProcessState GProcessState;

LX_PRIVATE LX_HOST_THREAD_LOCAL LxTIB *DefaultTib;
LX_PRIVATE LX_HOST_THREAD_LOCAL LxHostThreadState host_thread_state;

LX_NATIVE_CONSTRUCTOR(lib2ine)
{
    /* The failure-dialog helper needs none of the OS/2 process state below. */
    if (lx_host_fatal_is_dialog())
        return;
    lx_host_fatal_install();
    memset(&GProcessState, '\0', sizeof(GProcessState));
    GProcessState.dosExit = dosExit_lib2ine;
    GProcessState.initOs2Tib = initOs2Tib_lib2ine;
    GProcessState.setOs2Tib = setOs2Tib_lib2ine;
    GProcessState.getOs2Tib = getOs2Tib_lib2ine;
    GProcessState.deinitOs2Tib = deinitOs2Tib_lib2ine;
    GProcessState.allocSegment = allocSegment_lib2ine;
    GProcessState.freeSegment = freeSegment_lib2ine;
    GProcessState.findSelector = findSelector_lib2ine;
    GProcessState.freeSelector = freeSelector_lib2ine;
    GProcessState.convert1616to32 = convert1616to32_lib2ine;
    GProcessState.convert32to1616 = convert32to1616_lib2ine;
    GProcessState.loadModule = loadModule_lib2ine;
    GProcessState.makeUnixPath = makeUnixPath_lib2ine;
    GProcessState.terminate = terminate_lib2ine;
    GProcessState.registerAudioGenerator = lxRegisterAudioGenerator;
    GProcessState.registerStereoAudioGenerator = lxRegisterStereoAudioGenerator;
    GProcessState.lib2ine_shutdown = lib2ine_shutdown;

    /* Use a low amplitude to match the PC speaker mix. */
    GProcessState.beep_volume = 0.05f;
    GProcessState.display_dpi = 96;
    GProcessState.display_depth = 24;

    cfgLoadFiles();
    const char *display_dpi = getenv("TWOINE_DISPLAY_DPI");
    if (display_dpi)
    {
        char *end = NULL;
        unsigned long dpi = strtoul(display_dpi, &end, 10);
        if (end != display_dpi && !*end && dpi >= 72 && dpi <= 384)
            GProcessState.display_dpi = (unsigned)dpi;
    }
    const char *display_depth = getenv("TWOINE_DISPLAY_DEPTH");
    if (display_depth)
    {
        char *end = NULL;
        unsigned long depth = strtoul(display_depth, &end, 10);
        if (end != display_depth && !*end &&
            (depth == 4 || depth == 8 || depth == 16 || depth == 24))
            GProcessState.display_depth = (unsigned)depth;
    }
    prepOs2Drives();

    const size_t stack_limit = lx_host_stack_limit();
    GProcessState.initOs2Tib(GProcessState.main_tibspace, (void *)&stack_limit, stack_limit, 0);
    GProcessState.main_tib_selector = GProcessState.setOs2Tib(GProcessState.main_tibspace);

    if (getenv("TRACE_NATIVE"))
    {
        GProcessState.trace_native = 1;
    }

    if (getenv("TRACE_EVENTS"))
    {
        GProcessState.trace_events = 1;
    }

    GProcessState.subprocess = (getenv("IS_2INE") != NULL);

    initPib();
}

LX_NATIVE_DESTRUCTOR(lib2ine)
{
    lib2ine_shutdown();
}

void dosExit_lib2ine(uint32 action, uint32 result) {}

int findSelector_lib2ine(const uint32 addr, uint16 *outselector, uint16 *outoffset, int iscode)
{
    return 0; // always fail. This is for 16-bit support.
}

void freeSelector_lib2ine(const uint16 selector)
{
    // no-op. This is for 16-bit support.
}

void *convert1616to32_lib2ine(const uint32 addr1616)
{
    return NULL; // always fail. This is for 16-bit support.
}

uint32 convert32to1616_lib2ine(void *addr32)
{
    return 0; // always fail. This is for 16-bit support.
}

LxModule *loadModule_lib2ine(const char *modname)
{
    return NULL;
}

char *makeUnixPath_lib2ine(const char *os2path, uint32 *err)
{
    // this doesn't convert any OS/2 paths. For native apps, use native paths.
    char *retval = strdup(os2path);
    if (!retval)
    {
        *err = 8;
    }
    return retval;
}

void *allocSegment_lib2ine(uint16 *selector, const int iscode)
{
    if (selector)
        *selector = 0xFFFF;
    return NULL;
}

void freeSegment_lib2ine(const uint16 selector) {}

void __attribute__((noreturn)) terminate_lib2ine(const uint32 exitcode)
{
    exit((int)exitcode);
}

void lib2ine_shutdown(void)
{
    lxAudioShutdown();
    lxUnregisterProcessPeer();

    for (int i = 0; i < (sizeof(GProcessState.disks) / sizeof(GProcessState.disks[0])); i++)
    {
        free(GProcessState.disks[i]);
        GProcessState.disks[i] = NULL;
    }

    if (!GProcessState.execution)
        free(GProcessState.pib.pib_pchenv);
    GProcessState.pib.pib_pchenv = NULL;

    DefaultTib = NULL;
}
