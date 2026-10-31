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
LxHostThread *lx_host_thread_create(void *(*entry)(void *), void *argument, void (*cleanup)(void *));
int lx_host_thread_join(LxHostThread *thread, void **result);
void lx_host_thread_detach(LxHostThread *thread);
void lx_host_thread_exit(void *result);
uint64_t lx_host_thread_id(void);
/* The cleanup argument remains live in the caller's frame. The host adapter
   runs cleanup on both ordinary return and supported thread cancellation. */
uint32_t lx_host_guarded_call(uint32_t (*operation)(void *), void *argument,
                              void (*cleanup)(void *), void *cleanup_argument);
#endif
