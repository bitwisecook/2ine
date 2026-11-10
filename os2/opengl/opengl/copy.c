/* Public domain. No rights reserved. Wholly AI-authored. */
/* PGL copies server attributes, retaining independent client state and matrices. */
#include "os2/opengl/opengl/internal.h"

enum
{
    /* Bounds host-reported light and clip-plane counts so snapshots fit on the stack. */
    HOST_PLANE_LIMIT = 256,
    /* A 32x32 one-bit polygon stipple. */
    POLYGON_STIPPLE_BYTES = 32 * 32 / 8
};

typedef struct GlCopy
{
    GlContext *source, *destination;
    int failed;
} GlCopy;

static const LxHostGlApi *bind_copy(GlCopy *copy, int destination)
{
    GlContext *c = destination ? copy->destination : copy->source;
    if (!lx_host_gl_make_current(c->host, c->width ? c->width : 1, c->height ? c->height : 1))
        copy->failed = 1;
    return c->api;
}

static void copy_enable(GlCopy *copy, uint32_t token)
{
    int enabled = bind_copy(copy, 0)->glIsEnabled(token);
    const LxHostGlApi *api = bind_copy(copy, 1);
    if (enabled)
        api->glEnable(token);
    else
        api->glDisable(token);
}

static void copy_current(GlCopy *copy)
{
    const LxHostGlApi *source = bind_copy(copy, 0);
    double position[4], color[4], texture[4], distance, index;
    int32_t valid = 0;
    source->glGetDoublev(GL_CURRENT_RASTER_POSITION, position);
    source->glGetDoublev(GL_CURRENT_RASTER_COLOR, color);
    source->glGetDoublev(GL_CURRENT_RASTER_TEXTURE_COORDS, texture);
    source->glGetDoublev(GL_CURRENT_RASTER_DISTANCE, &distance);
    source->glGetDoublev(GL_CURRENT_RASTER_INDEX, &index);
    source->glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, &valid);
    const LxHostGlApi *api = bind_copy(copy, 1);
    int lighting = api->glIsEnabled(GL_LIGHTING), material = api->glIsEnabled(GL_COLOR_MATERIAL);
    int32_t mode, viewport[4], count;
    double model[GL_MATRIX_ELEMENTS], projection[GL_MATRIX_ELEMENTS], texmatrix[GL_MATRIX_ELEMENTS],
        depth[2];
    api->glGetIntegerv(GL_MATRIX_MODE, &mode);
    api->glGetIntegerv(GL_VIEWPORT, viewport);
    api->glGetDoublev(GL_DEPTH_RANGE, depth);
    api->glGetDoublev(GL_MODELVIEW_MATRIX, model);
    api->glGetDoublev(GL_PROJECTION_MATRIX, projection);
    api->glGetDoublev(GL_TEXTURE_MATRIX, texmatrix);
    api->glGetIntegerv(GL_MAX_CLIP_PLANES, &count);
    if (count < 0 || count > HOST_PLANE_LIMIT)
    {
        copy->failed = 1;
        return;
    }
    uint8_t enabled[HOST_PLANE_LIMIT];
    for (int n = 0; n < count; n++)
    {
        enabled[n] = api->glIsEnabled(GL_CLIP_PLANE0 + n);
        api->glDisable(GL_CLIP_PLANE0 + n);
    }
    api->glDisable(GL_LIGHTING);
    api->glDisable(GL_COLOR_MATERIAL);
    api->glMatrixMode(GL_MODELVIEW);
    api->glLoadIdentity();
    api->glMatrixMode(GL_TEXTURE);
    api->glLoadIdentity();
    api->glMatrixMode(GL_PROJECTION);
    api->glViewport(0, 0, 1, 1);
    api->glDepthRange(0, 1);
    api->glColor4dv(color);
    api->glTexCoord4dv(texture);
    api->glIndexd(index);
    if (valid)
    {
        /* Generate the saved clip W, depth and eye distance independently.
           A zero-size bitmap moves the valid raster XY outside the viewport too. */
        double transform[GL_MATRIX_ELEMENTS] = {0};
        transform[12] = -position[3];
        transform[13] = -position[3];
        transform[14] = (position[2] * 2 - 1) * position[3];
        transform[15] = position[3];
        api->glLoadMatrixd(transform);
        api->glRasterPos4d(0, 0, -distance, 1);
        api->glBitmap(0, 0, 0, 0, position[0], position[1], NULL);
    }
    else
    {
        /* x = 2 lies outside the clip volume, so the copied position is invalid too. */
        api->glLoadIdentity();
        api->glRasterPos4d(2, 0, 0, 1);
    }
    api->glMatrixMode(GL_MODELVIEW);
    api->glLoadMatrixd(model);
    api->glMatrixMode(GL_PROJECTION);
    api->glLoadMatrixd(projection);
    api->glMatrixMode(GL_TEXTURE);
    api->glLoadMatrixd(texmatrix);
    api->glMatrixMode(mode);
    api->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    api->glDepthRange(depth[0], depth[1]);
    for (int n = 0; n < count; n++)
        if (enabled[n])
            api->glEnable(GL_CLIP_PLANE0 + n);
    if (lighting)
        api->glEnable(GL_LIGHTING);
    double value[4];
    source = bind_copy(copy, 0);
    source->glGetDoublev(GL_CURRENT_COLOR, value);
    bind_copy(copy, 1)->glColor4dv(value);
    source = bind_copy(copy, 0);
    source->glGetDoublev(GL_CURRENT_NORMAL, value);
    bind_copy(copy, 1)->glNormal3dv(value);
    source = bind_copy(copy, 0);
    source->glGetDoublev(GL_CURRENT_TEXTURE_COORDS, value);
    bind_copy(copy, 1)->glTexCoord4dv(value);
    source = bind_copy(copy, 0);
    source->glGetDoublev(GL_CURRENT_INDEX, value);
    bind_copy(copy, 1)->glIndexd(value[0]);
    int32_t flag = 0;
    bind_copy(copy, 0)->glGetIntegerv(GL_EDGE_FLAG, &flag);
    api = bind_copy(copy, 1);
    api->glEdgeFlag(flag);
    if (material)
        api->glEnable(GL_COLOR_MATERIAL);
}

static void copy_server(GlCopy *copy, uint32_t mask)
{
    double v[GL_MATRIX_ELEMENTS] = {0};
    int32_t i[GL_MATRIX_ELEMENTS] = {0};
#define S() bind_copy(copy, 0)
#define D() bind_copy(copy, 1)
#define GET(p) S()->glGetDoublev(p, v)
#define INT(p) S()->glGetIntegerv(p, i)
#define ENABLE(p) copy_enable(copy, p)
#define SCALAR(group, token, setter)                                                               \
    do                                                                                             \
    {                                                                                              \
        if (mask & (group))                                                                        \
        {                                                                                          \
            GET(token);                                                                            \
            D()->setter(v[0]);                                                                     \
        }                                                                                          \
    } while (0)
#define ENABLE_GROUP(group, token)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (mask & ((group) | GL_ENABLE_BIT))                                                      \
            ENABLE(token);                                                                         \
    } while (0)
    SCALAR(GL_POINT_BIT, GL_POINT_SIZE, glPointSize);
    SCALAR(GL_LINE_BIT, GL_LINE_WIDTH, glLineWidth);
    SCALAR(GL_DEPTH_BUFFER_BIT, GL_DEPTH_CLEAR_VALUE, glClearDepth);
    SCALAR(GL_DEPTH_BUFFER_BIT, GL_DEPTH_FUNC, glDepthFunc);
    SCALAR(GL_DEPTH_BUFFER_BIT, GL_DEPTH_WRITEMASK, glDepthMask);
    SCALAR(GL_STENCIL_BUFFER_BIT, GL_STENCIL_CLEAR_VALUE, glClearStencil);
    SCALAR(GL_STENCIL_BUFFER_BIT, GL_STENCIL_WRITEMASK, glStencilMask);
    SCALAR(GL_POLYGON_BIT, GL_CULL_FACE_MODE, glCullFace);
    SCALAR(GL_POLYGON_BIT, GL_FRONT_FACE, glFrontFace);
    SCALAR(GL_LIGHTING_BIT, GL_SHADE_MODEL, glShadeModel);
    SCALAR(GL_LIST_BIT, GL_LIST_BASE, glListBase);
    SCALAR(GL_TRANSFORM_BIT, GL_MATRIX_MODE, glMatrixMode);
    SCALAR(GL_COLOR_BUFFER_BIT, GL_INDEX_CLEAR_VALUE, glClearIndex);
    SCALAR(GL_COLOR_BUFFER_BIT, GL_INDEX_WRITEMASK, glIndexMask);
    SCALAR(GL_COLOR_BUFFER_BIT, GL_LOGIC_OP_MODE, glLogicOp);
    ENABLE_GROUP(GL_POINT_BIT, GL_POINT_SMOOTH);
    ENABLE_GROUP(GL_LINE_BIT, GL_LINE_SMOOTH);
    ENABLE_GROUP(GL_LINE_BIT, GL_LINE_STIPPLE);
    ENABLE_GROUP(GL_POLYGON_BIT, GL_CULL_FACE);
    ENABLE_GROUP(GL_POLYGON_BIT, GL_POLYGON_SMOOTH);
    ENABLE_GROUP(GL_POLYGON_BIT, GL_POLYGON_STIPPLE);
    ENABLE_GROUP(GL_POLYGON_BIT, GL_POLYGON_OFFSET_POINT);
    ENABLE_GROUP(GL_POLYGON_BIT, GL_POLYGON_OFFSET_LINE);
    ENABLE_GROUP(GL_POLYGON_BIT, GL_POLYGON_OFFSET_FILL);
    ENABLE_GROUP(GL_DEPTH_BUFFER_BIT, GL_DEPTH_TEST);
    ENABLE_GROUP(GL_STENCIL_BUFFER_BIT, GL_STENCIL_TEST);
    ENABLE_GROUP(GL_LIGHTING_BIT, GL_LIGHTING);
    ENABLE_GROUP(GL_LIGHTING_BIT, GL_COLOR_MATERIAL);
    ENABLE_GROUP(GL_FOG_BIT, GL_FOG);
    ENABLE_GROUP(GL_TRANSFORM_BIT, GL_NORMALIZE);
    ENABLE_GROUP(GL_SCISSOR_BIT, GL_SCISSOR_TEST);
    ENABLE_GROUP(GL_EVAL_BIT, GL_AUTO_NORMAL);
    ENABLE_GROUP(GL_TEXTURE_BIT, GL_TEXTURE_1D);
    ENABLE_GROUP(GL_TEXTURE_BIT, GL_TEXTURE_2D);
    ENABLE_GROUP(GL_COLOR_BUFFER_BIT, GL_ALPHA_TEST);
    ENABLE_GROUP(GL_COLOR_BUFFER_BIT, GL_BLEND);
    ENABLE_GROUP(GL_COLOR_BUFFER_BIT, GL_DITHER);
    ENABLE_GROUP(GL_COLOR_BUFFER_BIT, GL_INDEX_LOGIC_OP);
    ENABLE_GROUP(GL_COLOR_BUFFER_BIT, GL_COLOR_LOGIC_OP);
    if (mask & GL_CURRENT_BIT)
        copy_current(copy);
    if (mask & GL_LINE_BIT)
    {
        INT(GL_LINE_STIPPLE_REPEAT);
        int repeat = i[0];
        INT(GL_LINE_STIPPLE_PATTERN);
        D()->glLineStipple(repeat, i[0]);
    }
    if (mask & GL_POLYGON_BIT)
    {
        INT(GL_POLYGON_MODE);
        D()->glPolygonMode(GL_FRONT, i[0]);
        D()->glPolygonMode(GL_BACK, i[1]);
        GET(GL_POLYGON_OFFSET_FACTOR);
        double factor = v[0];
        GET(GL_POLYGON_OFFSET_UNITS);
        D()->glPolygonOffset(factor, v[0]);
    }
    if (mask & GL_POLYGON_STIPPLE_BIT)
    {
        uint8_t stipple[POLYGON_STIPPLE_BYTES];
        const LxHostGlApi *api = S();
        api->glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        api->glPixelStorei(GL_PACK_ALIGNMENT, 1);
        api->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        api->glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        api->glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        api->glPixelStorei(GL_PACK_LSB_FIRST, 0);
        api->glGetPolygonStipple(stipple);
        api->glPopClientAttrib();
        api = D();
        api->glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
        api->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        api->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        api->glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        api->glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        api->glPixelStorei(GL_UNPACK_LSB_FIRST, 0);
        api->glPolygonStipple(stipple);
        api->glPopClientAttrib();
    }
    if (mask & GL_COLOR_BUFFER_BIT)
    {
        GET(GL_COLOR_CLEAR_VALUE);
        D()->glClearColor(v[0], v[1], v[2], v[3]);
        INT(GL_COLOR_WRITEMASK);
        D()->glColorMask(i[0], i[1], i[2], i[3]);
        INT(GL_ALPHA_TEST_FUNC);
        int func = i[0];
        GET(GL_ALPHA_TEST_REF);
        D()->glAlphaFunc(func, v[0]);
        INT(GL_BLEND_SRC);
        int src = i[0];
        INT(GL_BLEND_DST);
        D()->glBlendFunc(src, i[0]);
        S();
        uint32_t draw = lx_host_gl_buffer(copy->source->host, 0);
        D();
        if (!lx_host_gl_draw_buffer(copy->destination->host, draw))
            copy->failed = 1;
    }
    if (mask & GL_STENCIL_BUFFER_BIT)
    {
        INT(GL_STENCIL_FUNC);
        int func = i[0];
        INT(GL_STENCIL_REF);
        int ref = i[0];
        INT(GL_STENCIL_VALUE_MASK);
        D()->glStencilFunc(func, ref, (uint32_t)i[0]);
        INT(GL_STENCIL_FAIL);
        int fail = i[0];
        INT(GL_STENCIL_PASS_DEPTH_FAIL);
        int zfail = i[0];
        INT(GL_STENCIL_PASS_DEPTH_PASS);
        D()->glStencilOp(fail, zfail, i[0]);
    }
    if (mask & GL_ACCUM_BUFFER_BIT)
    {
        GET(GL_ACCUM_CLEAR_VALUE);
        D()->glClearAccum(v[0], v[1], v[2], v[3]);
    }
    if (mask & GL_VIEWPORT_BIT)
    {
        INT(GL_VIEWPORT);
        D()->glViewport(i[0], i[1], i[2], i[3]);
        GET(GL_DEPTH_RANGE);
        D()->glDepthRange(v[0], v[1]);
    }
    if (mask & GL_SCISSOR_BIT)
    {
        INT(GL_SCISSOR_BOX);
        D()->glScissor(i[0], i[1], i[2], i[3]);
    }
    if (mask & GL_HINT_BIT)
    {
        const uint32_t hints[] = {GL_PERSPECTIVE_CORRECTION_HINT, GL_POINT_SMOOTH_HINT,
                                  GL_LINE_SMOOTH_HINT, GL_POLYGON_SMOOTH_HINT, GL_FOG_HINT};
        for (unsigned n = 0; n < sizeof hints / sizeof hints[0]; n++)
        {
            INT(hints[n]);
            D()->glHint(hints[n], i[0]);
        }
    }
    if (mask & GL_FOG_BIT)
    {
        const uint32_t p[] = {GL_FOG_MODE, GL_FOG_DENSITY, GL_FOG_START, GL_FOG_END, GL_FOG_INDEX};
        for (unsigned n = 0; n < sizeof p / sizeof p[0]; n++)
        {
            GET(p[n]);
            D()->glFogf(p[n], v[0]);
        }
        float color[4];
        S()->glGetFloatv(GL_FOG_COLOR, color);
        D()->glFogfv(GL_FOG_COLOR, color);
    }
    INT(GL_MAX_LIGHTS);
    unsigned lights = i[0] > 0 && i[0] <= HOST_PLANE_LIMIT ? i[0] : 0;
    INT(GL_MAX_CLIP_PLANES);
    unsigned planes = i[0] > 0 && i[0] <= HOST_PLANE_LIMIT ? i[0] : 0;
    if (mask & (GL_LIGHTING_BIT | GL_ENABLE_BIT))
        for (unsigned n = 0; n < lights; n++)
            ENABLE(GL_LIGHT0 + n);
    if (mask & (GL_TRANSFORM_BIT | GL_ENABLE_BIT))
        for (unsigned n = 0; n < planes; n++)
            ENABLE(GL_CLIP_PLANE0 + n);
    if (mask & GL_LIGHTING_BIT)
    {
        const uint32_t model[] = {GL_LIGHT_MODEL_LOCAL_VIEWER, GL_LIGHT_MODEL_TWO_SIDE};
        for (unsigned n = 0; n < sizeof model / sizeof model[0]; n++)
        {
            INT(model[n]);
            D()->glLightModeli(model[n], i[0]);
        }
        float f[4];
        S()->glGetFloatv(GL_LIGHT_MODEL_AMBIENT, f);
        D()->glLightModelfv(GL_LIGHT_MODEL_AMBIENT, f);
        INT(GL_COLOR_MATERIAL_FACE);
        int face = i[0];
        INT(GL_COLOR_MATERIAL_PARAMETER);
        D()->glColorMaterial(face, i[0]);
        /* Positions and directions are already in eye space when queried. */
        const LxHostGlApi *api = D();
        int32_t matrix_mode;
        api->glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
        double modelview[GL_MATRIX_ELEMENTS];
        api->glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
        api->glMatrixMode(GL_MODELVIEW);
        api->glLoadIdentity();
        const uint32_t light[] = {GL_AMBIENT,
                                  GL_DIFFUSE,
                                  GL_SPECULAR,
                                  GL_POSITION,
                                  GL_SPOT_DIRECTION,
                                  GL_SPOT_EXPONENT,
                                  GL_SPOT_CUTOFF,
                                  GL_CONSTANT_ATTENUATION,
                                  GL_LINEAR_ATTENUATION,
                                  GL_QUADRATIC_ATTENUATION};
        for (unsigned n = 0; n < lights; n++)
            for (unsigned k = 0; k < sizeof light / sizeof light[0]; k++)
            {
                S()->glGetLightfv(GL_LIGHT0 + n, light[k], f);
                D()->glLightfv(GL_LIGHT0 + n, light[k], f);
            }
        api = D();
        api->glLoadMatrixd(modelview);
        api->glMatrixMode(matrix_mode);
        const uint32_t material[] = {GL_AMBIENT,  GL_DIFFUSE,   GL_SPECULAR,
                                     GL_EMISSION, GL_SHININESS, GL_COLOR_INDEXES};
        /* Color material must not replace explicitly copied material values. */
        int color_material = S()->glIsEnabled(GL_COLOR_MATERIAL);
        D()->glDisable(GL_COLOR_MATERIAL);
        for (unsigned side = 0; side < 2; side++)
            for (unsigned n = 0; n < sizeof material / sizeof material[0]; n++)
            {
                S()->glGetMaterialfv(side ? GL_BACK : GL_FRONT, material[n], f);
                D()->glMaterialfv(side ? GL_BACK : GL_FRONT, material[n], f);
            }
        if (color_material)
            D()->glEnable(GL_COLOR_MATERIAL);
    }
    if (mask & GL_TRANSFORM_BIT)
    {
        const LxHostGlApi *api = D();
        int32_t mode;
        api->glGetIntegerv(GL_MATRIX_MODE, &mode);
        double modelview[GL_MATRIX_ELEMENTS];
        api->glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
        api->glMatrixMode(GL_MODELVIEW);
        api->glLoadIdentity();
        for (unsigned n = 0; n < planes; n++)
        {
            S()->glGetClipPlane(GL_CLIP_PLANE0 + n, v);
            D()->glClipPlane(GL_CLIP_PLANE0 + n, v);
        }
        api = D();
        api->glLoadMatrixd(modelview);
        api->glMatrixMode(mode);
    }
    if (mask & GL_PIXEL_MODE_BIT)
    {
        const uint32_t p[] = {GL_MAP_COLOR,   GL_MAP_STENCIL, GL_INDEX_SHIFT, GL_INDEX_OFFSET,
                              GL_RED_SCALE,   GL_RED_BIAS,    GL_GREEN_SCALE, GL_GREEN_BIAS,
                              GL_BLUE_SCALE,  GL_BLUE_BIAS,   GL_ALPHA_SCALE, GL_ALPHA_BIAS,
                              GL_DEPTH_SCALE, GL_DEPTH_BIAS};
        for (unsigned n = 0; n < sizeof p / sizeof p[0]; n++)
        {
            GET(p[n]);
            D()->glPixelTransferf(p[n], v[0]);
        }
        GET(GL_ZOOM_X);
        double x = v[0];
        GET(GL_ZOOM_Y);
        D()->glPixelZoom(x, v[0]);
        S();
        uint32_t read = lx_host_gl_buffer(copy->source->host, 1);
        D();
        if (!lx_host_gl_read_buffer(copy->destination->host, read))
            copy->failed = 1;
        for (uint32_t token = GL_PIXEL_MAP_I_TO_I; token <= GL_PIXEL_MAP_A_TO_A; token++)
        {
            INT(token + GL_PIXEL_MAP_I_TO_I_SIZE - GL_PIXEL_MAP_I_TO_I);
            if (i[0] < 1 || i[0] > (int32_t)(GL_MAX_TRANSFER / sizeof(float)))
            {
                copy->failed = 1;
                break;
            }
            float *map = malloc((size_t)i[0] * sizeof *map);
            if (!map)
            {
                copy->failed = 1;
                break;
            }
            int count = i[0];
            S()->glGetPixelMapfv(token, map);
            D()->glPixelMapfv(token, count, map);
            free(map);
        }
    }
    for (uint32_t target = GL_MAP1_COLOR_4; target <= GL_MAP1_VERTEX_4; target++)
    {
        if (mask & (GL_EVAL_BIT | GL_ENABLE_BIT))
        {
            ENABLE(target);
            ENABLE(target + GL_MAP2_COLOR_4 - GL_MAP1_COLOR_4);
        }
        if (!(mask & GL_EVAL_BIT))
            continue;
        unsigned components = target == GL_MAP1_VERTEX_3 || target == GL_MAP1_NORMAL ||
                                      target == GL_MAP1_TEXTURE_COORD_3
                                  ? 3
                              : target == GL_MAP1_COLOR_4 || target == GL_MAP1_VERTEX_4 ||
                                      target == GL_MAP1_TEXTURE_COORD_4
                                  ? 4
                              : target == GL_MAP1_TEXTURE_COORD_2 ? 2
                                                                  : 1;
        for (unsigned dim = 1; dim <= 2; dim++)
        {
            uint32_t t = dim == 1 ? target : target + GL_MAP2_COLOR_4 - GL_MAP1_COLOR_4;
            int32_t order[2] = {1, 1};
            double domain[4] = {0, 1, 0, 1};
            S()->glGetMapiv(t, GL_ORDER, order);
            S()->glGetMapdv(t, GL_DOMAIN, domain);
            size_t count =
                order[0] > 0 && order[1] > 0 ? (size_t)order[0] * order[1] * components : 0;
            if (!count || count > GL_MAX_TRANSFER / sizeof(double))
            {
                copy->failed = 1;
                continue;
            }
            double *points = malloc(count * sizeof *points);
            if (!points)
            {
                copy->failed = 1;
                continue;
            }
            S()->glGetMapdv(t, GL_COEFF, points);
            if (dim == 1)
                D()->glMap1d(t, domain[0], domain[1], components, order[0], points);
            else
                D()->glMap2d(t, domain[0], domain[1], components * order[1], order[0], domain[2],
                             domain[3], components, order[1], points);
            free(points);
        }
    }
    if (mask & GL_EVAL_BIT)
    {
        GET(GL_MAP1_GRID_DOMAIN);
        double domain[4];
        memcpy(domain, v, sizeof domain);
        INT(GL_MAP1_GRID_SEGMENTS);
        D()->glMapGrid1d(i[0], domain[0], domain[1]);
        GET(GL_MAP2_GRID_DOMAIN);
        memcpy(domain, v, sizeof domain);
        INT(GL_MAP2_GRID_SEGMENTS);
        D()->glMapGrid2d(i[0], domain[0], domain[1], i[1], domain[2], domain[3]);
    }
    for (uint32_t coord = GL_S; coord <= GL_Q; coord++)
    {
        if (mask & (GL_TEXTURE_BIT | GL_ENABLE_BIT))
            ENABLE(GL_TEXTURE_GEN_S + coord - GL_S);
        if (!(mask & GL_TEXTURE_BIT))
            continue;
        S()->glGetTexGeniv(coord, GL_TEXTURE_GEN_MODE, i);
        D()->glTexGeni(coord, GL_TEXTURE_GEN_MODE, i[0]);
        S()->glGetTexGendv(coord, GL_OBJECT_PLANE, v);
        D()->glTexGendv(coord, GL_OBJECT_PLANE, v);
        S()->glGetTexGendv(coord, GL_EYE_PLANE, v);
        const LxHostGlApi *api = D();
        int32_t mode;
        api->glGetIntegerv(GL_MATRIX_MODE, &mode);
        double modelview[GL_MATRIX_ELEMENTS];
        api->glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
        api->glMatrixMode(GL_MODELVIEW);
        api->glLoadIdentity();
        api->glTexGendv(coord, GL_EYE_PLANE, v);
        api->glLoadMatrixd(modelview);
        api->glMatrixMode(mode);
    }
    if (mask & GL_TEXTURE_BIT)
    {
        float env[4];
        S()->glGetTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, env);
        D()->glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, env);
        S()->glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, i);
        D()->glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, i[0]);
        const uint32_t p[] = {GL_TEXTURE_MAG_FILTER, GL_TEXTURE_MIN_FILTER,   GL_TEXTURE_WRAP_S,
                              GL_TEXTURE_WRAP_T,     GL_TEXTURE_BORDER_COLOR, GL_TEXTURE_PRIORITY};
        for (unsigned n = 0; n < 2; n++)
        {
            uint32_t t = n ? GL_TEXTURE_2D : GL_TEXTURE_1D;
            INT(n ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_1D);
            D()->glBindTexture(t, (uint32_t)i[0]);
            for (unsigned k = 0; k < sizeof p / sizeof p[0]; k++)
            {
                S()->glGetTexParameterfv(t, p[k], env);
                D()->glTexParameterfv(t, p[k], env);
            }
        }
    }
#undef S
#undef D
#undef GET
#undef INT
#undef ENABLE
#undef SCALAR
#undef ENABLE_GROUP
}

uint32_t gl_copy_context(uint8_t *arguments)
{
    GlThread *thread = gl_thread();
    if (!thread || gl_word(arguments, 0) != WinQueryAnchorBlock(HWND_DESKTOP) ||
        (gl_word(arguments, 12) & ~GL_ALL_ATTRIB_BITS))
        return 0;
    uint32_t result = 0;
    lx_host_monitor_lock(GlLock);
    LxHandleBorrow leases[2] = {{0}};
    if (!lx_handle_borrow(gl_word(arguments, 4), LX_HANDLE_GL_CONTEXT, &leases[0]) ||
        !lx_handle_borrow(gl_word(arguments, 8), LX_HANDLE_GL_CONTEXT, &leases[1]))
        goto done;
    GlContext *source = leases[0].resource, *destination = leases[1].resource;
    if (destination->owner || destination->inside || destination->front_bitmap || source->inside ||
        source->front_bitmap || (source->owner && source->owner != lx_host_thread_id()))
        goto done;
    GlCopy copy = {source, destination, 0};
    copy_server(&copy, gl_word(arguments, 12));
    result = !copy.failed;
done:
    if (thread->current)
        lx_host_gl_make_current(thread->current->host, thread->current->width,
                                thread->current->height);
    else
        lx_host_gl_release();
    lx_handle_release(&leases[1]);
    lx_handle_release(&leases[0]);
    lx_host_monitor_unlock(GlLock);
    return result;
}
