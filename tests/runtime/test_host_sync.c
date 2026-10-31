/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host synchronization semantics used by OS/2 mutex and event resources. */
#define _POSIX_C_SOURCE 200809L
#include "host_sync.h"
#include "host_thread.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static LxHostMutex *Mutex;
static void *contender(void *unused)
{
    (void) unused;
    CHECK(lx_host_mutex_unlock(Mutex) == LX_HOST_LOCK_NOT_OWNER);
    CHECK(lx_host_mutex_lock(Mutex, 0) == LX_HOST_LOCK_BUSY);
    CHECK(lx_host_mutex_lock(Mutex, 5) == LX_HOST_LOCK_TIMEOUT);
    return NULL;
}
typedef struct MonitorTest { LxHostMonitor *monitor; int entered, proceed; } MonitorTest;
static void *waiter(void *userdata)
{
    MonitorTest *t = userdata;
    lx_host_monitor_lock(t->monitor);
    t->entered = 1; lx_host_monitor_wake_all(t->monitor);
    while (!t->proceed) CHECK(lx_host_monitor_wait(t->monitor, UINT32_MAX) == LX_HOST_LOCK_OK);
    lx_host_monitor_unlock(t->monitor); return NULL;
}
static void monitor_test(int cancel)
{
    MonitorTest t = {lx_host_monitor_create(), 0, 0}; CHECK(t.monitor);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, waiter, &t) == 0);
    lx_host_monitor_lock(t.monitor);
    while (!t.entered) CHECK(lx_host_monitor_wait(t.monitor, 1000) == LX_HOST_LOCK_OK);
    if (cancel) CHECK(pthread_cancel(thread) == 0);
    else { t.proceed = 1; lx_host_monitor_wake_all(t.monitor); }
    lx_host_monitor_unlock(t.monitor);
    void *result; CHECK(pthread_join(thread, &result) == 0);
    if (cancel) CHECK(result == PTHREAD_CANCELED);
    lx_host_monitor_lock(t.monitor);
    CHECK(lx_host_monitor_wait(t.monitor, 1) == LX_HOST_LOCK_TIMEOUT);
    lx_host_monitor_unlock(t.monitor); lx_host_monitor_destroy(t.monitor);
}
static void *cancelled_mutex_wait(void *unused)
{
    (void) unused;
    CHECK(lx_host_mutex_lock(Mutex, 60000) == LX_HOST_LOCK_OK);
    CHECK(0); return NULL;
}
static void mutex_cancel_test(void)
{
    Mutex = lx_host_mutex_create(0, 1); CHECK(Mutex);
    pthread_t thread; CHECK(!pthread_create(&thread, NULL, cancelled_mutex_wait, NULL));
    CHECK(!pthread_cancel(thread));
    void *result; CHECK(!pthread_join(thread, &result)); CHECK(result == PTHREAD_CANCELED);
    CHECK(lx_host_mutex_unlock(Mutex) == LX_HOST_LOCK_OK);
    CHECK(!lx_host_mutex_busy(Mutex));
    CHECK(lx_host_mutex_destroy(Mutex) == LX_HOST_LOCK_OK);
}
static LxHostOnce Once = LX_HOST_ONCE_INIT;
static int Initializations, Cleanups;
static void initialize(void) { Initializations++; }
static void cleanup(void *unused) { (void) unused; __atomic_add_fetch(&Cleanups, 1, __ATOMIC_SEQ_CST); }
static uint32_t operation(void *cancel)
{
    lx_host_once(&Once, initialize);
    if (cancel) for (;;) pthread_testcancel();
    return 42;
}
static void *guarded(void *cancel)
{
    CHECK(lx_host_guarded_call(operation, cancel, cleanup, NULL) == 42); return NULL;
}
static void once_guard_test(void)
{
    LxHostThread *threads[2];
    for (unsigned i = 0; i < 2; i++) { threads[i] = lx_host_thread_create(guarded, NULL, cleanup); CHECK(threads[i]); }
    for (unsigned i = 0; i < 2; i++) CHECK(!lx_host_thread_join(threads[i], NULL));
    CHECK(Initializations == 1 && Cleanups == 4);
    pthread_t thread; CHECK(!pthread_create(&thread, NULL, guarded, &Once));
    CHECK(!pthread_cancel(thread)); void *result;
    CHECK(!pthread_join(thread, &result) && result == PTHREAD_CANCELED);
    CHECK(Cleanups == 5);
}
int main(void)
{
    Mutex = lx_host_mutex_create(1, 1); CHECK(Mutex && lx_host_mutex_busy(Mutex));
    CHECK(lx_host_mutex_destroy(Mutex) == LX_HOST_LOCK_BUSY);
    CHECK(lx_host_mutex_lock(Mutex, 0) == LX_HOST_LOCK_OK);
    CHECK(lx_host_mutex_unlock(Mutex) == LX_HOST_LOCK_OK);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, contender, NULL) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(lx_host_mutex_unlock(Mutex) == LX_HOST_LOCK_OK);
    CHECK(!lx_host_mutex_busy(Mutex));
    CHECK(lx_host_mutex_destroy(Mutex) == LX_HOST_LOCK_OK);
    monitor_test(0); monitor_test(1); mutex_cancel_test(); once_guard_test();
    puts("recursive ownership, timed contention, monitor rendezvous and cancelled-wait cleanup passed");
    return 0;
}
