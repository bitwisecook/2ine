/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "runtime/core/internal.h"

LxLoaderState GLoaderState;

LX_PRIVATE LX_HOST_THREAD_LOCAL LxTIB *DefaultTib;
LX_PRIVATE LX_HOST_THREAD_LOCAL LxHostThreadState host_thread_state;

LX_NATIVE_CONSTRUCTOR(lib2ine)
{
    memset(&GLoaderState, '\0', sizeof(GLoaderState));
    GLoaderState.dosExit = dosExit_lib2ine;
    GLoaderState.initOs2Tib = initOs2Tib_lib2ine;
    GLoaderState.setOs2Tib = setOs2Tib_lib2ine;
    GLoaderState.getOs2Tib = getOs2Tib_lib2ine;
    GLoaderState.deinitOs2Tib = deinitOs2Tib_lib2ine;
    GLoaderState.allocSegment = allocSegment_lib2ine;
    GLoaderState.freeSegment = freeSegment_lib2ine;
    GLoaderState.findSelector = findSelector_lib2ine;
    GLoaderState.freeSelector = freeSelector_lib2ine;
    GLoaderState.convert1616to32 = convert1616to32_lib2ine;
    GLoaderState.convert32to1616 = convert32to1616_lib2ine;
    GLoaderState.loadModule = loadModule_lib2ine;
    GLoaderState.makeUnixPath = makeUnixPath_lib2ine;
    GLoaderState.terminate = terminate_lib2ine;
    GLoaderState.registerAudioGenerator = lxRegisterAudioGenerator;
    GLoaderState.lib2ine_shutdown = lib2ine_shutdown;

    /* Use a low amplitude to match the PC speaker mix. */
    GLoaderState.beep_volume = 0.05f;

    cfgLoadFiles();
    prepOs2Drives();

    const size_t stack_limit = lx_host_stack_limit();
    GLoaderState.initOs2Tib(GLoaderState.main_tibspace, (void *)&stack_limit, stack_limit, 0);
    GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);

    if (getenv("TRACE_NATIVE"))
    {
        GLoaderState.trace_native = 1;
    }

    if (getenv("TRACE_EVENTS"))
    {
        GLoaderState.trace_events = 1;
    }

    GLoaderState.subprocess = (getenv("IS_2INE") != NULL);

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

    for (int i = 0; i < (sizeof(GLoaderState.disks) / sizeof(GLoaderState.disks[0])); i++)
    {
        free(GLoaderState.disks[i]);
        GLoaderState.disks[i] = NULL;
    }

    if (!GLoaderState.execution)
        free(GLoaderState.pib.pib_pchenv);
    GLoaderState.pib.pib_pchenv = NULL;

    DefaultTib = NULL;
}
