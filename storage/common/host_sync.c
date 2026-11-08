/* Public domain. No rights reserved. Wholly AI-authored. */
#include "storage/common/host.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK locks[TB_STORAGE_LOCK_COUNT] = {SRWLOCK_INIT, SRWLOCK_INIT};

void tb_storage_lock(TbStorageLock lock)
{
    AcquireSRWLockExclusive(&locks[lock]);
}

void tb_storage_unlock(TbStorageLock lock)
{
    ReleaseSRWLockExclusive(&locks[lock]);
}
#else
#include <pthread.h>
static pthread_mutex_t locks[TB_STORAGE_LOCK_COUNT] = {PTHREAD_MUTEX_INITIALIZER,
                                                       PTHREAD_MUTEX_INITIALIZER};

void tb_storage_lock(TbStorageLock lock)
{
    pthread_mutex_lock(&locks[lock]);
}

void tb_storage_unlock(TbStorageLock lock)
{
    pthread_mutex_unlock(&locks[lock]);
}
#endif
