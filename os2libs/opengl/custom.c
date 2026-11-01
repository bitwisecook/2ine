/* Public domain. No rights reserved. Wholly AI-authored. */
/* Driver strings and retained render-mode buffers have explicit guest storage. */
#include "internal.h"

uint32_t call_glGetError(uint8_t *a)
{
    (void)a;
    GlThread *t = gl_thread();
    if (!t)
        return GL_OUT_OF_MEMORY;
    uint32_t *slot = t->current ? &t->current->error : &t->error;
    if (*slot)
    {
        uint32_t result = *slot;
        *slot = 0;
        return result;
    }
    if (t->current && t->current->inside)
        return GL_INVALID_OPERATION;
    if (!t->current)
        return GL_NO_ERROR;
    GlContext *c = gl_current();
    uint32_t result = c ? c->api->glGetError() : GL_INVALID_OPERATION;
    TRACE_NATIVE("glGetError() = 0x%x", result);
    return result;
}
uint32_t call_glGetString(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    uint32_t token = gl_word(a, 0);
    if (c->inside || token < GL_VENDOR || token > GL_EXTENSIONS)
    {
        gl_error(c->inside ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return 0;
    }
    if (!c->strings)
    {
        c->strings =
            lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
        if (!c->strings)
        {
            gl_error(GL_OUT_OF_MEMORY);
            return 0;
        }
        char *p = lx_execution_mapping_host(c->strings);
        for (unsigned i = 0; i < 4; i++)
        {
            const char *text = i == 2   ? "1.1 2ine Mesa"
                               : i == 3 ? ""
                                        : (const char *)c->api->glGetString(GL_VENDOR + i);
            snprintf(p + i * 1024, 1024, "%s", text ? text : "");
        }
        if (!lx_execution_protect(GLoaderState.execution, c->strings, LX_GUEST_READ))
        {
            lx_execution_unmap(GLoaderState.execution, c->strings);
            c->strings = NULL;
            gl_error(GL_OUT_OF_MEMORY);
            return 0;
        }
    }
    return lx_execution_mapping_address(c->strings) + (token - GL_VENDOR) * 1024;
}
uint32_t call_glDrawBuffer(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    if (c->inside)
        gl_error(GL_INVALID_OPERATION);
    else if (!lx_host_gl_draw_buffer(c->host, gl_word(a, 0)))
        gl_error(GL_INVALID_ENUM);
    return 0;
}
uint32_t call_glReadBuffer(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    if (c->inside)
        gl_error(GL_INVALID_OPERATION);
    else if (!lx_host_gl_read_buffer(c->host, gl_word(a, 0)))
        gl_error(GL_INVALID_ENUM);
    return 0;
}
uint32_t call_glPushAttrib(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    uint32_t mask = gl_word(a, 0);
    if (c->inside)
        gl_error(GL_INVALID_OPERATION);
    else if (mask & ~GL_ALL_ATTRIB_BITS)
        gl_error(GL_INVALID_VALUE);
    else if (c->attribute_depth == 16)
        gl_error(GL_STACK_OVERFLOW);
    else
    {
        uint32_t *frame = c->attribute_stack[c->attribute_depth++];
        frame[0] = mask;
        frame[1] = lx_host_gl_buffer(c->host, 0);
        frame[2] = lx_host_gl_buffer(c->host, 1);
        c->api->glPushAttrib(mask);
    }
    return 0;
}
uint32_t call_glPopAttrib(uint8_t *a)
{
    (void)a;
    GlContext *c = gl_current();
    if (!c)
        return 0;
    if (c->inside)
        gl_error(GL_INVALID_OPERATION);
    else if (!c->attribute_depth)
        gl_error(GL_STACK_UNDERFLOW);
    else
    {
        uint32_t *frame = c->attribute_stack[--c->attribute_depth];
        c->api->glPopAttrib();
        if (frame[0] & GL_COLOR_BUFFER_BIT)
        {
            lx_host_gl_draw_buffer(c->host, frame[1]);
            lx_host_gl_read_buffer(c->host, frame[2]);
        }
    }
    return 0;
}
static uint32_t retained_buffer(uint8_t *a, int feedback)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    int32_t count = (int32_t)gl_word(a, 0);
    uint32_t type = feedback ? gl_word(a, 4) : 0;
    if (c->inside || c->render_mode != GL_RENDER)
    {
        gl_error(GL_INVALID_OPERATION);
        return 0;
    }
    if (feedback && (type < GL_2D || type > GL_4D_COLOR_TEXTURE))
    {
        gl_error(GL_INVALID_ENUM);
        return 0;
    }
    GlMemory memory = {0};
    uint32_t address = gl_word(a, feedback ? 8 : 4);
    if (!gl_memory(address, gl_count(count), 4, 1, 0, &memory))
        return 0;
    /* The driver keeps this owned allocation. The guest mapping is not pinned
       across calls and is checked again before render-mode copy-back. */
    void *buffer = memory.host;
    size_t bytes = memory.bytes;
    memory.host = NULL;
    gl_memory_finish(&memory);
    if (feedback)
    {
        c->api->glFeedbackBuffer(count, type, buffer);
        free(c->feedback);
        c->feedback = buffer;
        c->feedback_address = address;
        c->feedback_bytes = bytes;
    }
    else
    {
        c->api->glSelectBuffer(count, buffer);
        free(c->selection);
        c->selection = buffer;
        c->selection_address = address;
        c->selection_bytes = bytes;
    }
    return 0;
}
uint32_t call_glSelectBuffer(uint8_t *a)
{
    return retained_buffer(a, 0);
}
uint32_t call_glFeedbackBuffer(uint8_t *a)
{
    return retained_buffer(a, 1);
}
uint32_t call_glRenderMode(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    uint32_t mode = gl_word(a, 0);
    if (c->inside)
    {
        gl_error(GL_INVALID_OPERATION);
        return 0;
    }
    if (mode != GL_RENDER && mode != GL_SELECT && mode != GL_FEEDBACK)
    {
        gl_error(GL_INVALID_ENUM);
        return 0;
    }
    if ((mode == GL_SELECT && !c->selection_bytes) || (mode == GL_FEEDBACK && !c->feedback_bytes))
    {
        gl_error(GL_INVALID_OPERATION);
        return 0;
    }
    uint32_t previous = c->render_mode;
    uint32_t result = (uint32_t)c->api->glRenderMode(mode);
    int32_t actual = GL_RENDER;
    c->api->glGetIntegerv(GL_RENDER_MODE, &actual);
    c->render_mode = actual;
    void *buffer = previous == GL_SELECT ? c->selection : c->feedback;
    uint32_t address = previous == GL_SELECT ? c->selection_address : c->feedback_address;
    size_t bytes = previous == GL_SELECT ? c->selection_bytes : c->feedback_bytes;
    if (previous != GL_RENDER && bytes &&
        !lx_execution_write(GLoaderState.execution, address, buffer, bytes))
        gl_error(GL_INVALID_OPERATION);
    return result;
}
