/* Public domain. No rights reserved. */
#ifndef GL_OS2_H
#define GL_OS2_H
#include <GL/gl.h>
#ifndef GL_INTENSITY
#define GL_INTENSITY 0x8049
#endif
void QGL_InitTextures(void);
void QGL_ShutdownTextures(void);
void QGL_BindTexture(GLenum target, GLuint name);
void QGL_TexImage2D(GLenum target, GLint level, GLint components, GLsizei width, GLsizei height,
                    GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void QGL_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, const GLvoid *pixels);
void QGL_TexParameterf(GLenum target, GLenum name, GLfloat value);
#ifndef QGL_IMPLEMENTATION
#define glBindTexture QGL_BindTexture
#define glTexImage2D QGL_TexImage2D
#define glTexSubImage2D QGL_TexSubImage2D
#define glTexParameterf QGL_TexParameterf
#endif
#endif
