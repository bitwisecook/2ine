/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/process/internal.h"
#include "host/fatal.h"

LxProcessState GProcessState;

LX_PRIVATE LX_HOST_THREAD_LOCAL LxGuestTib *DefaultTib;
LX_PRIVATE LX_HOST_THREAD_LOCAL LxHostThreadState host_thread_state;

LX_NATIVE_CONSTRUCTOR(lib2ine)
{
    /* The failure-dialog helper needs none of the OS/2 process state below. */
    if (lx_host_fatal_is_dialog())
        return;
    lx_host_fatal_install();
    memset(&GProcessState, '\0', sizeof(GProcessState));
    GProcessState.dosExit = dosExit_lib2ine;
    GProcessState.initOs2Tib = lxInitOs2Tib;
    GProcessState.setOs2Tib = lxSetOs2Tib;
    GProcessState.getOs2Tib = lxGetOs2Tib;
    GProcessState.deinitOs2Tib = lxDeinitOs2Tib;
    GProcessState.allocSegment = lxAllocSegment;
    GProcessState.freeSegment = lxFreeSegment;
    GProcessState.createCodeAlias = lxCreateCodeAlias;
    GProcessState.findSelector = lxFindSelector;
    GProcessState.freeSelector = lxFreeSelector;
    GProcessState.convert1616to32 = lxConvert1616to32;
    GProcessState.convert32to1616 = lxConvert32to1616;
    GProcessState.loadModule = loadModule_lib2ine;
    GProcessState.makeUnixPath = lxProcessMakeUnixPath;
    GProcessState.terminate = terminate_lib2ine;
    GProcessState.registerAudioGenerator = lxRegisterAudioGenerator;
    GProcessState.registerStereoAudioGenerator = lxRegisterStereoAudioGenerator;
    GProcessState.lib2ine_shutdown = lib2ine_shutdown;

    /* Use a low amplitude to match the PC speaker mix. */
    GProcessState.beep_volume = 0.05f;
    GProcessState.display_dpi = LX_DEFAULT_DISPLAY_DPI;
    GProcessState.display_depth = LX_DEFAULT_DISPLAY_DEPTH;

    cfgLoadFiles();
    const char *display_dpi = getenv("TWOINE_DISPLAY_DPI");
    if (display_dpi)
    {
        char *end = NULL;
        unsigned long dpi = strtoul(display_dpi, &end, 10);
        if (end != display_dpi && !*end && dpi >= LX_MIN_DISPLAY_DPI && dpi <= LX_MAX_DISPLAY_DPI)
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

struct LxModule *loadModule_lib2ine(const char *modname)
{
    return NULL;
}

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
        free(GProcessState.pib.environment);
    GProcessState.pib.environment = NULL;

    DefaultTib = NULL;
}
