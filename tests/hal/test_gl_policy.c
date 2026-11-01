/* Public domain. No rights reserved. Wholly AI-authored. */
/* macOS driver-selection policy can be checked independently of a GPU. */
#include "hal/opengl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    char error[256];
    CHECK(!lx_host_gl_platform_renderer(NULL));
    CHECK(!lx_host_gl_platform_renderer("Apple M4"));
    CHECK(!lx_host_gl_platform_renderer("llvmpipe"));
    CHECK(!lx_host_gl_platform_renderer("zink Vulkan 1.3(Apple M4 (MOLTENVK))"));
    CHECK(lx_host_gl_platform_renderer("zink Vulkan 1.3(Apple M4 (MESA_KOSMICKRISP))"));
    CHECK(!setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1));
    CHECK(!setenv("VK_ADD_DRIVER_FILES", "other.json", 1));
    CHECK(lx_host_gl_platform_prepare(error, sizeof error));
    CHECK(!strcmp(getenv("VK_DRIVER_FILES"), "/twoine-test/kosmickrisp.json"));
    CHECK(!strcmp(getenv("GALLIUM_DRIVER"), "zink"));
    CHECK(!strcmp(getenv("MESA_LOADER_DRIVER_OVERRIDE"), "zink"));
    CHECK(!getenv("LIBGL_ALWAYS_SOFTWARE") && !getenv("VK_ADD_DRIVER_FILES"));
    puts("KosmicKrisp selection policy passed; this is not a macOS GPU test");
    return 0;
}
