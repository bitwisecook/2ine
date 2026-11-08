# Public domain. No rights reserved. Wholly AI-authored.
# Mesa EGL rendering contexts for the OpenGL module.
twoine_host_function(opengl opengl/egl_context.c)
twoine_host_requires(opengl INCLUDE "${TWOINE_EGL_INCLUDE_DIR}")
twoine_host_requires(opengl LINK ${TWOINE_EGL_LIBRARY})
