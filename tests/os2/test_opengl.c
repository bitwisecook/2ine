/* Public domain. No rights reserved. Wholly AI-authored. */
/* PGL rendering reaches retained PM pixels at the client window's origin. */
#include "os2/opengl/opengl/internal.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/include/pmgpi.h"
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

static uint32_t pgl_values(const char *name, const uint32_t *values, unsigned count)
{
    uint8_t args[28] = {0};
    CHECK(count <= 7);
    for (unsigned i = 0; i < count; i++)
        lx_guest_store_u32(args + i * 4, values[i]);
    for (size_t i = 0; i < GlPglEntryCount; i++)
        if (!strcmp(GlPglEntries[i].name, name))
            return GlPglEntries[i].call(args);
    abort();
}

static uint32_t pgl(const char *name, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t values[] = {a, b, c, d};
    return pgl_values(name, values, 4);
}

int main(void)
{
    char error[256];
    GProcessState.execution = lx_execution_create(TWOINE_PORTABLE_LOADER ? "unicorn" : "native",
                                                  NULL, error, sizeof error);
    CHECK(GProcessState.execution);
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
    LxGuestMapping *data = lx_execution_map(GProcessState.execution, 0, 4096, 3, 0);
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
    uint32_t duplicate = pgl("pglCreateContext", hab, config, context, 1);
    CHECK(duplicate);
    current = gl_current();
    CHECK(current);
    current->api->glDepthFunc(GL_GREATER);
    current->api->glEnable(GL_DEPTH_TEST);
    current->api->glColor4d(0.25, 0.5, 0.75, 1);
    current->api->glRasterPos3d(0.5, 0.5, 0);
    double raster[4];
    current->api->glGetDoublev(GL_CURRENT_RASTER_POSITION, raster);
    CHECK(pgl("pglCopyContext", hab, context, duplicate, GL_ALL_ATTRIB_BITS));
    CHECK(pgl("pglGetCurrentContext", hab, 0, 0, 0) == context);
    CHECK(pgl("pglMakeCurrent", hab, duplicate, child, 0));
    GlContext *copy = gl_current();
    CHECK(copy && copy->api->glIsEnabled(GL_DEPTH_TEST));
    int32_t depth_func = 0;
    copy->api->glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
    CHECK(depth_func == GL_GREATER);
    double color[4] = {0}, copied_raster[4] = {0};
    copy->api->glGetDoublev(GL_CURRENT_COLOR, color);
    CHECK(color[0] == 0.25 && color[1] == 0.5 && color[2] == 0.75);
    copy->api->glGetDoublev(GL_CURRENT_RASTER_POSITION, copied_raster);
    CHECK(copied_raster[0] == raster[0] && copied_raster[1] == raster[1]);
    CHECK(copy->api->glGetError() == GL_NO_ERROR);
    CHECK(pgl("pglMakeCurrent", hab, context, child, 0));
    CHECK(pgl("pglDestroyContext", hab, duplicate, 0, 0));
    current = gl_current();
    current->api->glDisable(GL_DEPTH_TEST);
    CHECK(pgl("pglGrabFrontBitmap", hab, address + 512, address + 516, 0));
    HPS bitmap_ps = lx_guest_load_u32(host + 512);
    ULONG bitmap_handle = lx_guest_load_u32(host + 516);
    CHECK(bitmap_ps && bitmap_handle);
    uint32_t bitmap_pixel = 0;
    CHECK(lx_pm_raster_read(bitmap_ps, 8, 8, 1, 1, &bitmap_pixel) && bitmap_pixel == 0x00ff00);
    CHECK(!pgl("pglGrabFrontBitmap", hab, address + 512, address + 516, 0));
    CHECK(call_glGetError(NULL) == GL_INVALID_OPERATION);
    CHECK(pgl("pglReleaseFrontBitmap", hab, 0, 0, 0));
    CHECK(!pgl("pglReleaseFrontBitmap", hab, 0, 0, 0));
    LxHandleBorrow bitmap_borrow = {0};
    CHECK(!lx_pm_bitmap(bitmap_handle, &bitmap_borrow));
    uint32_t font_args[] = {hab, 0, 'A', 1, 300};
    CHECK(pgl_values("pglUsePMBitmapFont", font_args, 5));
    current = gl_current();
    CHECK(current && current->api->glIsList(300));
    current->api->glClearColor(0, 0, 0, 1);
    current->api->glClear(GL_COLOR_BUFFER_BIT);
    current->api->glColor3f(1, 1, 1);
    current->api->glRasterPos2f(-0.75, -0.75);
    current->api->glCallList(300);
    uint8_t glyph_pixels[16 * 16 * 4] = {0};
    current->api->glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, glyph_pixels);
    unsigned lit = 0;
    for (unsigned n = 0; n < 16 * 16; n++)
        lit += glyph_pixels[n * 4] != 0;
    CHECK(lit > 0 && lit < 128 && current->api->glGetError() == GL_NO_ERROR);
    ps = WinGetPS(child);
    CHECK(ps);
    FATTRS *font_attributes = (FATTRS *)(host + 1024);
    memset(font_attributes, 0, sizeof *font_attributes);
    font_attributes->usRecordLength = sizeof *font_attributes;
    font_attributes->lMaxBaselineExt = 12;
    strcpy(font_attributes->szFacename, "Helv");
    uint32_t logical_font_args[] = {hab, ps, address + 1024, 20, 'B', 1, 301};
    CHECK(pgl_values("pglUseFont", logical_font_args, 7));
    current = gl_current();
    CHECK(current && current->api->glIsList(301) && current->api->glGetError() == GL_NO_ERROR);
    CHECK(GpiQueryCharSet(ps) == 0);
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
    CHECK(lx_execution_unmap(GProcessState.execution, data));
    lx_execution_destroy(GProcessState.execution);
    GProcessState.execution = NULL;
    GProcessState.execution = NULL;
    puts("PGL context lifecycle, client placement, retained PM presentation and resize passed");
    return 0;
}
