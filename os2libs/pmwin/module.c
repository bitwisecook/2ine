/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"

static int ProcessDisplayReference;

static int initializeProcessDisplay(void)
{
    const LxModule *program = GProcessState.main_module;
    /* GUI imports load before guest DLL initialization can create PM threads. */
    if (ProcessDisplayReference || !program || !program->is_lx ||
        (program->header.lx.module_flags & 0x0300) != 0x0300)
        return 1;
    if (!lx_host_video_initialize(&desktop_window.w, &desktop_window.h))
    {
        fprintf(stderr, "PM process display initialization failed: %s\n", lx_host_video_error());
        return 0;
    }
    desktop_window.hwnd = HWND_DESKTOP;
    ProcessDisplayReference = 1;
    TRACE_NATIVE("PM process display prepared('%s')", program->name);
    return 1;
}

#undef LX_NATIVE_MODULE_INIT
#define LX_NATIVE_MODULE_INIT(initcode)                                                            \
    const LxExport *lxNativeModuleInit(uint32 *lx_num_exports)                                     \
    {                                                                                              \
        initcode;                                                                                  \
        if (!initializeProcessDisplay())                                                           \
        {                                                                                          \
            lxNativeModuleDeinit();                                                                \
            return NULL;                                                                           \
        }                                                                                          \
        static const LxExport lx_native_exports[] = {
#undef LX_NATIVE_MODULE_DEINIT
#define LX_NATIVE_MODULE_DEINIT(code)                                                              \
    void lxNativeModuleDeinit(void)                                                                \
    {                                                                                              \
        destroySharedPointers();                                                                   \
        code;                                                                                      \
        if (ProcessDisplayReference)                                                               \
        {                                                                                          \
            ProcessDisplayReference = 0;                                                           \
            lx_host_video_quit();                                                                  \
        }                                                                                          \
    }
#include "os2libs/generated/pmwin-lx.h"

LX_PRIVATE uint32 GHABCounter;
LX_PRIVATE uint32 GHMQCounter;

LX_PRIVATE Window desktop_window;

PFNWP defaultGuestWindowProc(void)
{
#if TWOINE_PORTABLE_LOADER && LX_LEGACY
    if (GProcessState.execution && obj32.mapping && bridge_address_WinDefWindowProc)
        return (PFNWP)(uintptr_t)(obj32.guest_base +
                                  (uint32)((uint8 *)bridge_address_WinDefWindowProc -
                                           (uint8 *)obj32.addr));
#endif
    return WinDefWindowProc;
}

PFNWP defaultGuestDialogProc(void)
{
#if TWOINE_PORTABLE_LOADER && LX_LEGACY
    if (GProcessState.execution && obj32.mapping && bridge_address_WinDefDlgProc)
        return (PFNWP)(uintptr_t)(obj32.guest_base +
                                  (uint32)((uint8 *)bridge_address_WinDefDlgProc -
                                           (uint8 *)obj32.addr));
#endif
    return WinDefDlgProc;
}
