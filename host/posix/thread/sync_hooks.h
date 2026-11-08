/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private platform hooks for shared POSIX synchronization. */
#ifndef TWOINE_HOST_POSIX_THREAD_SYNC_HOOKS_H
#define TWOINE_HOST_POSIX_THREAD_SYNC_HOOKS_H
#include <pthread.h>
#include <stdint.h>
#include <time.h>
void lx_posix_mutex_attributes(pthread_mutexattr_t *attributes);
void lx_posix_mutex_recover(pthread_mutex_t *mutex, int result);
clockid_t lx_posix_condition_clock(pthread_condattr_t *attributes);
int lx_posix_condition_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex, clockid_t clock,
                                 uint32_t milliseconds);
#endif
