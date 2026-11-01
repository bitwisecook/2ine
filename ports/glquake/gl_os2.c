/* Public domain. No rights reserved.
 * Texture objects for the GL 1.0 interface shipped with Warp 4.52.
 * A display list restores each texture's complete mip chain and sampler state.
 * GL 1.1 providers use their own texture objects and subimage implementation.
 */
#define QGL_IMPLEMENTATION
#include "quakedef.h"
#define INCL_DOSMODULEMGR
#include <os2.h>
#include "gl_os2.h"
#define TEXTURES 4096
#define LEVELS 16

typedef void(APIENTRY *bind_fn)(GLenum, GLuint);
typedef void(APIENTRY *sub_fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                               const GLvoid *);
static bind_fn native_bind;
static sub_fn native_sub;
static HMODULE module;
typedef struct
{
    int width, height, components, channels;
    GLenum format;
    byte *pixels;
} mip_t;
typedef struct
{
    mip_t mip[LEVELS];
    GLuint list;
    int dirty;
    GLfloat min, mag, s, t;
} qgl_texture_t;
static qgl_texture_t *textures[TEXTURES];
static GLuint current;

static qgl_texture_t *texture(void)
{
    qgl_texture_t *t;
    if (current >= TEXTURES)
        Sys_Error("Texture number out of range");
    t = textures[current];
    if (!t)
    {
        t = calloc(1, sizeof(*t));
        if (!t)
            Sys_Error("Texture allocation failed");
        t->min = GL_NEAREST_MIPMAP_LINEAR;
        t->mag = GL_LINEAR;
        t->s = t->t = GL_REPEAT;
        t->dirty = 1;
        textures[current] = t;
    }
    return t;
}

void QGL_InitTextures(void)
{
    char error[256];
    const char *version = (const char *)glGetString(GL_VERSION);
    PFN bind = NULL, sub = NULL;
    if (!COM_CheckParm("-gl10") && version && atof(version) >= 1.1 &&
        !DosLoadModule(error, sizeof(error), "OPENGL", &module))
    {
        DosQueryProcAddr(module, 0, "glBindTexture", &bind);
        DosQueryProcAddr(module, 0, "glTexSubImage2D", &sub);
        if (bind && sub)
        {
            native_bind = (bind_fn)bind;
            native_sub = (sub_fn)sub;
        }
    }
    Con_Printf("OS/2 textures: %s\n", native_bind ? "native GL 1.1" : "GL 1.0 display lists");
}

void QGL_BindTexture(GLenum target, GLuint name)
{
    qgl_texture_t *t;
    int i;
    if (native_bind)
    {
        native_bind(target, name);
        return;
    }
    if (target != GL_TEXTURE_2D)
        Sys_Error("Unsupported texture target");
    current = name;
    t = texture();
    if (t->dirty)
    {
        if (!t->list)
            t->list = glGenLists(1);
        if (!t->list)
            Sys_Error("Texture display list allocation failed");
        glNewList(t->list, GL_COMPILE);
        for (i = 0; i < LEVELS; ++i)
        {
            mip_t *m = &t->mip[i];
            if (m->pixels)
                glTexImage2D(target, i, m->components, m->width, m->height, 0, m->format,
                             GL_UNSIGNED_BYTE, m->pixels);
        }
        glTexParameterf(target, GL_TEXTURE_MIN_FILTER, t->min);
        glTexParameterf(target, GL_TEXTURE_MAG_FILTER, t->mag);
        glTexParameterf(target, GL_TEXTURE_WRAP_S, t->s);
        glTexParameterf(target, GL_TEXTURE_WRAP_T, t->t);
        glEndList();
        t->dirty = 0;
    }
    glCallList(t->list);
}

void QGL_TexImage2D(GLenum target, GLint level, GLint components, GLsizei width, GLsizei height,
                    GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    qgl_texture_t *t;
    mip_t *m;
    int channels;
    size_t size;
    if (!native_bind)
    {
        channels = format == GL_RGBA              ? 4
                   : format == GL_RGB             ? 3
                   : format == GL_LUMINANCE_ALPHA ? 2
                                                  : 1;
        if (target != GL_TEXTURE_2D || level < 0 || level >= LEVELS || border ||
            type != GL_UNSIGNED_BYTE || width < 1 || height < 1 || width > 4096 || height > 4096 ||
            !pixels)
            Sys_Error("Unsupported GL 1.0 texture image");
        t = texture();
        m = &t->mip[level];
        size = (size_t)width * height * channels;
        free(m->pixels);
        m->pixels = malloc(size);
        if (!m->pixels)
            Sys_Error("Texture image allocation failed");
        memcpy(m->pixels, pixels, size);
        m->width = width;
        m->height = height;
        m->components = components;
        m->channels = channels;
        m->format = format;
        t->dirty = 1;
    }
    glTexImage2D(target, level, components, width, height, border, format, type, pixels);
}

void QGL_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, const GLvoid *pixels)
{
    qgl_texture_t *t;
    mip_t *m;
    int row;
    if (native_sub)
    {
        native_sub(target, level, x, y, width, height, format, type, pixels);
        return;
    }
    t = texture();
    if (level < 0 || level >= LEVELS)
        Sys_Error("Invalid subimage level");
    m = &t->mip[level];
    if (!m->pixels || x < 0 || y < 0 || width < 0 || height < 0 || x > m->width - width ||
        y > m->height - height || format != m->format || type != GL_UNSIGNED_BYTE)
        Sys_Error("Invalid texture subimage");
    for (row = 0; row < height; ++row)
        memcpy(m->pixels + ((y + row) * m->width + x) * m->channels,
               (const byte *)pixels + row * width * m->channels, width * m->channels);
    t->dirty = 1;
    glTexImage2D(target, level, m->components, m->width, m->height, 0, m->format, type, m->pixels);
}

void QGL_TexParameterf(GLenum target, GLenum name, GLfloat value)
{
    if (!native_bind)
    {
        qgl_texture_t *t = texture();
        switch (name)
        {
            case GL_TEXTURE_MIN_FILTER:
                t->min = value;
                break;
            case GL_TEXTURE_MAG_FILTER:
                t->mag = value;
                break;
            case GL_TEXTURE_WRAP_S:
                t->s = value;
                break;
            case GL_TEXTURE_WRAP_T:
                t->t = value;
                break;
            default:
                Sys_Error("Unsupported texture parameter");
        }
        t->dirty = 1;
    }
    glTexParameterf(target, name, value);
}

void QGL_ShutdownTextures(void)
{
    int i, j;
    for (i = 0; i < TEXTURES; ++i)
        if (textures[i])
        {
            for (j = 0; j < LEVELS; ++j)
                free(textures[i]->mip[j].pixels);
            if (textures[i]->list)
                glDeleteLists(textures[i]->list, 1);
            free(textures[i]);
            textures[i] = NULL;
        }
    native_bind = NULL;
    native_sub = NULL;
    if (module)
    {
        DosFreeModule(module);
        module = 0;
    }
}
