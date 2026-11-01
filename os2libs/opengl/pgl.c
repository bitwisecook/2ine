/* Public domain. No rights reserved. Wholly AI-authored. */
/* PGL visual records live in read-only guest memory. Current bindings hold a
   typed handle lease and exclusive host-thread ownership until release. */
#include "internal.h"
#include "os2libs/pmwin/graphics.h"

GlContext *GlContexts;
LxHostMonitor *GlLock;
static LxHostTlsKey *ThreadKey;
static LxGuestMapping *Visuals;
enum
{
    VISUAL_BYTES = 88,
    VISUAL_COUNT = 2,
    VISUAL_LIST_OFFSET = 176
};

static void release_current(GlThread *t)
{
    if (t && t->current)
    {
        if (t->current->pm_ps)
        {
            WinReleasePS(t->current->pm_ps);
            t->current->pm_ps = 0;
        }
        lx_host_gl_release();
        t->current->owner = 0;
        t->current = NULL;
        lx_handle_release(&t->lease);
    }
}
static void destroy_thread(void *argument)
{
    GlThread *t = argument;
    lx_host_monitor_lock(GlLock);
    release_current(t);
    lx_host_monitor_unlock(GlLock);
    free(t);
}
GlThread *gl_thread(void)
{
    GlThread *t = lx_host_tls_get(ThreadKey);
    if (!t)
    {
        t = calloc(1, sizeof *t);
        if (!t || !lx_host_tls_set(ThreadKey, t))
        {
            free(t);
            return NULL;
        }
    }
    return t;
}
void gl_error(uint32_t error)
{
    GlThread *t = gl_thread();
    if (!t)
        return;
    uint32_t *slot = t->current ? &t->current->error : &t->error;
    if (!*slot)
        *slot = error;
}
GlContext *gl_current(void)
{
    GlThread *t = gl_thread();
    if (!t || !t->current)
    {
        gl_error(GL_INVALID_OPERATION);
        return NULL;
    }
    /* PM drawing can bind its own host graphics context between guest calls. */
    if (!t->current->inside &&
        !lx_host_gl_make_current(t->current->host, t->current->width, t->current->height))
    {
        gl_error(GL_INVALID_OPERATION);
        return NULL;
    }
    return t->current;
}
static int anchor(uint32_t hab)
{
    return hab && hab == WinQueryAnchorBlock(HWND_DESKTOP);
}
static void destroy_context(void *argument)
{
    GlContext *c = argument;
    lx_host_gl_destroy(c->host);
    if (c->strings)
        lx_execution_unmap(GLoaderState.execution, c->strings);
    free(c->selection);
    free(c->feedback);
    free(c);
}
int gl_pgl_initialize(void)
{
    GlLock = lx_host_monitor_create();
    ThreadKey = lx_host_tls_create(destroy_thread);
    Visuals = lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!GlLock || !ThreadKey || !Visuals)
        return 0;
    uint32_t base = lx_execution_mapping_address(Visuals);
    uint8_t *memory = lx_execution_mapping_host(Visuals);
    for (unsigned i = 0; i < VISUAL_COUNT; i++)
    {
        uint32_t words[22] = {i + 1, 1, 8, 8, 8,  8,  0xff0000, 0xff00, 0xff, 0, 0,
                              0,     0, i, 0, 32, 24, 8,        0,      0,    0, 0};
        for (unsigned n = 0; n < 22; n++)
            lx_guest_store_u32(memory + i * VISUAL_BYTES + n * 4, words[n]);
        lx_guest_store_u32(memory + VISUAL_LIST_OFFSET + i * 4, base + i * VISUAL_BYTES);
    }
    return lx_execution_protect(GLoaderState.execution, Visuals, LX_GUEST_READ);
}
void gl_pgl_shutdown(void)
{
    if (ThreadKey)
    {
        GlThread *t = lx_host_tls_get(ThreadKey);
        if (t)
        {
            lx_host_tls_set(ThreadKey, NULL);
            destroy_thread(t);
        }
    }
    while (GlContexts)
    {
        GlContext *c = GlContexts;
        GlContexts = c->next;
        lx_handle_close(c->handle, LX_HANDLE_GL_CONTEXT);
    }
    lx_host_gl_shutdown();
    if (Visuals)
        lx_execution_unmap(GLoaderState.execution, Visuals);
    Visuals = NULL;
    lx_host_tls_destroy(ThreadKey);
    ThreadKey = NULL;
    if (GlLock)
        lx_host_monitor_destroy(GlLock);
    GlLock = NULL;
}
static uint32_t choose(uint8_t *a)
{
    if (!anchor(gl_word(a, 0)))
        return 0;
    uint32_t list = gl_word(a, 4);
    int rgba = 0, double_buffer = 0, single_buffer = 0, ended = 0;
    for (unsigned i = 0; i < 128; i++)
    {
        uint8_t value_bytes[4];
        uint64_t address = (uint64_t)list + 4 * i;
        if (!list || address > UINT32_MAX ||
            !lx_execution_read(GLoaderState.execution, (uint32_t)address, value_bytes, 4))
            return 0;
        uint32_t token = lx_guest_load_u32(value_bytes);
        if (!token)
        {
            ended = 1;
            break;
        }
        if (token == 1)
            continue; /* PGL_USE_GL */
        if (token == 4)
        {
            rgba = 1;
            continue;
        }
        if (token == 5)
        {
            double_buffer = 1;
            continue;
        }
        if (token == 18)
        {
            single_buffer = 1;
            continue;
        }
        if (token == 6)
            return 0; /* Stereo visuals are unavailable. */
        if (++i == 128 || address > UINT32_MAX - 4 ||
            !lx_execution_read(GLoaderState.execution, (uint32_t)address + 4, value_bytes, 4))
            return 0;
        int32_t value = (int32_t)lx_guest_load_u32(value_bytes);
        if (value < 0)
            return 0;
        switch (token)
        {
            case 2:
                if (value > 32)
                    return 0;
                break;
            case 3:
            case 7:
            case 14:
            case 15:
            case 16:
            case 17:
                if (value)
                    return 0;
                break;
            case 8:
            case 9:
            case 10:
            case 11:
                if (value > 8)
                    return 0;
                break;
            case 12:
                if (value > 24)
                    return 0;
                break;
            case 13:
                if (value > 8)
                    return 0;
                break;
            default:
                return 0;
        }
    }
    return ended && rgba && !(double_buffer && single_buffer)
               ? lx_execution_mapping_address(Visuals) + (double_buffer ? VISUAL_BYTES : 0)
               : 0;
}
static uint32_t query_configs(uint8_t *a)
{
    return anchor(gl_word(a, 0)) ? lx_execution_mapping_address(Visuals) + VISUAL_LIST_OFFSET : 0;
}
static uint32_t create(uint8_t *a)
{
    uint32_t hab = gl_word(a, 0), visual = gl_word(a, 4), share = gl_word(a, 8);
    uint32_t base = lx_execution_mapping_address(Visuals);
    if (!anchor(hab) || (visual != base && visual != base + VISUAL_BYTES))
        return 0;
    GlContext *c = calloc(1, sizeof *c);
    if (!c)
        return 0;
    c->config = (LxHostGlConfig){visual != base, 8, 8, 8, 8, 24, 8};
    c->hab = hab;
    c->render_mode = GL_RENDER;
    static const int sizes[] = {4, 3, 4, 1, 4, 1};
    for (unsigned i = 0; i < GL_ARRAY_COUNT; i++)
        c->arrays[i] =
            (GlArray){0, i == GL_ARRAY_EDGE ? GL_UNSIGNED_BYTE : GL_FLOAT, sizes[i], 0, 0};
    lx_host_monitor_lock(GlLock);
    LxHandleBorrow share_lease = {0};
    GlContext *shared = NULL;
    if (share)
    {
        if (!lx_handle_borrow(share, LX_HANDLE_GL_CONTEXT, &share_lease))
            goto failed;
        shared = share_lease.resource;
    }
    c->host = lx_host_gl_create(&c->config, shared ? shared->host : NULL);
    lx_handle_release(&share_lease);
    if (!c->host)
    {
        fprintf(stderr, "2ine OpenGL: %s\n", lx_host_gl_error());
        goto failed;
    }
    c->api = lx_host_gl_api(c->host);
    c->handle = lx_handle_create(LX_HANDLE_GL_CONTEXT, c, destroy_context);
    if (!c->handle)
        goto failed;
    c->next = GlContexts;
    GlContexts = c;
    lx_host_monitor_unlock(GlLock);
    return c->handle;
failed:
    lx_handle_release(&share_lease);
    lx_host_monitor_unlock(GlLock);
    destroy_context(c);
    return 0;
}
static uint32_t make_current(uint8_t *a)
{
    GlThread *t = gl_thread();
    uint32_t handle = gl_word(a, 4), window = gl_word(a, 8);
    if (!t || !anchor(gl_word(a, 0)) || (t->current && t->current->inside))
        return 0;
    lx_host_monitor_lock(GlLock);
    if (!handle && !window)
    {
        release_current(t);
        lx_host_monitor_unlock(GlLock);
        return 1;
    }
    LxHandleBorrow lease = {0};
    RECTL rect;
    uint32_t result = 0;
    if (!handle || !window || !lx_handle_borrow(handle, LX_HANDLE_GL_CONTEXT, &lease))
        goto done;
    GlContext *c = lease.resource;
    if ((c->owner && c->owner != lx_host_thread_id()) || !WinQueryWindowRect(window, &rect) ||
        rect.xRight <= 0 || rect.yTop <= 0 ||
        !lx_host_gl_make_current(c->host, rect.xRight, rect.yTop))
    {
        /* A failed attempt must preserve the previous PGL binding. */
        if (t->current)
            lx_host_gl_make_current(t->current->host, t->current->width, t->current->height);
        goto done;
    }
    if (t->current)
    {
        if (t->current->pm_ps)
        {
            WinReleasePS(t->current->pm_ps);
            t->current->pm_ps = 0;
        }
        t->current->owner = 0;
        lx_handle_release(&t->lease);
    }
    t->current = c;
    t->lease = lease;
    memset(&lease, 0, sizeof lease);
    c->owner = lx_host_thread_id();
    c->window = window;
    c->width = rect.xRight;
    c->height = rect.yTop;
    result = 1;
done:
    lx_handle_release(&lease);
    lx_host_monitor_unlock(GlLock);
    return result;
}
static uint32_t destroy(uint8_t *a)
{
    if (!anchor(gl_word(a, 0)))
        return 0;
    lx_host_monitor_lock(GlLock);
    LxHandleBorrow lease = {0};
    uint32_t result = 0, handle = gl_word(a, 4);
    if (lx_handle_borrow(handle, LX_HANDLE_GL_CONTEXT, &lease))
    {
        GlContext *c = lease.resource;
        if (!c->owner)
        {
            GlContext **link = &GlContexts;
            while (*link && *link != c)
                link = &(*link)->next;
            if (*link)
                *link = c->next;
            result = lx_handle_close(handle, LX_HANDLE_GL_CONTEXT);
        }
    }
    lx_handle_release(&lease);
    lx_host_monitor_unlock(GlLock);
    return result;
}
int gl_resize_current(GlContext *c)
{
    RECTL rect;
    if (c->inside || !WinQueryWindowRect(c->window, &rect) || rect.xRight <= 0 || rect.yTop <= 0)
        return c->inside;
    if ((unsigned)rect.xRight == c->width && (unsigned)rect.yTop == c->height)
        return 1;
    if (!lx_host_gl_make_current(c->host, rect.xRight, rect.yTop))
    {
        gl_error(GL_OUT_OF_MEMORY);
        return 0;
    }
    c->width = rect.xRight;
    c->height = rect.yTop;
    return 1;
}
int gl_present(GlContext *c, int swap)
{
    if (!c || c->inside || !c->width || !c->height)
        return 0;
    size_t count = (size_t)c->width * c->height;
    uint32_t *pixels = malloc(count * sizeof *pixels);
    if (!pixels)
        return 0;
    int result =
        swap ? lx_host_gl_swap(c->host, pixels, count) : lx_host_gl_front(c->host, pixels, count);
    HPS ps = result ? WinGetPS(c->window) : 0;
    result = ps && lx_pm_raster_write(ps, 0, 0, c->width, c->height, pixels);
    if (ps)
        WinReleasePS(ps);
    free(pixels);
    return lx_host_gl_make_current(c->host, c->width, c->height) && result;
}
static uint32_t current_context(uint8_t *a)
{
    GlThread *t = gl_thread();
    return anchor(gl_word(a, 0)) && t && t->current ? t->current->handle : 0;
}
static uint32_t current_window(uint8_t *a)
{
    GlThread *t = gl_thread();
    return anchor(gl_word(a, 0)) && t && t->current ? t->current->window : 0;
}
static uint32_t swap(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c || !anchor(gl_word(a, 0)) || c->window != gl_word(a, 4) || !gl_present(c, 1))
        gl_error(GL_INVALID_OPERATION);
    return 0;
}
static uint32_t capability(uint8_t *a)
{
    if (!anchor(gl_word(a, 0)))
        return 0;
    LxHostGlConfig config = {0, 8, 8, 8, 8, 24, 8};
    LxHostGlContext *probe = lx_host_gl_create(&config, NULL);
    if (!probe)
        return 0;
    lx_host_gl_destroy(probe);
    return 1;
}
static uint32_t wait_gl(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c || c->inside || !anchor(gl_word(a, 0)))
        return 0;
    c->api->glFinish();
    if (!gl_present(c, 0))
        return 0;
    if (!c->pm_ps)
        c->pm_ps = WinGetPS(c->window);
    return c->pm_ps;
}
static uint32_t wait_pm(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c || c->inside || !anchor(gl_word(a, 0)))
        return 0;
    if (c->pm_ps)
    {
        size_t count = (size_t)c->width * c->height;
        uint32_t *pixels = malloc(count * sizeof *pixels);
        if (!pixels || !lx_pm_raster_read(c->pm_ps, 0, 0, c->width, c->height, pixels) ||
            !lx_host_gl_make_current(c->host, c->width, c->height) ||
            !lx_host_gl_write_front(c->host, pixels, count))
            gl_error(GL_INVALID_OPERATION);
        free(pixels);
        WinReleasePS(c->pm_ps);
        c->pm_ps = 0;
    }
    return 0;
}
static uint32_t version(uint8_t *a)
{
    if (!anchor(gl_word(a, 0)))
        return 0;
    GlMemory major = {0}, minor = {0};
    if (gl_memory(gl_word(a, 4), 1, 4, 1, 0, &major) &&
        gl_memory(gl_word(a, 8), 1, 4, 1, 0, &minor))
    {
        lx_guest_store_u32(major.host, 1);
        lx_guest_store_u32(minor.host, 1);
        major.commit = minor.commit = 1;
    }
    gl_memory_finish(&minor);
    gl_memory_finish(&major);
    return 0;
}
static uint32_t indirect(uint8_t *a)
{
    LxHandleBorrow lease = {0};
    uint32_t result = UINT32_MAX;
    if (anchor(gl_word(a, 0)) && lx_handle_borrow(gl_word(a, 4), LX_HANDLE_GL_CONTEXT, &lease))
        result = 0;
    lx_handle_release(&lease);
    return result;
}
static uint32_t unsupported(uint8_t *a)
{
    (void)a;
    return 0;
}
const GlEntry GlPglEntries[] = {
#include "os2libs/generated/pgl-entries.h"
};
const size_t GlPglEntryCount = sizeof GlPglEntries / sizeof GlPglEntries[0];
