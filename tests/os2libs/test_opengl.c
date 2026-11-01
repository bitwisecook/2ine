/* Public domain. No rights reserved. Wholly AI-authored. */
/* PGL rendering reaches retained PM pixels at the client window's origin. */
#include "os2libs/opengl/internal.h"
#include "os2libs/pmwin/graphics.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static uint32_t pgl(const char *name, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint8_t args[16];
    lx_guest_store_u32(args, a);
    lx_guest_store_u32(args + 4, b);
    lx_guest_store_u32(args + 8, c);
    lx_guest_store_u32(args + 12, d);
    for (size_t i = 0; i < GlPglEntryCount; i++)
        if (!strcmp(GlPglEntries[i].name, name))
            return GlPglEntries[i].call(args);
    abort();
}
int main(void)
{
    char error[256];
    GLoaderState.execution = lx_execution_create(TWOINE_PORTABLE_LOADER ? "unicorn" : "native",
                                                 NULL, error, sizeof error);
    CHECK(GLoaderState.execution);
    CHECK(gl_pgl_initialize());
    HAB hab = WinInitialize(0);
    CHECK(hab && WinCreateMsgQueue(hab, 0));
    HWND parent = WinCreateWindow(HWND_DESKTOP, "#1", "GL PM", 0x80000000, 0, 0, 48, 48,
                                  HWND_DESKTOP, HWND_TOP, 1, NULL, NULL);
    CHECK(parent);
    HPS ps = WinGetPS(parent);
    RECTL bounds = {0, 0, 48, 48};
    CHECK(ps && WinFillRect(ps, &bounds, CLR_BLUE) && WinReleasePS(ps));
    HWND child = WinCreateWindow(parent, "#1", "", 0x80000000, 8, 8, 16, 16, parent, HWND_TOP, 2,
                                 NULL, NULL);
    CHECK(child);
    LxGuestMapping *data = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    CHECK(data);
    uint32_t address = lx_execution_mapping_address(data);
    uint8_t *host = lx_execution_mapping_host(data);
    uint32_t attrs[] = {4, 5, 12, 24, 0};
    for (unsigned i = 0; i < 5; i++)
        lx_guest_store_u32(host + 4 * i, attrs[i]);
    uint32_t config = pgl("pglChooseConfig", hab, address, 0, 0);
    CHECK(config);
    uint32_t context = pgl("pglCreateContext", hab, config, 0, 1);
    CHECK(context && pgl("pglMakeCurrent", hab, context, child, 0));
    GlContext *current = gl_current();
    CHECK(current);
    current->api->glClearColor(0, 1, 0, 1);
    current->api->glClear(GL_COLOR_BUFFER_BIT);
    pgl("pglSwapBuffers", hab, child, 0, 0);
    CHECK(call_glGetError(NULL) == GL_NO_ERROR);
    uint32_t pixel = 0;
    ps = WinGetPS(parent);
    CHECK(ps && lx_pm_raster_read(ps, 16, 16, 1, 1, &pixel) && pixel == 0x00ff00);
    CHECK(lx_pm_raster_read(ps, 4, 4, 1, 1, &pixel) && pixel == 0x0000ff);
    CHECK(WinReleasePS(ps));
    HPS mixed = pgl("pglWaitGL", hab, 0, 0, 0);
    CHECK(mixed);
    RECTL overlay = {2, 2, 6, 6};
    CHECK(WinFillRect(mixed, &overlay, CLR_RED));
    pgl("pglWaitPM", hab, 0, 0, 0);
    CHECK(call_glGetError(NULL) == GL_NO_ERROR);
    uint8_t rgba[4] = {0};
    CHECK(lx_host_gl_read_buffer(current->host, GL_FRONT));
    current->api->glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0);
    CHECK(lx_host_gl_read_buffer(current->host, GL_BACK));
    CHECK(WinSetWindowPos(child, 0, 8, 8, 12, 12, SWP_SIZE));
    CHECK(pgl("pglMakeCurrent", hab, context, child, 0));
    current->api->glClearColor(1, 0, 0, 1);
    current->api->glClear(GL_COLOR_BUFFER_BIT);
    pgl("pglSwapBuffers", hab, child, 0, 0);
    ps = WinGetPS(parent);
    CHECK(ps && lx_pm_raster_read(ps, 16, 16, 1, 1, &pixel) && pixel == 0xff0000);
    CHECK(WinReleasePS(ps));
    CHECK(pgl("pglMakeCurrent", hab, 0, 0, 0));
    CHECK(pgl("pglDestroyContext", hab, context, 0, 0));
    CHECK(WinDestroyWindow(child) && WinDestroyWindow(parent) && WinTerminate(hab));
    gl_pgl_shutdown();
    CHECK(lx_execution_unmap(GLoaderState.execution, data));
    lx_execution_destroy(GLoaderState.execution);
    GLoaderState.execution = NULL;
    puts("PGL context lifecycle, client placement, retained PM presentation and resize passed");
    return 0;
}
