/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Locking helpers, and file and semaphore operations run as guarded host calls so that a
   thread killed mid-call still releases its handle borrow. */
#include "os2/baseos/doscalls/internal.h"

int grabLock(LxHostMutex **lock)
{
    return (lx_host_mutex_lock(*lock, UINT32_MAX) == LX_HOST_LOCK_OK);
}

void ungrabLock(LxHostMutex **lock)
{
    lx_host_mutex_unlock(*lock);
}

uint32 guardedResource(enum ResourceOperation operation, uint32 handle, uint32 value,
                       LxHandleBorrow *object, void *buffer, void *output)
{
    ResourceCall call = {operation, handle, value, object->resource, buffer, output};
    return lx_host_guarded_call(resourceOperation, &call, releaseResourceBorrow, object);
}

void releaseResourceBorrow(void *p)
{
    lx_handle_release(p);
}

uint32 resourceOperation(void *argument)
{
    ResourceCall *call = argument;
    switch (call->operation)
    {
        case RESOURCE_READ:
            return lx_host_file_read(call->resource, call->buffer, call->value, call->output);
        case RESOURCE_WRITE:
        {
            LxHostFsError result =
                lx_host_file_write(call->resource, call->buffer, call->value, call->output);
            USHORT verify = 0;
            Dos16QVerify(&verify);
            if (!result && verify)
            {
                LxHostFileInfo info;
                if (!lx_host_file_info(call->resource, &info) && info.kind == LX_HOST_FILE_REGULAR)
                    result = lx_host_file_flush(call->resource);
            }
            return result;
        }
        case RESOURCE_REQUEST_MUTEX:
            return DosRequestMutexSemResource(call->handle, call->value, call->resource);
        case RESOURCE_RELEASE_MUTEX:
            return DosReleaseMutexSemResource(call->handle, call->resource);
        case RESOURCE_RESET_EVENT:
            return DosResetEventSemResource(call->handle, call->output, call->resource);
        case RESOURCE_POST_EVENT:
            return DosPostEventSemResource(call->handle, call->resource);
        case RESOURCE_WAIT_EVENT:
            return DosWaitEventSemResource(call->handle, call->value, call->resource);
        case RESOURCE_QUERY_EVENT:
            return DosQueryEventSemResource(call->handle, call->output, call->resource);
    }
    return ERROR_INVALID_FUNCTION;
}
