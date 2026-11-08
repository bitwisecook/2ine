/* Public domain. No rights reserved. Wholly AI-authored. */
/* GUI import initialization owns a display before a worker requests memory GPI. */
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi.h"
#include "guest/abi.h"
#include "host/library.h"
#include "host/thread.h"
#include "host/window.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, SDL_GetError());           \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void no_windows(void)
{
    int count = -1;
    SDL_Window **windows = SDL_GetWindows(&count);
    CHECK(windows && !count);
    SDL_free(windows);
}

static void *memory_graphics(void *unused)
{
    (void)unused;
    CHECK(SDL_WasInit(SDL_INIT_VIDEO));
    HAB anchor = WinInitialize(0);
    CHECK(anchor && WinCreateMsgQueue(anchor, 0));
    HDC context = DevOpenDC(anchor, 8, "*", 0, NULL, 0);
    SIZEL size = {2, 2};
    HPS space = GpiCreatePS(anchor, context, &size, 0x5008);
    CHECK(context && space);
    uint8 header[64] = {64};
    lx_guest_store_u32(header + 4, 2);
    lx_guest_store_u32(header + 8, 2);
    lx_guest_store_u16(header + 12, 1);
    lx_guest_store_u16(header + 14, 24);
    uint8 pixels[16] = {0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0};
    ULONG bitmap = GpiCreateBitmap(space, header, 4, pixels, header);
    CHECK(bitmap && !GpiSetBitmap(space, bitmap));
    uint8 result[16] = {0};
    CHECK(GpiQueryBitmapBits(space, 0, 2, result, header) == 2);
    CHECK(!memcmp(result, pixels, sizeof pixels));
    CHECK(GpiSetBitmap(space, 0) == bitmap && GpiDeleteBitmap(bitmap));
    CHECK(GpiDestroyPS(space) && DevCloseDC(context) == 0);
    CHECK(WinTerminate(anchor));
    lxHostThreadCleanup();
    CHECK(!lxHostThreadState()->anchor_block && SDL_WasInit(SDL_INIT_VIDEO));
    return NULL;
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    const int gui =
        !strcmp(argv[2], "gui") || !strcmp(argv[2], "retained") || !strcmp(argv[2], "invalid");
    const int invalid = !strcmp(argv[2], "invalid");
    const int retained = !strcmp(argv[2], "retained");
    CHECK(gui || !strcmp(argv[2], "vio") || !strcmp(argv[2], "compatible") ||
          !strcmp(argv[2], "ne") || !strcmp(argv[2], "none"));
    CHECK(!SDL_WasInit(SDL_INIT_VIDEO) && !lxHostThreadState()->anchor_block);
    LxModule program = {0};
    program.is_lx = strcmp(argv[2], "ne") != 0;
    program.header.lx.module_flags = gui || !program.is_lx            ? 0x0300
                                     : !strcmp(argv[2], "compatible") ? 0x0200
                                                                      : 0;
    strcpy(program.name, "PMSTARTUP");
    GLoaderState.main_module = !strcmp(argv[2], "none") ? NULL : &program;
    char error[512];
    GLoaderState.execution = lx_execution_create("unicorn", NULL, error, sizeof error);
    if (!GLoaderState.execution)
        fprintf(stderr, "Execution initialization failed: %s\n", error);
    CHECK(GLoaderState.execution);
    LxHostLibrary *module = lx_host_library_open(argv[1], error, sizeof error);
    CHECK(module);
    LxNativeModuleInitEntryPoint initialize =
        (LxNativeModuleInitEntryPoint)lx_host_library_symbol(module, "lxNativeModuleInit");
    LxNativeModuleDeinitEntryPoint deinitialize =
        (LxNativeModuleDeinitEntryPoint)lx_host_library_symbol(module, "lxNativeModuleDeinit");
    CHECK(initialize && deinitialize);
    int width = 0, height = 0;
    if (retained)
        CHECK(lx_host_video_initialize(&width, &height));
    uint32 count = 0;
    const LxExport *exports = initialize(&count);
    CHECK(!lxHostThreadState()->anchor_block);
    if (invalid)
    {
        CHECK(!exports && !SDL_WasInit(SDL_INIT_VIDEO));
        CHECK(strstr(lx_host_video_error(), "not-a-video-driver"));
    }
    else
    {
        CHECK(exports && count);
        CHECK(!!SDL_WasInit(SDL_INIT_VIDEO) == gui);
        if (gui)
        {
            no_windows();
            LxHostThread *worker = lx_host_thread_create(memory_graphics, NULL, NULL);
            CHECK(worker && lx_host_thread_join(worker, NULL) == 0);
            CHECK(!lxHostThreadState()->anchor_block);
            no_windows();
        }
        deinitialize();
        CHECK(!!SDL_WasInit(SDL_INIT_VIDEO) == retained);
        if (retained)
        {
            no_windows();
            lx_host_video_quit();
            CHECK(!SDL_WasInit(SDL_INIT_VIDEO));
        }
    }
    GLoaderState.main_module = NULL;
    lx_host_library_close(module);
    lx_execution_destroy(GLoaderState.execution);
    GLoaderState.execution = NULL;
    printf("PM display startup %s: reference balance, no eager HAB or host windows passed\n",
           argv[2]);
    return 0;
}
