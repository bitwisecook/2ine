/* Public domain. No rights reserved. Wholly AI-authored. */
/* OpenGL pointer extents include pixel-store offsets and evaluator strides.
   Temporary aligned snapshots keep host pointers out of retained guest state. */
#include "internal.h"

uint32_t gl_word(const uint8_t *a, unsigned offset)
{
    return lx_guest_load_u32(a + offset);
}
float gl_float(const uint8_t *a, unsigned offset)
{
    uint32_t bits = gl_word(a, offset);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
double gl_double(const uint8_t *a, unsigned offset)
{
    uint64_t bits = (uint64_t)gl_word(a, offset) | (uint64_t)gl_word(a, offset + 4) << 32;
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
size_t gl_count(int32_t count)
{
    return count < 0 ? SIZE_MAX : (size_t)count;
}
int gl_memory(uint32_t address, size_t count, size_t unit, int output, int nullable, GlMemory *m)
{
    if (count > GL_MAX_TRANSFER || !unit || unit > GL_MAX_TRANSFER ||
        count > GL_MAX_TRANSFER / unit)
    {
        gl_error(GL_INVALID_VALUE);
        return 0;
    }
    size_t bytes = count * unit;
    if (!bytes || (!address && nullable))
        return 1;
    if (!address || !lx_execution_borrow(GLoaderState.execution, address, bytes,
                                         output ? LX_GUEST_WRITE : LX_GUEST_READ, &m->borrow))
    {
        gl_error(GL_INVALID_OPERATION);
        return 0;
    }
    m->host = malloc(bytes);
    if (!m->host)
    {
        lx_execution_release(&m->borrow);
        gl_error(GL_OUT_OF_MEMORY);
        return 0;
    }
    m->bytes = bytes;
    memcpy(m->host, m->borrow.host, bytes);
    return 1;
}
void gl_memory_finish(GlMemory *m)
{
    if (m->commit && m->host)
        memcpy(m->borrow.host, m->host, m->bytes);
    lx_execution_release(&m->borrow);
    free(m->host);
    memset(m, 0, sizeof *m);
}
size_t gl_type_size(uint32_t type)
{
    switch (type)
    {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return 2;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
            return 4;
        case GL_DOUBLE:
            return 8;
        default:
            return 0;
    }
}
size_t gl_list_bytes(int32_t n, uint32_t type)
{
    size_t unit = type == GL_2_BYTES   ? 2
                  : type == GL_3_BYTES ? 3
                  : type == GL_4_BYTES ? 4
                                       : gl_type_size(type);
    return n < 0 || !unit || (uint64_t)n * unit > GL_MAX_TRANSFER ? SIZE_MAX : (size_t)n * unit;
}
size_t gl_parameter_count(uint32_t p)
{
    switch (p)
    {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_POSITION:
        case GL_AMBIENT_AND_DIFFUSE:
        case GL_EMISSION:
        case GL_LIGHT_MODEL_AMBIENT:
        case GL_FOG_COLOR:
        case GL_TEXTURE_ENV_COLOR:
        case GL_OBJECT_PLANE:
        case GL_EYE_PLANE:
        case GL_TEXTURE_BORDER_COLOR:
            return 4;
        case GL_SPOT_DIRECTION:
        case GL_COLOR_INDEXES:
            return 3;
        case GL_SHININESS:
        case GL_SPOT_EXPONENT:
        case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
        case GL_LIGHT_MODEL_LOCAL_VIEWER:
        case GL_LIGHT_MODEL_TWO_SIDE:
        case GL_FOG_MODE:
        case GL_FOG_DENSITY:
        case GL_FOG_START:
        case GL_FOG_END:
        case GL_FOG_INDEX:
        case GL_TEXTURE_MAG_FILTER:
        case GL_TEXTURE_MIN_FILTER:
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
        case GL_TEXTURE_PRIORITY:
        case GL_TEXTURE_RESIDENT:
        case GL_TEXTURE_ENV_MODE:
        case GL_TEXTURE_GEN_MODE:
            return 1;
        default:
            gl_error(GL_INVALID_ENUM);
            return SIZE_MAX;
    }
}
size_t gl_query_count(uint32_t p)
{
    switch (p)
    {
        case GL_MODELVIEW_MATRIX:
        case GL_PROJECTION_MATRIX:
        case GL_TEXTURE_MATRIX:
            return 16;
        case GL_CURRENT_COLOR:
        case GL_CURRENT_TEXTURE_COORDS:
        case GL_CURRENT_RASTER_COLOR:
        case GL_CURRENT_RASTER_TEXTURE_COORDS:
        case GL_CURRENT_RASTER_POSITION:
        case GL_LIGHT_MODEL_AMBIENT:
        case GL_FOG_COLOR:
        case GL_VIEWPORT:
        case GL_SCISSOR_BOX:
        case GL_COLOR_CLEAR_VALUE:
        case GL_COLOR_WRITEMASK:
        case GL_ACCUM_CLEAR_VALUE:
        case GL_MAP2_GRID_DOMAIN:
            return 4;
        case GL_CURRENT_NORMAL:
            return 3;
        case GL_DEPTH_RANGE:
        case GL_POLYGON_MODE:
        case GL_MAX_VIEWPORT_DIMS:
        case GL_POINT_SIZE_RANGE:
        case GL_LINE_WIDTH_RANGE:
        case GL_MAP1_GRID_DOMAIN:
        case GL_MAP2_GRID_SEGMENTS:
            return 2;
        default:
            /* GL 1.1 has no variable-length general state queries. Reject later
           extension tokens whose output lengths are not part of this ABI. */
            return p < 0x3000 || (p >= GL_POLYGON_OFFSET_FACTOR && p <= GL_EDGE_FLAG_ARRAY_STRIDE)
                       ? 1
                       : SIZE_MAX;
    }
}
size_t gl_pixel_bytes(GlContext *c, int32_t width, int32_t height, uint32_t format, uint32_t type,
                      int pack)
{
    if (width < 0 || height < 0)
        return SIZE_MAX;
    if (!width || !height)
        return 0;
    size_t components;
    switch (format)
    {
        case GL_RGB:
            components = 3;
            break;
        case GL_RGBA:
            components = 4;
            break;
        case GL_LUMINANCE_ALPHA:
            components = 2;
            break;
        case GL_COLOR_INDEX:
        case GL_STENCIL_INDEX:
        case GL_DEPTH_COMPONENT:
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:
            components = 1;
            break;
        default:
            return SIZE_MAX;
    }
    size_t unit = gl_type_size(type);
    if (type == GL_BITMAP)
    {
        if (format != GL_COLOR_INDEX && format != GL_STENCIL_INDEX)
            return SIZE_MAX;
        unit = 1;
    }
    if (!unit)
        return SIZE_MAX;
    int32_t alignment = 0, row_length = 0, skip_rows = 0, skip_pixels = 0;
    c->api->glGetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT, &alignment);
    c->api->glGetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH, &row_length);
    c->api->glGetIntegerv(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, &skip_rows);
    c->api->glGetIntegerv(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    if ((alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8) || row_length < 0 ||
        skip_rows < 0 || skip_pixels < 0)
        return SIZE_MAX;
    uint64_t row = (uint64_t)(row_length ? row_length : width) * components * unit;
    if (type == GL_BITMAP)
        row = (row + 7) / 8;
    row = (row + alignment - 1) & ~(uint64_t)(alignment - 1);
    uint64_t last = ((uint64_t)skip_pixels + width) * components * unit;
    if (type == GL_BITMAP)
        last = (last + 7) / 8;
    if (row > GL_MAX_TRANSFER || last > GL_MAX_TRANSFER)
        return SIZE_MAX;
    uint64_t bytes = ((uint64_t)skip_rows + height - 1) * row + last;
    return bytes > GL_MAX_TRANSFER ? SIZE_MAX : (size_t)bytes;
}
size_t gl_texture_bytes(GlContext *c, uint32_t target, int32_t level, uint32_t format,
                        uint32_t type)
{
    if ((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) || level < 0 || level > 31)
        return SIZE_MAX;
    int32_t width = 0, height = 1;
    c->api->glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &width);
    if (target == GL_TEXTURE_2D)
        c->api->glGetTexLevelParameteriv(target, level, GL_TEXTURE_HEIGHT, &height);
    return gl_pixel_bytes(c, width, height, format, type, 1);
}
size_t gl_pixel_map_count(GlContext *c, uint32_t map)
{
    if (map < GL_PIXEL_MAP_I_TO_I || map > GL_PIXEL_MAP_A_TO_A)
        return SIZE_MAX;
    int32_t count = 0;
    c->api->glGetIntegerv(map + (GL_PIXEL_MAP_I_TO_I_SIZE - GL_PIXEL_MAP_I_TO_I), &count);
    return gl_count(count);
}
static unsigned map_components(uint32_t target)
{
    switch (target)
    {
        case GL_MAP1_INDEX:
        case GL_MAP2_INDEX:
        case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_1:
            return 1;
        case GL_MAP1_TEXTURE_COORD_2:
        case GL_MAP2_TEXTURE_COORD_2:
            return 2;
        case GL_MAP1_NORMAL:
        case GL_MAP2_NORMAL:
        case GL_MAP1_VERTEX_3:
        case GL_MAP2_VERTEX_3:
        case GL_MAP1_TEXTURE_COORD_3:
        case GL_MAP2_TEXTURE_COORD_3:
            return 3;
        case GL_MAP1_COLOR_4:
        case GL_MAP2_COLOR_4:
        case GL_MAP1_VERTEX_4:
        case GL_MAP2_VERTEX_4:
        case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP2_TEXTURE_COORD_4:
            return 4;
        default:
            return 0;
    }
}
size_t gl_map_count(uint32_t target, int32_t us, int32_t un, int32_t vs, int32_t vn)
{
    unsigned components = map_components(target);
    if (!components || us < (int32_t)components || un < 1 || vn < 1 ||
        (vs && vs < (int32_t)components))
        return SIZE_MAX;
    uint64_t count = (uint64_t)(un - 1) * us + (uint64_t)(vn - 1) * vs + components;
    return count > GL_MAX_TRANSFER ? SIZE_MAX : (size_t)count;
}
size_t gl_map_query_count(GlContext *c, uint32_t target, uint32_t query)
{
    unsigned components = map_components(target);
    int dimensions = target >= GL_MAP2_COLOR_4 && target <= GL_MAP2_VERTEX_4 ? 2 : 1;
    if (!components)
        return SIZE_MAX;
    if (query == GL_ORDER)
        return dimensions;
    if (query == GL_DOMAIN)
        return dimensions * 2;
    if (query != GL_COEFF)
        return SIZE_MAX;
    int32_t order[2] = {0, 1};
    c->api->glGetMapiv(target, GL_ORDER, order);
    uint64_t count = (uint64_t)(uint32_t)order[0] * (uint32_t)order[1] * components;
    return count > GL_MAX_TRANSFER ? SIZE_MAX : (size_t)count;
}
void gl_fix_query(GlContext *c, uint32_t pname, void *output, size_t unit, int floating)
{
    if (!output)
        return;
    uint32_t value;
    if (pname == GL_DOUBLEBUFFER)
        value = c->config.double_buffer;
    else if (pname == GL_DRAW_BUFFER || pname == GL_READ_BUFFER)
        value = lx_host_gl_buffer(c->host, pname == GL_READ_BUFFER);
    else
    {
        int index = -1, field = -1;
        static const uint32_t queries[][3] = {
            {GL_VERTEX_ARRAY_SIZE, GL_VERTEX_ARRAY_TYPE, GL_VERTEX_ARRAY_STRIDE},
            {0, GL_NORMAL_ARRAY_TYPE, GL_NORMAL_ARRAY_STRIDE},
            {GL_COLOR_ARRAY_SIZE, GL_COLOR_ARRAY_TYPE, GL_COLOR_ARRAY_STRIDE},
            {0, GL_INDEX_ARRAY_TYPE, GL_INDEX_ARRAY_STRIDE},
            {GL_TEXTURE_COORD_ARRAY_SIZE, GL_TEXTURE_COORD_ARRAY_TYPE,
             GL_TEXTURE_COORD_ARRAY_STRIDE},
            {0, 0, GL_EDGE_FLAG_ARRAY_STRIDE}};
        for (unsigned i = 0; i < GL_ARRAY_COUNT; i++)
            for (unsigned n = 0; n < 3; n++)
                if (queries[i][n] && queries[i][n] == pname)
                {
                    index = i;
                    field = n;
                }
        if (index < 0)
            return;
        GlArray *p = &c->arrays[index];
        value = field == 0 ? (uint32_t)p->size : field == 1 ? p->type : (uint32_t)p->stride;
    }
    if (floating && unit == 8)
        *(double *)output = value;
    else if (floating)
        *(float *)output = (float)value;
    else if (unit == 1)
        *(uint8_t *)output = !!value;
    else
        *(uint32_t *)output = value;
}
