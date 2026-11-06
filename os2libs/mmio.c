/* Public domain. No rights reserved. Wholly AI-authored. */
/* Memory-file MMIO elements read from a snapshot taken at open; writes update the
   snapshot and the caller's buffer. */
#include "os2native.h"
#include "mmio.h"
#include "guest/abi.h"
#include "hal/host_sync.h"
static struct
{
    ULONG handle;
    unsigned char *bytes;
    uint32_t address;
    size_t size;
    size_t position;
    ULONG flags;
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
    if (name || !info || !(flags & 0x1c)) /* MMIO_READ, MMIO_WRITE, MMIO_READWRITE */
        return 0;
    unsigned char *p = info;
    /* MMIOINFO is a 32-bit wire structure, including its buffer pointer. */
    ULONG size = lx_guest_load_u32(p + 16), address = lx_guest_load_u32(p + 20);
    /* FOURCC_MEM may name the IOProc directly or as the child IOProc. */
    if (!size || size > 32 * 1024 * 1024 ||
        (lx_guest_load_u32(p + 4) != 0x204d454d && lx_guest_load_u32(p + 68) != 0x204d454d))
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
            elements[i].address = address;
            elements[i].size = size;
            elements[i].flags = flags;
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
static LONG transfer(ULONG handle, void *buffer, LONG count, int writing)
{
    if (!buffer || count < 0)
        return -1;
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    LONG result = -1;
    for (unsigned i = 0; i < 64; i++)
        if (handle && elements[i].handle == handle)
        {
            /* MEM IOProc accesses a fixed caller-supplied buffer. */
            if ((writing && (elements[i].flags & 4)) || (!writing && (elements[i].flags & 8)))
                break;
            size_t remaining = elements[i].size - elements[i].position;
            size_t bytes = (size_t)count < remaining ? (size_t)count : remaining;
            if (writing)
            {
                if (bytes && !lx_execution_write(GLoaderState.execution,
                                                 elements[i].address + (uint32_t)elements[i].position,
                                                 buffer, bytes))
                    break;
                memcpy(elements[i].bytes + elements[i].position, buffer, bytes);
            }
            else
                memcpy(buffer, elements[i].bytes + elements[i].position, bytes);
            elements[i].position += bytes;
            result = (LONG)bytes;
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}
LONG mmioRead(ULONG handle, PCHAR buffer, LONG bytes)
{
    return transfer(handle, buffer, bytes, 0);
}
LONG mmioWrite(ULONG handle, PCHAR buffer, LONG bytes)
{
    return transfer(handle, buffer, bytes, 1);
}
LONG mmioSeek(ULONG handle, LONG offset, LONG origin)
{
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    LONG result = -1;
    for (unsigned i = 0; i < 64; i++)
        if (handle && elements[i].handle == handle)
        {
            int64_t base = origin == 0 ? 0 : origin == 1 ? (int64_t)elements[i].position
                                             : origin == 2 ? (int64_t)elements[i].size : -1;
            int64_t position = base + offset;
            if (base >= 0 && position >= 0 && position <= (int64_t)elements[i].size)
                result = (LONG)(elements[i].position = (size_t)position);
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}
ULONG mmioGetHeader(ULONG handle, PVOID header, LONG capacity, PLONG bytes,
                    ULONG reserved, ULONG flags)
{
    (void)reserved;
    (void)flags;
    if (!header || capacity < 1 || !bytes)
        return 0x1969; /* MMIOERR_INVALID_PARAMETER */
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    ULONG result = 0x1968; /* MMIOERR_INVALID_HANDLE */
    for (unsigned i = 0; i < 64; i++)
        if (handle && elements[i].handle == handle)
        {
            /* The raw MEM IOProc has no translated media header. MMPM's
               dispatcher converts MMIOM_GETHEADER's -2 into zero bytes/-1. */
            *bytes = 0;
            result = UINT32_MAX;
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
