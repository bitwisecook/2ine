/* Public domain. No rights reserved. Wholly AI-authored. */
/* Mesa chooses the Linux hardware driver; LIBGL_ALWAYS_SOFTWARE selects its
   software renderer for headless contracts. No Vulkan backend is selected. */
#include "hal/opengl.h"
int lx_host_gl_platform_prepare(char *error, size_t size)
{
    (void)error;
    (void)size;
    return 1;
}
int lx_host_gl_platform_renderer(const char *renderer)
{
    return renderer && *renderer;
}
const char *lx_host_gl_backend(void)
{
    return "mesa";
}
