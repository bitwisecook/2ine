/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native thread creation and cancellation cleanup are confined to the host. */
#include "hal/host_thread.h"
#include <stdlib.h>
#include <pthread.h>
struct LxHostThread
{
    pthread_t object;
};
struct LxHostTlsKey
{
    pthread_key_t key;
};
LxHostTlsKey *lx_host_tls_create(void (*destroy)(void *))
{
    LxHostTlsKey *key = malloc(sizeof *key);
    if (key && pthread_key_create(&key->key, destroy))
    {
        free(key);
        key = NULL;
    }
    return key;
}
void lx_host_tls_destroy(LxHostTlsKey *key)
{
    if (key)
    {
        pthread_key_delete(key->key);
        free(key);
    }
}
void *lx_host_tls_get(LxHostTlsKey *key)
{
    return key ? pthread_getspecific(key->key) : NULL;
}
int lx_host_tls_set(LxHostTlsKey *key, void *value)
{
    return key && !pthread_setspecific(key->key, value);
}
typedef struct Start
{
    void *(*entry)(void *);
    void *argument;
    void (*cleanup)(void *);
} Start;
static void cleanupStart(void *argument)
{
    Start *start = argument;
    if (start->cleanup)
        start->cleanup(start->argument);
}
static void *enter(void *argument)
{
    Start start = *(Start *)argument;
    free(argument);
    void *result;
    pthread_cleanup_push(cleanupStart, &start);
    result = start.entry(start.argument);
    pthread_cleanup_pop(1);
    return result;
}
LxHostThread *lx_host_thread_create(void *(*entry)(void *), void *argument, void (*cleanup)(void *))
{
    LxHostThread *thread = calloc(1, sizeof *thread);
    Start *start = malloc(sizeof *start);
    if (!thread || !start)
    {
        free(thread);
        free(start);
        return NULL;
    }
    start->entry = entry;
    start->argument = argument;
    start->cleanup = cleanup;
    if (pthread_create(&thread->object, NULL, enter, start))
    {
        free(start);
        free(thread);
        return NULL;
    }
    return thread;
}
LxHostCancelResult lx_host_thread_cancel(LxHostThread *thread)
{
    return pthread_cancel(thread->object) ? LX_HOST_CANCEL_FAILED : LX_HOST_CANCEL_OK;
}
void lx_host_thread_test_cancel(void)
{
    pthread_testcancel();
}
int lx_host_thread_join(LxHostThread *thread, void **result)
{
    int error = pthread_join(thread->object, result);
    if (!error)
        free(thread);
    return error;
}
void lx_host_thread_detach(LxHostThread *thread)
{
    pthread_detach(thread->object);
    free(thread);
}
void lx_host_thread_exit(void *result)
{
    pthread_exit(result);
}
uint64_t lx_host_thread_id(void)
{
    return (uint64_t)(uintptr_t)pthread_self();
}
uint32_t lx_host_guarded_call(uint32_t (*operation)(void *), void *argument,
                              void (*cleanup)(void *), void *cleanup_argument)
{
    uint32_t result;
    pthread_cleanup_push(cleanup, cleanup_argument);
    result = operation(argument);
    pthread_cleanup_pop(1);
    return result;
}
