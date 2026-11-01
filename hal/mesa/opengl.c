/* Public domain. No rights reserved. Wholly AI-authored. */
/* EGL owns the desktop compatibility context. Private framebuffer attachments
   implement front/back semantics independently of the host window system. */
#include "hal/opengl.h"
#include "hal/video.h"
#include "hal/host_sync.h"
#include "hal/host_thread.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "hal/gl_constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t GLenum, GLuint;
typedef int32_t GLint;
/* Host framebuffer tokens are not exposed by the guest's GL 1.1 interface. */
enum
{
    GL_FRAMEBUFFER = 0x8D40,
    GL_RENDERBUFFER = 0x8D41,
    GL_READ_FRAMEBUFFER = 0x8CA8,
    GL_DRAW_FRAMEBUFFER = 0x8CA9,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
    GL_RENDERBUFFER_BINDING = 0x8CA7,
    GL_COLOR_ATTACHMENT0 = 0x8CE0,
    GL_COLOR_ATTACHMENT1 = 0x8CE1,
    GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    GL_DEPTH24_STENCIL8 = 0x88F0
};

struct LxHostGlContext
{
    EGLContext context;
    EGLSurface surface;
    LxHostGlApi api;
    void (*gen_framebuffers)(int32_t, uint32_t *);
    void (*delete_framebuffers)(int32_t, const uint32_t *);
    void (*bind_framebuffer)(uint32_t, uint32_t);
    uint32_t (*framebuffer_status)(uint32_t);
    void (*gen_renderbuffers)(int32_t, uint32_t *);
    void (*delete_renderbuffers)(int32_t, const uint32_t *);
    void (*bind_renderbuffer)(uint32_t, uint32_t);
    void (*renderbuffer_storage)(uint32_t, uint32_t, int32_t, int32_t);
    void (*framebuffer_renderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*draw_buffers)(int32_t, const uint32_t *);
    void (*framebuffer_texture)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    void (*blit_framebuffer)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                             uint32_t, uint32_t);
    GLuint framebuffer, colors[2], depth;
    unsigned width, height, front;
    int double_buffer, validated;
    GLenum draw, read;
};
static EGLDisplay display = EGL_NO_DISPLAY;
static EGLConfig config;
static LxHostOnce initialized = LX_HOST_ONCE_INIT;
static char initialization_error[256];
static LX_HOST_THREAD_LOCAL char error_text[256];
static void release_for_video(void)
{
    if (display != EGL_NO_DISPLAY && eglGetCurrentContext() != EGL_NO_CONTEXT)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static int fail(const char *message)
{
    snprintf(error_text, sizeof error_text, "%s (EGL error 0x%x)", message, eglGetError());
    return 0;
}
const char *lx_host_gl_error(void)
{
    return error_text;
}
static void initialize(void)
{
    if (!lx_host_gl_platform_prepare(initialization_error, sizeof initialization_error))
        return;
    EGLDisplay candidate =
        eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint major, minor, count;
    const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                 EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_BIT,
                                 EGL_RED_SIZE,
                                 8,
                                 EGL_GREEN_SIZE,
                                 8,
                                 EGL_BLUE_SIZE,
                                 8,
                                 EGL_ALPHA_SIZE,
                                 8,
                                 EGL_NONE};
    if (candidate == EGL_NO_DISPLAY || !eglInitialize(candidate, &major, &minor))
        fail("Cannot initialize surfaceless Mesa EGL");
    else
    {
        const char *vendor = eglQueryString(candidate, EGL_VENDOR);
        if (!vendor || !strstr(vendor, "Mesa"))
            fail("The OpenGL adapter requires Mesa EGL");
        else if (!eglChooseConfig(candidate, attributes, &config, 1, &count) || count != 1)
            fail("Mesa has no RGBA desktop OpenGL configuration");
        else
        {
            display = candidate;
            lx_host_video_set_graphics_release(release_for_video);
            return;
        }
        eglTerminate(candidate);
    }
    snprintf(initialization_error, sizeof initialization_error, "%s", error_text);
}

static int load_api(LxHostGlContext *c)
{
#define LOAD_GL(name)                                                                              \
    do                                                                                             \
    {                                                                                              \
        __eglMustCastToProperFunctionPointerType proc = eglGetProcAddress(#name);                  \
        if (!proc)                                                                                 \
            return fail("Mesa is missing " #name);                                                 \
        memcpy(&c->api.name, &proc, sizeof c->api.name);                                           \
    } while (0)
#include "gl_load.h"
#undef LOAD_GL
#define LOAD_PRIVATE(member, name)                                                                 \
    do                                                                                             \
    {                                                                                              \
        __eglMustCastToProperFunctionPointerType proc = eglGetProcAddress(name);                   \
        if (!proc)                                                                                 \
            return fail("Mesa is missing " name);                                                  \
        memcpy(&c->member, &proc, sizeof c->member);                                               \
    } while (0)
    LOAD_PRIVATE(gen_framebuffers, "glGenFramebuffers");
    LOAD_PRIVATE(delete_framebuffers, "glDeleteFramebuffers");
    LOAD_PRIVATE(bind_framebuffer, "glBindFramebuffer");
    LOAD_PRIVATE(framebuffer_status, "glCheckFramebufferStatus");
    LOAD_PRIVATE(gen_renderbuffers, "glGenRenderbuffers");
    LOAD_PRIVATE(delete_renderbuffers, "glDeleteRenderbuffers");
    LOAD_PRIVATE(bind_renderbuffer, "glBindRenderbuffer");
    LOAD_PRIVATE(renderbuffer_storage, "glRenderbufferStorage");
    LOAD_PRIVATE(framebuffer_renderbuffer, "glFramebufferRenderbuffer");
    LOAD_PRIVATE(draw_buffers, "glDrawBuffers");
    LOAD_PRIVATE(framebuffer_texture, "glFramebufferTexture2D");
    LOAD_PRIVATE(blit_framebuffer, "glBlitFramebuffer");
#undef LOAD_PRIVATE
    return 1;
}

LxHostGlContext *lx_host_gl_create(const LxHostGlConfig *requested, LxHostGlContext *share)
{
    lx_host_once(&initialized, initialize);
    if (display == EGL_NO_DISPLAY)
    {
        snprintf(error_text, sizeof error_text, "%s", initialization_error);
        return NULL;
    }
    if (!requested || !eglBindAPI(EGL_OPENGL_API))
    {
        fail("Cannot bind desktop OpenGL");
        return NULL;
    }
    LxHostGlContext *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1,
                                 EGL_NONE};
    const EGLint surface[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    c->context =
        eglCreateContext(display, config, share ? share->context : EGL_NO_CONTEXT, attributes);
    c->surface = eglCreatePbufferSurface(display, config, surface);
    c->double_buffer = !!requested->double_buffer;
    c->draw = c->read = c->double_buffer ? GL_BACK : GL_FRONT;
    if (c->context == EGL_NO_CONTEXT || c->surface == EGL_NO_SURFACE || !load_api(c))
    {
        fail("Cannot create Mesa compatibility context");
        lx_host_gl_destroy(c);
        return NULL;
    }
    return c;
}
void lx_host_gl_destroy(LxHostGlContext *c)
{
    if (!c)
        return;
    /* Renderbuffers are shareable objects. Delete private targets explicitly
       before destroying one context in a still-live share group. */
    if (c->framebuffer)
    {
        EGLContext previous = eglGetCurrentContext();
        EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
        if (eglMakeCurrent(display, c->surface, c->surface, c->context))
        {
            c->delete_framebuffers(1, &c->framebuffer);
            c->delete_renderbuffers(2, c->colors);
            c->delete_renderbuffers(1, &c->depth);
            eglMakeCurrent(display, draw, read, previous);
        }
    }
    if (c->context != EGL_NO_CONTEXT)
        eglDestroyContext(display, c->context);
    if (c->surface != EGL_NO_SURFACE)
        eglDestroySurface(display, c->surface);
    free(c);
}
void lx_host_gl_shutdown(void)
{
    lx_host_video_set_graphics_release(NULL);
    if (display != EGL_NO_DISPLAY)
        eglTerminate(display);
    display = EGL_NO_DISPLAY;
    initialized = (LxHostOnce)LX_HOST_ONCE_INIT;
    initialization_error[0] = 0;
}
const LxHostGlApi *lx_host_gl_api(LxHostGlContext *c)
{
    return c ? &c->api : NULL;
}
static GLenum attachment(LxHostGlContext *c, GLenum buffer)
{
    return GL_COLOR_ATTACHMENT0 +
           ((buffer == GL_BACK || buffer == GL_BACK_LEFT) ? 1 - c->front : c->front);
}
int lx_host_gl_draw_buffer(LxHostGlContext *c, uint32_t buffer)
{
    if (buffer == GL_FRONT_AND_BACK)
    {
        const GLenum targets[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        c->draw_buffers(c->double_buffer ? 2 : 1, targets);
    }
    else if (buffer == GL_NONE)
        c->api.glDrawBuffer(GL_NONE);
    else if (buffer == GL_FRONT || buffer == GL_FRONT_LEFT ||
             (c->double_buffer && (buffer == GL_BACK || buffer == GL_BACK_LEFT)))
        c->api.glDrawBuffer(attachment(c, buffer));
    else
        return 0;
    c->draw = buffer;
    return 1;
}
int lx_host_gl_read_buffer(LxHostGlContext *c, uint32_t buffer)
{
    if (buffer != GL_FRONT && buffer != GL_FRONT_LEFT &&
        !(c->double_buffer && (buffer == GL_BACK || buffer == GL_BACK_LEFT)))
        return 0;
    c->api.glReadBuffer(attachment(c, buffer));
    c->read = buffer;
    return 1;
}
uint32_t lx_host_gl_buffer(LxHostGlContext *c, int read)
{
    int32_t actual = 0;
    c->api.glGetIntegerv(read ? GL_READ_BUFFER : GL_DRAW_BUFFER, &actual);
    GLenum *cached = read ? &c->read : &c->draw;
    if ((GLenum)actual == GL_NONE)
        *cached = GL_NONE;
    else if ((GLenum)actual == GL_COLOR_ATTACHMENT0 && *cached != GL_FRONT &&
             *cached != GL_FRONT_LEFT && *cached != GL_FRONT_AND_BACK)
        *cached = GL_FRONT;
    else if ((GLenum)actual == GL_COLOR_ATTACHMENT1 && *cached != GL_BACK &&
             *cached != GL_BACK_LEFT)
        *cached = GL_BACK;
    return *cached;
}

static int resize(LxHostGlContext *c, unsigned width, unsigned height)
{
    GLuint framebuffer = 0, colors[2] = {0}, depth = 0;
    GLint previous_renderbuffer = 0;
    c->api.glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_renderbuffer);
    c->gen_framebuffers(1, &framebuffer);
    c->bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
    c->gen_renderbuffers(2, colors);
    for (unsigned i = 0; i < 2; i++)
    {
        c->bind_renderbuffer(GL_RENDERBUFFER, colors[i]);
        c->renderbuffer_storage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        c->framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER,
                                    colors[i]);
    }
    c->gen_renderbuffers(1, &depth);
    c->bind_renderbuffer(GL_RENDERBUFFER, depth);
    c->renderbuffer_storage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    c->framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                depth);
    c->bind_renderbuffer(GL_RENDERBUFFER, previous_renderbuffer);
    if (c->framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        c->delete_framebuffers(1, &framebuffer);
        c->delete_renderbuffers(2, colors);
        c->delete_renderbuffers(1, &depth);
        c->bind_framebuffer(GL_FRAMEBUFFER, c->framebuffer);
        return fail("Mesa cannot allocate the drawable framebuffer");
    }
    int first = !c->width;
    c->delete_framebuffers(1, &c->framebuffer);
    c->delete_renderbuffers(2, c->colors);
    c->delete_renderbuffers(1, &c->depth);
    c->framebuffer = framebuffer;
    memcpy(c->colors, colors, sizeof colors);
    c->depth = depth;
    c->width = width;
    c->height = height;
    if (first)
        c->api.glViewport(0, 0, width, height);
    float clear_color[4];
    double clear_depth;
    int32_t clear_stencil, stencil_mask;
    uint8_t color_mask[4], depth_mask;
    int scissor = c->api.glIsEnabled(GL_SCISSOR_TEST);
    c->api.glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);
    c->api.glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
    c->api.glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clear_stencil);
    c->api.glGetIntegerv(GL_STENCIL_WRITEMASK, &stencil_mask);
    c->api.glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    c->api.glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    const GLenum targets[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    c->draw_buffers(2, targets);
    c->api.glDisable(GL_SCISSOR_TEST);
    c->api.glColorMask(1, 1, 1, 1);
    c->api.glDepthMask(1);
    c->api.glStencilMask(UINT32_MAX);
    c->api.glClearColor(0, 0, 0, 0);
    c->api.glClearDepth(1);
    c->api.glClearStencil(0);
    c->api.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    c->api.glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    c->api.glClearDepth(clear_depth);
    c->api.glClearStencil(clear_stencil);
    c->api.glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    c->api.glDepthMask(depth_mask);
    c->api.glStencilMask(stencil_mask);
    if (scissor)
        c->api.glEnable(GL_SCISSOR_TEST);
    lx_host_gl_draw_buffer(c, c->draw);
    lx_host_gl_read_buffer(c, c->read);
    return 1;
}
int lx_host_gl_make_current(LxHostGlContext *c, unsigned width, unsigned height)
{
    if (!c || !width || !height || width > 16384 || height > 16384 ||
        (uint64_t)width * height > 16777216)
        return fail("Invalid GL drawable dimensions");
    if (!lx_host_video_release_graphics() || !eglBindAPI(EGL_OPENGL_API) ||
        !eglMakeCurrent(display, c->surface, c->surface, c->context))
        return fail("Cannot make the Mesa context current");
    if (!c->validated)
    {
        const char *renderer = (const char *)c->api.glGetString(GL_RENDERER);
        if (!lx_host_gl_platform_renderer(renderer))
        {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            return fail("Mesa selected a renderer incompatible with the configured backend");
        }
        c->validated = 1;
    }
    if (width != c->width || height != c->height)
        return resize(c, width, height);
    c->bind_framebuffer(GL_FRAMEBUFFER, c->framebuffer);
    return 1;
}
int lx_host_gl_release(void)
{
    return display != EGL_NO_DISPLAY &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

typedef struct PixelState
{
    int32_t store[6], map_color;
    float transfer[8];
} PixelState;
static const GLenum PixelPack[] = {GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,   GL_PACK_ROW_LENGTH,
                                   GL_PACK_SKIP_ROWS,  GL_PACK_SKIP_PIXELS, GL_PACK_ALIGNMENT};
static const GLenum PixelUnpack[] = {GL_UNPACK_SWAP_BYTES,  GL_UNPACK_LSB_FIRST,
                                     GL_UNPACK_ROW_LENGTH,  GL_UNPACK_SKIP_ROWS,
                                     GL_UNPACK_SKIP_PIXELS, GL_UNPACK_ALIGNMENT};
static const GLenum PixelTransfer[] = {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE,
                                       GL_RED_BIAS,  GL_GREEN_BIAS,  GL_BLUE_BIAS,  GL_ALPHA_BIAS};
static void save_pixels(LxHostGlContext *c, PixelState *state, int pack)
{
    const GLenum *store = pack ? PixelPack : PixelUnpack;
    for (unsigned i = 0; i < 6; i++)
    {
        c->api.glGetIntegerv(store[i], &state->store[i]);
        c->api.glPixelStorei(store[i], i == 5 ? 4 : 0);
    }
    c->api.glGetIntegerv(GL_MAP_COLOR, &state->map_color);
    c->api.glPixelTransferi(GL_MAP_COLOR, 0);
    for (unsigned i = 0; i < 8; i++)
    {
        c->api.glGetFloatv(PixelTransfer[i], &state->transfer[i]);
        c->api.glPixelTransferf(PixelTransfer[i], i < 4 ? 1 : 0);
    }
}
static void restore_pixels(LxHostGlContext *c, const PixelState *state, int pack)
{
    const GLenum *store = pack ? PixelPack : PixelUnpack;
    for (unsigned i = 0; i < 6; i++)
        c->api.glPixelStorei(store[i], state->store[i]);
    for (unsigned i = 0; i < 8; i++)
        c->api.glPixelTransferf(PixelTransfer[i], state->transfer[i]);
    c->api.glPixelTransferi(GL_MAP_COLOR, state->map_color);
}

int lx_host_gl_front(LxHostGlContext *c, uint32_t *pixels, size_t count)
{
    size_t size = (size_t)c->width * c->height;
    if (!pixels || count < size)
        return 0;
    uint8_t *rgba = malloc(size * 4);
    if (!rgba)
        return 0;
    lx_host_gl_buffer(c, 1);
    PixelState state;
    save_pixels(c, &state, 1);
    c->api.glReadBuffer(GL_COLOR_ATTACHMENT0 + c->front);
    c->api.glReadPixels(0, 0, c->width, c->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    lx_host_gl_read_buffer(c, c->read);
    restore_pixels(c, &state, 1);
    for (size_t i = 0; i < size; i++)
        pixels[i] = (uint32_t)rgba[4 * i] << 16 | (uint32_t)rgba[4 * i + 1] << 8 | rgba[4 * i + 2];
    free(rgba);
    return 1;
}
int lx_host_gl_write_front(LxHostGlContext *c, const uint32_t *pixels, size_t count)
{
    size_t size = (size_t)c->width * c->height;
    if (!pixels || count < size)
        return 0;
    uint8_t *rgba = malloc(size * 4);
    if (!rgba)
        return 0;
    for (size_t i = 0; i < size; i++)
    {
        rgba[4 * i] = pixels[i] >> 16;
        rgba[4 * i + 1] = pixels[i] >> 8;
        rgba[4 * i + 2] = pixels[i];
        rgba[4 * i + 3] = 255;
    }
    PixelState state;
    save_pixels(c, &state, 0);
    uint32_t draw = lx_host_gl_buffer(c, 0), read = lx_host_gl_buffer(c, 1);
    int32_t previous_texture = 0;
    c->api.glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    int scissor = c->api.glIsEnabled(GL_SCISSOR_TEST);
    GLuint texture = 0, framebuffer = 0;
    c->api.glGenTextures(1, &texture);
    c->api.glBindTexture(GL_TEXTURE_2D, texture);
    c->api.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c->width, c->height, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba);
    c->gen_framebuffers(1, &framebuffer);
    c->bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
    c->framebuffer_texture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    int complete = c->framebuffer_status(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete)
    {
        c->api.glReadBuffer(GL_COLOR_ATTACHMENT0);
        c->bind_framebuffer(GL_DRAW_FRAMEBUFFER, c->framebuffer);
        c->api.glDrawBuffer(GL_COLOR_ATTACHMENT0 + c->front);
        c->api.glDisable(GL_SCISSOR_TEST);
        c->blit_framebuffer(0, 0, c->width, c->height, 0, 0, c->width, c->height,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    c->bind_framebuffer(GL_FRAMEBUFFER, c->framebuffer);
    lx_host_gl_draw_buffer(c, draw);
    lx_host_gl_read_buffer(c, read);
    if (scissor)
        c->api.glEnable(GL_SCISSOR_TEST);
    c->api.glBindTexture(GL_TEXTURE_2D, previous_texture);
    c->api.glDeleteTextures(1, &texture);
    c->delete_framebuffers(1, &framebuffer);
    restore_pixels(c, &state, 0);
    free(rgba);
    return complete;
}
int lx_host_gl_swap(LxHostGlContext *c, uint32_t *pixels, size_t count)
{
    if (c->double_buffer)
    {
        /* Keep attachment names stable for display lists and attribute stacks,
           exchanging their storage to implement the logical buffer swap. */
        GLuint first = c->colors[0];
        c->colors[0] = c->colors[1];
        c->colors[1] = first;
        for (unsigned i = 0; i < 2; i++)
            c->framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER,
                                        c->colors[i]);
    }
    lx_host_gl_draw_buffer(c, c->draw);
    lx_host_gl_read_buffer(c, c->read);
    return lx_host_gl_front(c, pixels, count);
}
