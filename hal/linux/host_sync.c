/* Public domain. No rights reserved. Wholly AI-authored. */
/* Robust mutex recovery and absolute monotonic condition deadlines. */
#define _GNU_SOURCE 1
#include "../posix/sync_platform.h"
#include <errno.h>
void lx_posix_mutex_attributes(pthread_mutexattr_t *attributes)
{
    pthread_mutexattr_setrobust(attributes, PTHREAD_MUTEX_ROBUST);
}
void lx_posix_mutex_recover(pthread_mutex_t *mutex, int result)
{
    if (result == EOWNERDEAD)
        pthread_mutex_consistent(mutex);
}
clockid_t lx_posix_condition_clock(pthread_condattr_t *attributes)
{
    return pthread_condattr_setclock(attributes, CLOCK_MONOTONIC) ? CLOCK_REALTIME
                                                                  : CLOCK_MONOTONIC;
}
int lx_posix_condition_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex, clockid_t clock,
                                 uint32_t milliseconds)
{
    struct timespec deadline;
    clock_gettime(clock, &deadline);
    deadline.tv_sec += milliseconds / 1000;
    deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(condition, mutex, &deadline);
}
