/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host thread lifetime. Guest thread IDs and guest stacks are separate objects. */
#ifndef TWOINE_HOST_THREAD_H
#define TWOINE_HOST_THREAD_H
#include <stdint.h>
#if defined(_MSC_VER)
#define LX_HOST_THREAD_LOCAL __declspec(thread)
#else
#define LX_HOST_THREAD_LOCAL __thread
#endif
typedef struct LxHostThread LxHostThread;
LxHostThread *lx_host_thread_create(void *(*entry)(void *), void *argument,
                                    void (*cleanup)(void *));
typedef enum LxHostCancelResult
{
    LX_HOST_CANCEL_OK,
    LX_HOST_CANCEL_UNSUPPORTED,
    LX_HOST_CANCEL_FAILED
} LxHostCancelResult;
/* Cancellation runs registered cleanup at a host cancellation point. */
LxHostCancelResult lx_host_thread_cancel(LxHostThread *thread);
void lx_host_thread_test_cancel(void);
int lx_host_thread_join(LxHostThread *thread, void **result);
void lx_host_thread_detach(LxHostThread *thread);
void lx_host_thread_exit(void *result);
uint64_t lx_host_thread_id(void);
/* Values have a destructor on host thread exit. Delete only after all threads
   using the key have joined; deletion does not destroy their values. */
typedef struct LxHostTlsKey LxHostTlsKey;
LxHostTlsKey *lx_host_tls_create(void (*destroy)(void *));
void lx_host_tls_destroy(LxHostTlsKey *key);
void *lx_host_tls_get(LxHostTlsKey *key);
int lx_host_tls_set(LxHostTlsKey *key, void *value);
/* The cleanup argument remains live in the caller's frame. The host adapter
   runs cleanup on both ordinary return and supported thread cancellation. */
uint32_t lx_host_guarded_call(uint32_t (*operation)(void *), void *argument,
                              void (*cleanup)(void *), void *cleanup_argument);
#endif
