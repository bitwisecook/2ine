/* Public domain. No rights reserved. Wholly AI-authored. */
/* Desktop GL contexts render to private color/depth targets. Pixel rows use
   the GL lower-left origin; each returned word is 0x00RRGGBB. No guest types. */
#ifndef TWOINE_HOST_OPENGL_H
#define TWOINE_HOST_OPENGL_H
#include <stddef.h>
#include <stdint.h>
#include "hal/gl_dispatch.h"
typedef struct LxHostGlContext LxHostGlContext;
typedef struct LxHostGlConfig
{
    int double_buffer, red, green, blue, alpha, depth, stencil;
} LxHostGlConfig;
LxHostGlContext *lx_host_gl_create(const LxHostGlConfig *config, LxHostGlContext *share);
void lx_host_gl_destroy(LxHostGlContext *context);
/* Call after all contexts and worker threads have been released. */
void lx_host_gl_shutdown(void);
/* A context may be current on exactly one thread. Unbind before transfer. */
int lx_host_gl_make_current(LxHostGlContext *context, unsigned width, unsigned height);
int lx_host_gl_release(void);
const LxHostGlApi *lx_host_gl_api(LxHostGlContext *context);
const char *lx_host_gl_error(void);
const char *lx_host_gl_backend(void);
int lx_host_gl_swap(LxHostGlContext *context, uint32_t *pixels, size_t count);
int lx_host_gl_front(LxHostGlContext *context, uint32_t *pixels, size_t count);
int lx_host_gl_write_front(LxHostGlContext *context, const uint32_t *pixels, size_t count);
int lx_host_gl_draw_buffer(LxHostGlContext *context, uint32_t buffer);
int lx_host_gl_read_buffer(LxHostGlContext *context, uint32_t buffer);
uint32_t lx_host_gl_buffer(LxHostGlContext *context, int read);
/* Process policy is installed once, before initializing Mesa. */
int lx_host_gl_platform_prepare(char *error, size_t size);
int lx_host_gl_platform_renderer(const char *renderer);
#endif
