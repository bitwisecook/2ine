/* Public domain. No rights reserved. Wholly AI-authored. */
/* Memory-file MMIO elements retain their input independent of the caller. */
#include "os2native.h"
#include "mmio.h"
#include "guest/abi.h"
#include "hal/host_sync.h"
static struct
{
    ULONG handle;
    unsigned char *bytes;
    size_t size;
} elements[64];
static ULONG next_handle = 1;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMutex *mutex;
static void initialize(void)
{
    mutex = lx_host_mutex_create(0, 0);
}
ULONG mmioOpen(PSZ name, PVOID info, ULONG flags)
{
    TRACE_NATIVE("mmioOpen(%p, %p, 0x%x)", name, info, (unsigned)flags);
    if (name || !info || !(flags & 4))
        return 0;
    unsigned char *p = info;
    /* MMIOINFO is a 32-bit wire structure, including its buffer pointer. */
    ULONG size = lx_guest_load_u32(p + 16), address = lx_guest_load_u32(p + 20);
    if (!size || size > 32 * 1024 * 1024 || lx_guest_load_u32(p + 68) != 0x204d454d)
        return 0;
    unsigned char *copy = malloc(size);
    if (!copy || !lx_execution_read(GLoaderState.execution, address, copy, size))
    {
        free(copy);
        return 0;
    }
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    ULONG result = 0;
    for (unsigned i = 0; i < 64; i++)
        if (!elements[i].handle && next_handle)
        {
            result = next_handle++;
            elements[i].handle = result;
            elements[i].bytes = copy;
            elements[i].size = size;
            break;
        }
    lx_host_mutex_unlock(mutex);
    if (!result)
        free(copy);
    return result;
}
USHORT mmioClose(ULONG handle, USHORT flags)
{
    (void)flags;
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    USHORT result = 1;
    for (unsigned i = 0; i < 64; i++)
        if (elements[i].handle == handle && handle)
        {
            free(elements[i].bytes);
            memset(&elements[i], 0, sizeof elements[i]);
            result = 0;
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}
unsigned char *lx_mmio_copy(ULONG handle, size_t *size)
{
    unsigned char *result = NULL;
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    for (unsigned i = 0; i < 64; i++)
        if (elements[i].handle == handle && handle)
        {
            result = malloc(elements[i].size);
            if (result)
            {
                *size = elements[i].size;
                memcpy(result, elements[i].bytes, *size);
            }
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}
#include "os2libs/generated/mmio-lx.h"
