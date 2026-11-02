/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared views retain their object; a new object never inherits retired bytes. */
#include "hal/host_shared.h"
#include "hal/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void check_lifetime(size_t size)
{
    uint32_t pid, parent;
    lx_host_process_ids(&pid, &parent);
    char name[64];
    snprintf(name, sizeof name, "/twoine-lifetime-%u", pid);
    LxHostShared *first, *second, *missing;
    CHECK(lx_host_shared_open(name, size, &first) == LX_HOST_SHARED_OK);
    CHECK(lx_host_shared_size(first) == size);
    CHECK(lx_host_shared_live(name) == 1);
    CHECK(lx_host_shared_open(name, size, &missing) == LX_HOST_SHARED_EXISTS);
    CHECK(lx_host_shared_open(name, 0, &second) == LX_HOST_SHARED_OK);
    CHECK(lx_host_shared_size(second) == size);
    uint64_t slot;
    CHECK(lx_host_shared_slot(first, 0x30000000, 0x70000000, 65536, &slot));
    CHECK(lx_host_shared_publish(first, pid, 0x103));
    uint32_t owner, attributes, permissions;
    CHECK(lx_host_shared_query_access(second, parent, &owner, &attributes, &permissions));
    CHECK(owner == pid && attributes == 0x103 && !permissions);
    CHECK(lx_host_shared_grant(first, parent, 1));
    CHECK(lx_host_shared_grant(first, parent, 2));
    CHECK(lx_host_shared_query_access(second, parent, &owner, &attributes, &permissions));
    CHECK(permissions == 3);
    LxHostShared *found;
    char identified[128];
    CHECK(lx_host_shared_find(slot, &found, identified, sizeof identified) == LX_HOST_SHARED_OK);
    CHECK(!strcmp(identified, name) && lx_host_shared_size(found) == size);
    lx_host_shared_close(found);
    CHECK(lx_host_shared_find(slot + 1, &found, identified, sizeof identified) ==
          LX_HOST_SHARED_MISSING);
    unsigned char *a = lx_host_shared_map(first, NULL, 0);
    unsigned char *b = lx_host_shared_map(second, NULL, 0);
    CHECK(a && b && a[0] == 0);
    a[0] = 123;
    a[size - 1] = 42;
    CHECK(b[0] == 123);
    CHECK(b[size - 1] == 42);
    lx_host_shared_close(first);
    lx_host_shared_close(second);
    CHECK(lx_host_shared_live(name) == 1);
    CHECK(lx_host_shared_open(name, 0, &first) == LX_HOST_SHARED_OK);
    CHECK(lx_host_shared_size(first) == size);
    lx_host_shared_close(first);
    lx_host_shared_unmap(a, size);
    CHECK(b[0] == 123);
    lx_host_shared_unmap(b, size);
    CHECK(lx_host_shared_live(name) == 0);
    CHECK(lx_host_shared_open(name, 0, &missing) == LX_HOST_SHARED_MISSING);
    CHECK(lx_host_shared_find(slot, &found, identified, sizeof identified) ==
          LX_HOST_SHARED_MISSING);
    CHECK(lx_host_shared_open(name, size, &first) == LX_HOST_SHARED_OK);
    CHECK(!lx_host_shared_query_access(first, parent, &owner, &attributes, &permissions));
    CHECK(lx_host_shared_find(slot, &found, identified, sizeof identified) ==
          LX_HOST_SHARED_MISSING);
    a = lx_host_shared_map(first, NULL, 0);
    CHECK(a && a[0] == 0 && a[size - 1] == 0);
    lx_host_shared_unmap(a, size);
    lx_host_shared_close(first);
}
int main(void)
{
    check_lifetime(8);
    check_lifetime(12);
    check_lifetime(4096);
    puts("Shared object handles, mapped-view references and retired-object reset passed");
    return 0;
}
