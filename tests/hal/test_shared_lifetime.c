/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared views retain their object; a new object never inherits retired bytes. */
#include "hal/host_shared.h"
#include "hal/host.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
int main(void)
{
    uint32_t pid, parent;
    lx_host_process_ids(&pid, &parent);
    char name[64];
    snprintf(name, sizeof name, "/twoine-lifetime-%u", pid);
    LxHostShared *first, *second, *missing;
    CHECK(lx_host_shared_open(name, 4096, &first) == LX_HOST_SHARED_OK);
    CHECK(lx_host_shared_open(name, 4096, &missing) == LX_HOST_SHARED_EXISTS);
    CHECK(lx_host_shared_open(name, 0, &second) == LX_HOST_SHARED_OK);
    unsigned char *a = lx_host_shared_map(first, NULL, 0);
    unsigned char *b = lx_host_shared_map(second, NULL, 0);
    CHECK(a && b && a[0] == 0);
    a[0] = 123;
    CHECK(b[0] == 123);
    lx_host_shared_close(first);
    lx_host_shared_close(second);
    CHECK(lx_host_shared_open(name, 0, &first) == LX_HOST_SHARED_OK);
    lx_host_shared_close(first);
    lx_host_shared_unmap(a, 4096);
    CHECK(b[0] == 123);
    lx_host_shared_unmap(b, 4096);
    CHECK(lx_host_shared_open(name, 0, &missing) == LX_HOST_SHARED_MISSING);
    CHECK(lx_host_shared_open(name, 4096, &first) == LX_HOST_SHARED_OK);
    a = lx_host_shared_map(first, NULL, 0);
    CHECK(a && a[0] == 0);
    lx_host_shared_unmap(a, 4096);
    lx_host_shared_close(first);
    puts("Shared object handles, mapped-view references and retired-object reset passed");
    return 0;
}
