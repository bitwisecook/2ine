/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

APIRET DosSetFileLocks(HFILE id, PFILELOCK unlock, PFILELOCK lock, ULONG timeout, ULONG flags)
{
    TRACE_NATIVE("DosSetFileLocks(%u, %p, %p, %u, %u)", (uint)id, unlock, lock, (uint)timeout,
                 (uint)flags);
    FILELOCK release = unlock ? *unlock : (FILELOCK){0}, acquire = lock ? *lock : (FILELOCK){0};
    if ((flags & ~3u) || release.lOffset < 0 || release.lRange < 0 || acquire.lOffset < 0 ||
        acquire.lRange < 0 || (uint64)release.lOffset + release.lRange > INT32_MAX ||
        (uint64)acquire.lOffset + acquire.lRange > INT32_MAX)
        return ERROR_INVALID_PARAMETER;
    if (!release.lRange && !acquire.lRange)
        return ERROR_LOCK_VIOLATION;
    LxHandleBorrow file;
    if (!borrowFile(id, &file))
        return ERROR_INVALID_HANDLE;
    APIRET result = NO_ERROR;
    /* The host range adapter currently implements exclusive locks. Report
       unsupported shared/atomic modes before releasing an existing range. */
    if (flags & 2)
        result = release.lOffset != acquire.lOffset || release.lRange != acquire.lRange
                     ? ERROR_LOCK_VIOLATION
                     : ERROR_ATOMIC_LOCK_NOT_SUPPORTED;
    else if (flags & 1)
        result = ERROR_NOT_SUPPORTED;
    if (!result && release.lRange)
    {
        LxHostFsError error = lx_host_file_lock_range(file.resource, (uint32)release.lOffset,
                                                      (uint32)release.lRange, 0);
        result = error == LX_HOST_FS_INVALID ? ERROR_LOCK_VIOLATION : fileError(error);
    }
    if (!result && acquire.lRange)
    {
        uint64 start = lx_host_monotonic_us();
        for (;;)
        {
            LxHostFsError error = lx_host_file_lock_range(file.resource, (uint32)acquire.lOffset,
                                                          (uint32)acquire.lRange, 1);
            if (error != LX_HOST_FS_BUSY)
            {
                result = fileError(error);
                break;
            }
            uint64 elapsed = (lx_host_monotonic_us() - start) / 1000;
            if (timeout != UINT32_MAX && elapsed >= timeout)
            {
                result = ERROR_LOCK_VIOLATION;
                break;
            }
            uint32 remaining = timeout == UINT32_MAX ? 10 : (uint32)(timeout - elapsed);
            lx_host_sleep_ms(remaining < 10 ? remaining : 10);
        }
    }
    lx_handle_release(&file);
    return result;
}
