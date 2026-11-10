/* Public domain. No rights reserved. Wholly AI-authored. */
/* PGL, OS/2's OpenGL binding to Presentation Manager: pglChooseConfig attributes. */
#ifndef _INCL_GL_PGL_H_
#define _INCL_GL_PGL_H_

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        PGL_USE_GL = 1,
        PGL_BUFFER_SIZE = 2,
        PGL_LEVEL = 3,
        PGL_RGBA = 4,
        PGL_DOUBLEBUFFER = 5,
        PGL_STEREO = 6,
        PGL_AUX_BUFFERS = 7,
        PGL_RED_SIZE = 8,
        PGL_GREEN_SIZE = 9,
        PGL_BLUE_SIZE = 10,
        PGL_ALPHA_SIZE = 11,
        PGL_DEPTH_SIZE = 12,
        PGL_STENCIL_SIZE = 13,
        PGL_ACCUM_RED_SIZE = 14,
        PGL_ACCUM_GREEN_SIZE = 15,
        PGL_ACCUM_BLUE_SIZE = 16,
        PGL_ACCUM_ALPHA_SIZE = 17,
        PGL_SINGLEBUFFER = 18
    };

#ifdef __cplusplus
}
#endif

#endif
