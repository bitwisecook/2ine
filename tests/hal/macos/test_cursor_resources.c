/* Public domain. No rights reserved. Wholly AI-authored. */
/* A controlled native-cursor backend, never the user's desktop/cursor. */
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
#include "guest/handles.h"
#include "hal/host_library.h"
#include "hal/host_thread.h"
#include "hal/video.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #x);                                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
extern unsigned Created, Selected, Destroyed;
void cursor_test_drop_native_objects(void);
int cursor_test_objects_empty(void);
static ULONG (*Load)(HMODULE, PSZ);
static HPOINTER Cursor;

static void *worker(void *unused)
{
    (void)unused;
    CHECK(!pthread_main_np() && !SDL_WasInit(SDL_INIT_VIDEO));
    for (unsigned i = 0; i < 1000; i++)
    {
        HPOINTER value = Load(0, (PSZ)(uintptr_t)32514);
        CHECK(value && (!Cursor || Cursor == value));
        Cursor = value;
    }
    CHECK(!SDL_WasInit(SDL_INIT_VIDEO) && !Created);
    return NULL;
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    char error[512];
    LxHostLibrary *module = lx_host_library_open(argv[1], error, sizeof error);
    CHECK(module);
    LxHostLibrary *pm = lx_host_library_open(argv[2], error, sizeof error);
    CHECK(pm);
    void (*deinit)(void) = (void *)lx_host_library_symbol(pm, "lxNativeModuleDeinit");
    CHECK(deinit);
    Load = (void *)lx_host_library_symbol(module, "LoadCursor");
    HAB (*initialize)(ULONG) = (void *)lx_host_library_symbol(module, "WinInitialize");
    BOOL (*terminate)(HAB) = (void *)lx_host_library_symbol(module, "WinTerminate");
    ULONG (*set)(ULONG) = (void *)lx_host_library_symbol(module, "SetCursor");
    BOOL (*destroy)(ULONG) = (void *)lx_host_library_symbol(module, "DestroyCursor");
    CHECK(Load && initialize && terminate && set && destroy);
    LxHostThread *thread = lx_host_thread_create(worker, NULL, NULL);
    CHECK(thread && lx_host_thread_join(thread, NULL) == 0);
    CHECK(!SDL_WasInit(SDL_INIT_VIDEO) && Cursor);
    HAB anchor = initialize(0);
    CHECK(anchor && SDL_WasInit(SDL_INIT_VIDEO));
    set(Cursor);
    if (Created != 1 || Selected != 1)
        fprintf(stderr, "Controlled cursor backend created=%u selected=%u SDL=%s\n", Created,
                Selected, SDL_GetError());
    CHECK(Created == 1 && Selected == 1 && !destroy(Cursor));
    CHECK(terminate(anchor));
    lxHostThreadCleanup(); /* WinTerminate alone intentionally retains Warp PM thread state. */
    CHECK(!SDL_WasInit(SDL_INIT_VIDEO));
    /* SDL normally frees all its cursors on video shutdown; model that ownership here. */
    cursor_test_drop_native_objects();
    int width, height;
    CHECK(lx_host_video_initialize(&width, &height)); /* Independent display client. */
    anchor = initialize(0);
    CHECK(anchor);
    set(Cursor);
    CHECK(Created == 2 && Selected == 2);
    LxHandleBorrow borrow;
    CHECK(lx_handle_borrow(Cursor, LX_HANDLE_PM_POINTER, &borrow));
    lx_handle_release(&borrow);
    CHECK(terminate(anchor));
    lxHostThreadCleanup();
    lx_host_library_close(module);
    deinit(); /* Real loader module teardown, independent of dyld's TLS unload caching. */
    lx_host_library_close(pm);
    CHECK(!lx_handle_borrow(Cursor, LX_HANDLE_PM_POINTER, &borrow));
    CHECK(Created == Destroyed && cursor_test_objects_empty());
    lx_host_video_quit();
    puts(
        "worker cursor acquisition, shared lifetime, main-thread realization and unload cleanup passed");
    return 0;
}
