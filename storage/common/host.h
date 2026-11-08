/* Public domain. No rights reserved. Wholly AI-authored. */
/* The only storage interfaces whose implementations differ by host. */
#ifndef TB_STORAGE_HOST_H
#define TB_STORAGE_HOST_H
#include "storage/host_file.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* Advisory whole-file lock, shared for readers and exclusive for writers. */
int tb_host_lock(FILE *file, int write);
/* Flushes written image data to the device; 0 with errno on failure. */
int tb_host_sync(FILE *file);
struct tm *tb_host_localtime(int64_t seconds, struct tm *result);

/* Process-wide locks for state that the storage layer keeps per process. */
typedef enum TbStorageLock
{
    TB_STORAGE_LOCK_FATFS,
    TB_STORAGE_LOCK_CONTAINER_CACHE,
    TB_STORAGE_LOCK_COUNT
} TbStorageLock;

void tb_storage_lock(TbStorageLock lock);
void tb_storage_unlock(TbStorageLock lock);
#endif
