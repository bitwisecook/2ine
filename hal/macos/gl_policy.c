/* Public domain. No rights reserved. Wholly AI-authored. */
/* The selected Mesa installation owns Zink and its KosmicKrisp ICD. Apple
   OpenGL, MoltenVK and software fallback are not accepted by this adapter. */
#include "hal/opengl.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int lx_host_gl_platform_prepare(char *error, size_t size)
{
    const char *icd = getenv("TWOINE_KOSMICKRISP_ICD");
    if (!icd || !*icd)
        icd = TWOINE_KOSMICKRISP_ICD;
    if (setenv("VK_DRIVER_FILES", icd, 1) || setenv("GALLIUM_DRIVER", "zink", 1) ||
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "zink", 1) || unsetenv("VK_ADD_DRIVER_FILES") ||
        unsetenv("LIBGL_ALWAYS_SOFTWARE"))
    {
        snprintf(error, size, "Cannot select the KosmicKrisp Mesa renderer");
        return 0;
    }
    return 1;
}
int lx_host_gl_platform_renderer(const char *renderer)
{
    return renderer && strstr(renderer, "zink") &&
           (strstr(renderer, "MESA_KOSMICKRISP") || strstr(renderer, "KosmicKrisp"));
}
const char *lx_host_gl_backend(void)
{
    return "mesa-zink-kosmickrisp-metal";
}
