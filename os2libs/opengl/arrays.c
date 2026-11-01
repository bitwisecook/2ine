/* Public domain. No rights reserved. Wholly AI-authored. */
/* Client array definitions retain guest addresses. A draw takes checked,
   bounded snapshots and removes host pointers before releasing their storage. */
#include "internal.h"

static int array_index(uint32_t array)
{
    switch (array)
    {
        case GL_VERTEX_ARRAY:
        case GL_VERTEX_ARRAY_POINTER:
            return GL_ARRAY_VERTEX;
        case GL_NORMAL_ARRAY:
        case GL_NORMAL_ARRAY_POINTER:
            return GL_ARRAY_NORMAL;
        case GL_COLOR_ARRAY:
        case GL_COLOR_ARRAY_POINTER:
            return GL_ARRAY_COLOR;
        case GL_INDEX_ARRAY:
        case GL_INDEX_ARRAY_POINTER:
            return GL_ARRAY_INDEX;
        case GL_TEXTURE_COORD_ARRAY:
        case GL_TEXTURE_COORD_ARRAY_POINTER:
            return GL_ARRAY_TEXTURE;
        case GL_EDGE_FLAG_ARRAY:
        case GL_EDGE_FLAG_ARRAY_POINTER:
            return GL_ARRAY_EDGE;
        default:
            return -1;
    }
}
static int outside(GlContext *c)
{
    if (!c || c->inside)
    {
        gl_error(GL_INVALID_OPERATION);
        return 0;
    }
    return 1;
}
static int array_definition(int index, int size, uint32_t type, int stride)
{
    if (stride < 0)
        return 0;
    int numeric = type == GL_SHORT || type == GL_INT || type == GL_FLOAT || type == GL_DOUBLE;
    switch (index)
    {
        case GL_ARRAY_VERTEX:
            return numeric && size >= 2 && size <= 4;
        case GL_ARRAY_TEXTURE:
            return numeric && size >= 1 && size <= 4;
        case GL_ARRAY_NORMAL:
            return (numeric || type == GL_BYTE) && size == 3;
        case GL_ARRAY_INDEX:
            return (numeric || type == GL_UNSIGNED_BYTE) && size == 1;
        case GL_ARRAY_COLOR:
            return gl_type_size(type) && size >= 3 && size <= 4;
        case GL_ARRAY_EDGE:
            return type == GL_UNSIGNED_BYTE && size == 1;
        default:
            return 0;
    }
}
static uint32_t define_array(uint8_t *a, int index, int fixed_size, uint32_t fixed_type)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    unsigned offset = 0;
    int size = fixed_size ? fixed_size : (int32_t)gl_word(a, offset);
    if (!fixed_size)
        offset += 4;
    uint32_t type = fixed_type ? fixed_type : gl_word(a, offset);
    if (!fixed_type)
        offset += 4;
    int stride = (int32_t)gl_word(a, offset);
    if (!array_definition(index, size, type, stride))
    {
        gl_error(stride < 0 || size < 1 || size > 4 ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return 0;
    }
    GlArray *array = &c->arrays[index];
    *array = (GlArray){gl_word(a, offset + 4), type, size, stride, array->enabled};
    return 0;
}
uint32_t call_glVertexPointer(uint8_t *a)
{
    return define_array(a, GL_ARRAY_VERTEX, 0, 0);
}
uint32_t call_glNormalPointer(uint8_t *a)
{
    return define_array(a, GL_ARRAY_NORMAL, 3, 0);
}
uint32_t call_glColorPointer(uint8_t *a)
{
    return define_array(a, GL_ARRAY_COLOR, 0, 0);
}
uint32_t call_glIndexPointer(uint8_t *a)
{
    return define_array(a, GL_ARRAY_INDEX, 1, 0);
}
uint32_t call_glTexCoordPointer(uint8_t *a)
{
    return define_array(a, GL_ARRAY_TEXTURE, 0, 0);
}
uint32_t call_glEdgeFlagPointer(uint8_t *a)
{
    return define_array(a, GL_ARRAY_EDGE, 1, GL_UNSIGNED_BYTE);
}
static uint32_t enable_array(uint8_t *a, int enable)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    uint32_t token = gl_word(a, 0);
    int index = array_index(token);
    if (index < 0 || token < GL_VERTEX_ARRAY || token > GL_EDGE_FLAG_ARRAY)
        gl_error(GL_INVALID_ENUM);
    else
    {
        c->arrays[index].enabled = enable;
        if (enable)
            c->api->glEnableClientState(token);
        else
            c->api->glDisableClientState(token);
    }
    return 0;
}
uint32_t call_glEnableClientState(uint8_t *a)
{
    return enable_array(a, 1);
}
uint32_t call_glDisableClientState(uint8_t *a)
{
    return enable_array(a, 0);
}

static void host_array(GlContext *c, int index, const void *data)
{
    GlArray *p = &c->arrays[index];
    switch (index)
    {
        case GL_ARRAY_VERTEX:
            c->api->glVertexPointer(p->size, p->type, p->stride, data);
            break;
        case GL_ARRAY_NORMAL:
            c->api->glNormalPointer(p->type, p->stride, data);
            break;
        case GL_ARRAY_COLOR:
            c->api->glColorPointer(p->size, p->type, p->stride, data);
            break;
        case GL_ARRAY_INDEX:
            c->api->glIndexPointer(p->type, p->stride, data);
            break;
        case GL_ARRAY_TEXTURE:
            c->api->glTexCoordPointer(p->size, p->type, p->stride, data);
            break;
        case GL_ARRAY_EDGE:
            c->api->glEdgeFlagPointer(p->stride, data);
            break;
    }
}
static void release_arrays(GlContext *c, GlMemory *memory)
{
    for (int i = 0; i < GL_ARRAY_COUNT; i++)
        if (memory[i].host)
        {
            host_array(c, i, NULL);
            gl_memory_finish(&memory[i]);
        }
}
static int prepare_arrays(GlContext *c, uint32_t maximum, GlMemory *memory)
{
    for (int i = 0; i < GL_ARRAY_COUNT; i++)
    {
        GlArray *p = &c->arrays[i];
        if (!p->enabled)
            continue;
        size_t element = gl_type_size(p->type) * p->size;
        uint64_t bytes = (uint64_t)maximum * (p->stride ? (uint32_t)p->stride : element) + element;
        if (bytes > GL_MAX_TRANSFER || !gl_memory(p->address, (size_t)bytes, 1, 0, 0, &memory[i]))
        {
            gl_error(GL_INVALID_OPERATION);
            release_arrays(c, memory);
            return 0;
        }
        host_array(c, i, memory[i].host);
    }
    return 1;
}
uint32_t call_glDrawArrays(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    uint32_t mode = gl_word(a, 0);
    int32_t first = (int32_t)gl_word(a, 4), count = (int32_t)gl_word(a, 8);
    if (first < 0 || count < 0 || (uint64_t)(uint32_t)first + (uint32_t)count > INT32_MAX)
    {
        gl_error(GL_INVALID_VALUE);
        return 0;
    }
    GlMemory memory[GL_ARRAY_COUNT] = {0};
    if (!count || prepare_arrays(c, first + count - 1, memory))
        c->api->glDrawArrays(mode, first, count);
    release_arrays(c, memory);
    return 0;
}
uint32_t call_glDrawElements(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    uint32_t mode = gl_word(a, 0), type = gl_word(a, 8);
    int32_t count = (int32_t)gl_word(a, 4);
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT)
    {
        gl_error(GL_INVALID_ENUM);
        return 0;
    }
    GlMemory indices = {0}, memory[GL_ARRAY_COUNT] = {0};
    size_t unit = gl_type_size(type);
    if (!gl_memory(gl_word(a, 12), gl_count(count), unit, 0, 0, &indices))
        return 0;
    uint32_t maximum = 0;
    const uint8_t *p = indices.host;
    for (int32_t i = 0; i < count; i++)
    {
        uint32_t value = unit == 1   ? p[i]
                         : unit == 2 ? lx_guest_load_u16(p + 2 * i)
                                     : lx_guest_load_u32(p + 4 * i);
        if (value > maximum)
            maximum = value;
    }
    if (!count || prepare_arrays(c, maximum, memory))
        c->api->glDrawElements(mode, count, type, indices.host);
    release_arrays(c, memory);
    gl_memory_finish(&indices);
    return 0;
}
uint32_t call_glGetPointerv(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    uint32_t token = gl_word(a, 0);
    int index = array_index(token);
    int retained = token == GL_FEEDBACK_BUFFER_POINTER || token == GL_SELECTION_BUFFER_POINTER;
    if (!retained &&
        (index < 0 || token < GL_VERTEX_ARRAY_POINTER || token > GL_EDGE_FLAG_ARRAY_POINTER))
        gl_error(GL_INVALID_ENUM);
    else
    {
        uint8_t word[4];
        lx_guest_store_u32(word, retained
                                     ? (token == GL_FEEDBACK_BUFFER_POINTER ? c->feedback_address
                                                                            : c->selection_address)
                                     : c->arrays[index].address);
        if (!lx_execution_write(GLoaderState.execution, gl_word(a, 4), word, sizeof word))
            gl_error(GL_INVALID_OPERATION);
    }
    return 0;
}
uint32_t call_glPushClientAttrib(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    uint32_t mask = gl_word(a, 0);
    if (mask != GL_CLIENT_ALL_ATTRIB_BITS &&
        (mask & ~(GL_CLIENT_PIXEL_STORE_BIT | GL_CLIENT_VERTEX_ARRAY_BIT)))
        gl_error(GL_INVALID_VALUE);
    else if (c->array_depth == 16)
        gl_error(GL_STACK_OVERFLOW);
    else
    {
        GlArrayFrame *frame = &c->array_stack[c->array_depth++];
        memcpy(frame->arrays, c->arrays, sizeof c->arrays);
        frame->mask = mask;
        c->api->glPushClientAttrib(mask);
    }
    return 0;
}
uint32_t call_glPopClientAttrib(uint8_t *a)
{
    (void)a;
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    if (!c->array_depth)
        gl_error(GL_STACK_UNDERFLOW);
    else
    {
        GlArrayFrame *frame = &c->array_stack[--c->array_depth];
        c->api->glPopClientAttrib();
        if (frame->mask & GL_CLIENT_VERTEX_ARRAY_BIT)
            memcpy(c->arrays, frame->arrays, sizeof c->arrays);
    }
    return 0;
}
uint32_t call_glInterleavedArrays(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!outside(c))
        return 0;
    /* Byte offsets and total record sizes are defined by GL 1.1's formats. */
    static const int formats[][9] = {{8, 2, 0, 0, 0, 0, -1, 0, 0},
                                     {12, 3, 0, 0, 0, 0, -1, 0, 0},
                                     {12, 2, 4, 4, GL_UNSIGNED_BYTE, 0, -1, 0, 0},
                                     {16, 3, 4, 4, GL_UNSIGNED_BYTE, 0, -1, 0, 0},
                                     {24, 3, 12, 3, GL_FLOAT, 0, -1, 0, 0},
                                     {24, 3, 12, 0, 0, 0, 0, 0, 0},
                                     {40, 3, 28, 4, GL_FLOAT, 0, 16, 0, 0},
                                     {20, 3, 8, 0, 0, 0, -1, 2, 0},
                                     {32, 4, 16, 0, 0, 0, -1, 4, 0},
                                     {24, 3, 12, 4, GL_UNSIGNED_BYTE, 8, -1, 2, 0},
                                     {32, 3, 20, 3, GL_FLOAT, 8, -1, 2, 0},
                                     {32, 3, 20, 0, 0, 0, 8, 2, 0},
                                     {48, 3, 36, 4, GL_FLOAT, 8, 24, 2, 0},
                                     {60, 4, 44, 4, GL_FLOAT, 16, 32, 4, 0}};
    uint32_t format = gl_word(a, 0), address = gl_word(a, 8);
    int32_t stride = (int32_t)gl_word(a, 4);
    if (format < GL_V2F || format > GL_T4F_C4F_N3F_V4F || stride < 0)
    {
        gl_error(stride < 0 ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return 0;
    }
    const int *f = formats[format - GL_V2F];
    if (address > UINT32_MAX - (uint32_t)f[0])
    {
        gl_error(GL_INVALID_VALUE);
        return 0;
    }
    if (!stride)
        stride = f[0];
    c->arrays[GL_ARRAY_VERTEX] = (GlArray){address + f[2], GL_FLOAT, f[1], stride, 1};
    c->arrays[GL_ARRAY_COLOR] = (GlArray){address + f[5], f[4], f[3], stride, !!f[3]};
    c->arrays[GL_ARRAY_NORMAL] =
        (GlArray){address + (f[6] < 0 ? 0 : f[6]), GL_FLOAT, 3, stride, f[6] >= 0};
    c->arrays[GL_ARRAY_TEXTURE] = (GlArray){address + f[8], GL_FLOAT, f[7], stride, !!f[7]};
    c->arrays[GL_ARRAY_INDEX].enabled = c->arrays[GL_ARRAY_EDGE].enabled = 0;
    for (unsigned i = 0; i < GL_ARRAY_COUNT; i++)
    {
        static const uint32_t tokens[] = {GL_VERTEX_ARRAY,        GL_NORMAL_ARRAY,
                                          GL_COLOR_ARRAY,         GL_INDEX_ARRAY,
                                          GL_TEXTURE_COORD_ARRAY, GL_EDGE_FLAG_ARRAY};
        if (c->arrays[i].enabled)
            c->api->glEnableClientState(tokens[i]);
        else
            c->api->glDisableClientState(tokens[i]);
    }
    return 0;
}
static void apply_element(GlContext *c, int index, const GlArray *p, const void *data)
{
    if (index == GL_ARRAY_EDGE)
    {
        c->api->glEdgeFlagv(data);
        return;
    }
#define APPLY_COORDINATE(suffix)                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (index == GL_ARRAY_VERTEX)                                                              \
        {                                                                                          \
            if (p->size == 2)                                                                      \
                c->api->glVertex2##suffix##v(data);                                                \
            else if (p->size == 3)                                                                 \
                c->api->glVertex3##suffix##v(data);                                                \
            else                                                                                   \
                c->api->glVertex4##suffix##v(data);                                                \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            if (p->size == 1)                                                                      \
                c->api->glTexCoord1##suffix##v(data);                                              \
            else if (p->size == 2)                                                                 \
                c->api->glTexCoord2##suffix##v(data);                                              \
            else if (p->size == 3)                                                                 \
                c->api->glTexCoord3##suffix##v(data);                                              \
            else                                                                                   \
                c->api->glTexCoord4##suffix##v(data);                                              \
        }                                                                                          \
    } while (0)
    if (index == GL_ARRAY_VERTEX || index == GL_ARRAY_TEXTURE)
    {
        switch (p->type)
        {
            case GL_SHORT:
                APPLY_COORDINATE(s);
                break;
            case GL_INT:
                APPLY_COORDINATE(i);
                break;
            case GL_FLOAT:
                APPLY_COORDINATE(f);
                break;
            case GL_DOUBLE:
                APPLY_COORDINATE(d);
                break;
        }
        return;
    }
#undef APPLY_COORDINATE
#define APPLY_COLOR(suffix)                                                                        \
    do                                                                                             \
    {                                                                                              \
        if (p->size == 3)                                                                          \
            c->api->glColor3##suffix##v(data);                                                     \
        else                                                                                       \
            c->api->glColor4##suffix##v(data);                                                     \
    } while (0)
    if (index == GL_ARRAY_COLOR)
    {
        switch (p->type)
        {
            case GL_BYTE:
                APPLY_COLOR(b);
                break;
            case GL_UNSIGNED_BYTE:
                APPLY_COLOR(ub);
                break;
            case GL_SHORT:
                APPLY_COLOR(s);
                break;
            case GL_UNSIGNED_SHORT:
                APPLY_COLOR(us);
                break;
            case GL_INT:
                APPLY_COLOR(i);
                break;
            case GL_UNSIGNED_INT:
                APPLY_COLOR(ui);
                break;
            case GL_FLOAT:
                APPLY_COLOR(f);
                break;
            case GL_DOUBLE:
                APPLY_COLOR(d);
                break;
        }
        return;
    }
#undef APPLY_COLOR
    if (index == GL_ARRAY_NORMAL)
    {
        switch (p->type)
        {
            case GL_BYTE:
                c->api->glNormal3bv(data);
                break;
            case GL_SHORT:
                c->api->glNormal3sv(data);
                break;
            case GL_INT:
                c->api->glNormal3iv(data);
                break;
            case GL_FLOAT:
                c->api->glNormal3fv(data);
                break;
            case GL_DOUBLE:
                c->api->glNormal3dv(data);
                break;
        }
        return;
    }
    switch (p->type)
    {
        case GL_SHORT:
            c->api->glIndexsv(data);
            break;
        case GL_INT:
            c->api->glIndexiv(data);
            break;
        case GL_FLOAT:
            c->api->glIndexfv(data);
            break;
        case GL_DOUBLE:
            c->api->glIndexdv(data);
            break;
        case GL_UNSIGNED_BYTE:
            c->api->glIndexubv(data);
            break;
    }
}
uint32_t call_glArrayElement(uint8_t *a)
{
    GlContext *c = gl_current();
    if (!c)
        return 0;
    int32_t index = (int32_t)gl_word(a, 0);
    if (index < 0)
    {
        gl_error(GL_INVALID_VALUE);
        return 0;
    }
    /* Vertex is applied last, after the enabled current attributes. */
    static const int order[] = {GL_ARRAY_COLOR,   GL_ARRAY_NORMAL, GL_ARRAY_INDEX,
                                GL_ARRAY_TEXTURE, GL_ARRAY_EDGE,   GL_ARRAY_VERTEX};
    for (unsigned n = 0; n < GL_ARRAY_COUNT; n++)
    {
        int i = order[n];
        GlArray *p = &c->arrays[i];
        if (!p->enabled)
            continue;
        size_t unit = gl_type_size(p->type), bytes = unit * p->size;
        uint64_t address = (uint64_t)p->address + (uint64_t)index * (p->stride ? p->stride : bytes);
        union
        {
            double alignment;
            uint8_t bytes[32];
        } data;
        if (address > UINT32_MAX || !bytes || bytes > sizeof data.bytes ||
            !lx_execution_read(GLoaderState.execution, (uint32_t)address, data.bytes, bytes))
        {
            gl_error(GL_INVALID_OPERATION);
            return 0;
        }
        apply_element(c, i, p, data.bytes);
    }
    return 0;
}
