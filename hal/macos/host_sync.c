/* Public domain. No rights reserved. Wholly AI-authored. */
/* macOS condition waits use relative durations. Mutexes are not robust. */
#define _DARWIN_C_SOURCE 1
#include "../posix/sync_platform.h"
void lx_posix_mutex_attributes(pthread_mutexattr_t *attributes)
{
    (void)attributes;
}
void lx_posix_mutex_recover(pthread_mutex_t *mutex, int result)
{
    (void)mutex;
    (void)result;
}
clockid_t lx_posix_condition_clock(pthread_condattr_t *attributes)
{
    (void)attributes;
    return CLOCK_REALTIME;
}
int lx_posix_condition_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex, clockid_t clock,
                                 uint32_t milliseconds)
{
    (void)clock;
    struct timespec duration = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000};
    return pthread_cond_timedwait_relative_np(condition, mutex, &duration);
}
