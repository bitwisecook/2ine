/* Public domain. No rights reserved. Wholly AI-authored. */
/* Linux robust mutex recovery restores ownership after a thread exits. */
#include "host_sync.h"
#include "host_thread.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                       \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static void *owner(void *argument)
{
    CHECK(lx_host_mutex_lock(argument, UINT32_MAX) == LX_HOST_LOCK_OK);
    return NULL;
}
int main(void)
{
    LxHostMutex *mutex = lx_host_mutex_create(0, 0);
    CHECK(mutex);
    LxHostThread *thread = lx_host_thread_create(owner, mutex, NULL);
    CHECK(thread && !lx_host_thread_join(thread, NULL));
    CHECK(lx_host_mutex_lock(mutex, 0) == LX_HOST_LOCK_OWNER_DIED);
    CHECK(lx_host_mutex_busy(mutex));
    CHECK(lx_host_mutex_destroy(mutex) == LX_HOST_LOCK_BUSY);
    CHECK(lx_host_mutex_unlock(mutex) == LX_HOST_LOCK_OK);
    CHECK(lx_host_mutex_lock(mutex, 0) == LX_HOST_LOCK_OK);
    CHECK(lx_host_mutex_unlock(mutex) == LX_HOST_LOCK_OK);
    CHECK(lx_host_mutex_destroy(mutex) == LX_HOST_LOCK_OK);
    puts("Linux robust mutex owner-death recovery passed");
    return 0;
}
