/* Resource lifetime across duplicate names, stale handles and active API calls. */
#include "handles.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static unsigned Destroyed;
static int Ready;
static int ready(void *p) { (void) p; return Ready; }
static void destroy(void *p) { Destroyed++; free(p); }
int main(void)
{
    uint64_t *resource = malloc(sizeof *resource); CHECK(resource); *resource = UINT64_C(0x123456789abcdef0);
    uint32_t a = lx_handle_create(LX_HANDLE_EVENT, resource, destroy);
    uint32_t b = lx_handle_create(LX_HANDLE_EVENT, resource, destroy);
    CHECK(a && b && a != b && a > UINT16_MAX);
    CHECK(!lx_handle_create(LX_HANDLE_EVENT, resource, free));
    CHECK(!lx_handle_create(LX_HANDLE_MUTEX, resource, destroy));
    LxHandleBorrow borrow;
    CHECK(!lx_handle_borrow(a, LX_HANDLE_MUTEX, &borrow));
    CHECK(!lx_handle_close(a, LX_HANDLE_MUTEX));
    CHECK(lx_handle_borrow(a, LX_HANDLE_EVENT, &borrow));
    CHECK(borrow.resource == resource);
    CHECK(lx_handle_count(LX_HANDLE_EVENT, resource) == 2);
    CHECK(lx_handle_close_guarded(a, LX_HANDLE_EVENT, 0, NULL) == -1);
    CHECK(lx_handle_close_guarded(a, LX_HANDLE_EVENT, 1, ready) == -1);
    Ready = 1;
    CHECK(lx_handle_close_guarded(a, LX_HANDLE_EVENT, 1, ready) == 1);
    CHECK(!lx_handle_close(a, LX_HANDLE_EVENT));
    LxHandleBorrow stale;
    CHECK(!lx_handle_borrow(a, LX_HANDLE_EVENT, &stale));
    CHECK(lx_handle_close(b, LX_HANDLE_EVENT));
    CHECK(Destroyed == 0 && *(uint64_t *) borrow.resource == UINT64_C(0x123456789abcdef0));
    lx_handle_release(&borrow); CHECK(Destroyed == 1);
    lx_handle_release(&borrow); CHECK(Destroyed == 1);
    /* Exhaust and reuse slots, proving a stale integer cannot name a new object. */
    for (unsigned i = 0; i < 70000; i++) {
        resource = malloc(sizeof *resource); CHECK(resource);
        uint32_t h = lx_handle_create(LX_HANDLE_EVENT, resource, destroy); CHECK(h && h != a);
        CHECK(!lx_handle_borrow(a, LX_HANDLE_EVENT, &stale));
        CHECK(lx_handle_close(h, LX_HANDLE_EVENT));
    }
    CHECK(Destroyed == 70001);
    puts("fixed-width typed handles, duplicate ownership, pinned lifetime and stale-slot rejection passed");
    return 0;
}
