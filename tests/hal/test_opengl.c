/* Public domain. No rights reserved. Wholly AI-authored. */
/* Real Mesa rendering, buffer exchange, texture/list sharing and resize state. */
#include "hal/opengl.h"
#include "hal/gl_constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, #x, lx_host_gl_error());        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void clear(const LxHostGlApi *gl, float r, float g, float b)
{
    gl->glClearColor(r, g, b, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
int main(void)
{
    LxHostGlConfig config = {1, 8, 8, 8, 8, 24, 8};
    LxHostGlContext *first = lx_host_gl_create(&config, NULL);
    CHECK(first);
    CHECK(lx_host_gl_make_current(first, 32, 32));
    const LxHostGlApi *gl = lx_host_gl_api(first);
    printf("Backend: %s; renderer: %s\n", lx_host_gl_backend(), gl->glGetString(GL_RENDERER));
    CHECK(strstr((const char *)gl->glGetString(GL_VERSION), "Compatibility") ||
          gl->glGetString(GL_VERSION)[0] == '2');
    uint32_t pixels[32 * 32];
    CHECK(lx_host_gl_front(first, pixels, 32 * 32));
    CHECK(pixels[0] == 0);
    clear(gl, 0, 0, 1);
    gl->glBegin(GL_TRIANGLES);
    gl->glColor3d(1, 0, 0);
    gl->glVertex2d(-1, -1);
    gl->glVertex2d(1, -1);
    gl->glVertex2d(0, 1);
    gl->glEnd();
    CHECK(gl->glGetError() == GL_NO_ERROR);
    CHECK(lx_host_gl_front(first, pixels, 32 * 32) && pixels[16 * 32 + 16] == 0);
    CHECK(lx_host_gl_swap(first, pixels, 32 * 32));
    CHECK(pixels[16 * 32 + 16] == 0xff0000 && pixels[31 * 32] == 0x0000ff);
    CHECK(lx_host_gl_swap(first, pixels, 32 * 32) && pixels[16 * 32 + 16] == 0);
    uint32_t list = gl->glGenLists(1), texture = 0;
    CHECK(list);
    gl->glNewList(list, GL_COMPILE);
    gl->glClearColor(0, 1, 0, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    gl->glEndList();
    gl->glGenTextures(1, &texture);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    const uint8_t texel[] = {255, 255, 0, 255};
    gl->glTexImage2D(GL_TEXTURE_2D, 0, 4, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    CHECK(texture && gl->glIsTexture(texture) && gl->glGetError() == GL_NO_ERROR);
    LxHostGlContext *second = lx_host_gl_create(&config, first);
    CHECK(second && lx_host_gl_make_current(second, 32, 32));
    gl = lx_host_gl_api(second);
    CHECK(gl->glIsList(list) && gl->glIsTexture(texture));
    gl->glCallList(list);
    CHECK(lx_host_gl_swap(second, pixels, 32 * 32) && pixels[16 * 32 + 16] == 0x00ff00);
    CHECK(lx_host_gl_make_current(second, 16, 16));
    int32_t viewport[4] = {0};
    gl->glGetIntegerv(GL_VIEWPORT, viewport);
    CHECK(viewport[2] == 32 && viewport[3] == 32);
    gl->glViewport(0, 0, 16, 16);
    CHECK(lx_host_gl_draw_buffer(second, GL_FRONT));
    clear(gl, 1, 0, 1);
    CHECK(lx_host_gl_front(second, pixels, 32 * 32) && pixels[0] == 0xff00ff);
    CHECK(lx_host_gl_draw_buffer(second, GL_BACK));
    clear(gl, 0, 1, 1);
    gl->glPixelStorei(GL_PACK_ROW_LENGTH, 64);
    gl->glPixelStorei(GL_PACK_SKIP_ROWS, 7);
    gl->glPixelTransferf(GL_RED_SCALE, 0.5f);
    for (unsigned i = 0; i < 16; i++)
        gl->glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    CHECK(lx_host_gl_swap(second, pixels, 32 * 32) && pixels[0] == 0x00ffff);
    for (unsigned i = 0; i < 16; i++)
        gl->glPopClientAttrib();
    float scale = 0;
    gl->glGetFloatv(GL_RED_SCALE, &scale);
    CHECK(scale == 0.5f);
    int32_t rows = 0;
    gl->glGetIntegerv(GL_PACK_SKIP_ROWS, &rows);
    CHECK(rows == 7 && gl->glGetError() == GL_NO_ERROR);
    CHECK(lx_host_gl_release());
    lx_host_gl_destroy(second);
    lx_host_gl_destroy(first);
    puts("Mesa context, rendering, sharing, buffer exchange and resize contracts passed");
    return 0;
}
