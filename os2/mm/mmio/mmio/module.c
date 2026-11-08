/* Public domain. No rights reserved. Wholly AI-authored. */
/* MMIO elements are named DOS files or memory files. Memory files use the caller's
   buffer, keeping a copy made at open for reads after that buffer is gone. */
#include "os2/include/private/export.h"
#include "os2/include/mmio.h"
#include "emulator/abi.h"
#include "host/sync.h"
#include "host/fs.h"
#include <stdio.h>

static struct
{
    ULONG handle;
    unsigned char *bytes;
    uint32_t address;
    LxHostFile *file;
    uint32_t procedure;   /* Guest IOProc, when this element has one. */
    LxGuestMapping *info; /* Guest MMIOINFO passed to that IOProc. */
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

enum
{
    MMIOM_CLOSE = 0xe03,
    MMIOM_OPEN = 0xe04,
    MMIOM_READ = 0xe05,
    MMIOM_SEEK = 0xe06,
    MMIOM_WRITE = 0xe07,
    MMIOM_GETHEADER = 0xe09,
    FOURCC_MEM = 0x204d454d,
    FOURCC_DOS = 0x20534f44
};

static struct
{
    uint32_t fourcc, procedure;
} installed[32];

ULONG mmioInstallIOProc(ULONG fourcc, ULONG procedure, ULONG flags)
{
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    ULONG result = 0;
    unsigned slot = 32;
    for (unsigned i = 32; i-- > 0;)
    {
        if (installed[i].fourcc == fourcc && installed[i].procedure && slot == 32)
            slot = i;
    }
    if (slot < 32)
        result = installed[slot].procedure;
    if (flags & 1) /* MMIO_INSTALLPROC */
    {
        if (slot == 32)
            for (unsigned i = 0; i < 32 && slot == 32; i++)
                if (!installed[i].procedure)
                    slot = i;
        if (slot < 32)
            installed[slot].fourcc = fourcc, installed[slot].procedure = procedure;
    }
    else if ((flags & 2) && slot < 32) /* MMIO_REMOVEPROC */
        installed[slot].procedure = 0;
    lx_host_mutex_unlock(mutex);
    return result;
}

static ULONG add_element(unsigned char *copy, uint32_t address, size_t size, LxHostFile *file,
                         ULONG flags)
{
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
            elements[i].file = file;
            elements[i].flags = flags;
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}

/* The DOS IOProc reads and writes a named file. Installed IOProcs are not available. */
static ULONG open_file(PSZ name, const unsigned char *info, ULONG flags)
{
    if (info && (lx_guest_load_u32(info + 8) ||
                 (lx_guest_load_u32(info + 4) && lx_guest_load_u32(info + 4) != 0x20534f44)))
        return 0;
    if (!GProcessState.makeUnixPath)
        return 0;
    uint32 error = 0;
    char *path = GProcessState.makeUnixPath(name, &error);
    if (!path)
        return 0;
    unsigned mode = flags & 8      ? LX_HOST_FILE_WRITE
                    : flags & 0x10 ? LX_HOST_FILE_READ | LX_HOST_FILE_WRITE
                                   : LX_HOST_FILE_READ;
    if (flags & 1) /* MMIO_CREATE */
        mode |= LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_TRUNCATE;
    LxHostFsError status;
    LxHostFile *file = lx_host_file_open(path, mode, 0, &status);
    free(path);
    if (!file)
        return 0;
    ULONG result = add_element(NULL, 0, 0, file, flags);
    if (!result)
        lx_host_file_close(file);
    return result;
}

/* The explicit procedure wins, then an installed FOURCC; MEM and DOS are built in. */
static uint32_t custom_procedure(const unsigned char *info)
{
    if (!info)
        return 0;
    if (lx_guest_load_u32(info + 8))
        return lx_guest_load_u32(info + 8);
    uint32_t fourcc = lx_guest_load_u32(info + 4), result = 0;
    if (!fourcc || fourcc == FOURCC_MEM || fourcc == FOURCC_DOS)
        return 0;
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    for (unsigned i = 0; i < 32; i++)
        if (installed[i].procedure && installed[i].fourcc == fourcc)
            result = installed[i].procedure;
    lx_host_mutex_unlock(mutex);
    return result;
}

/* An IOProc receives guest MMIOINFO, which it may update across every later call. */
static ULONG open_custom(PSZ name, unsigned char *info, ULONG flags, uint32_t procedure)
{
    LxGuestMapping *mapping =
        lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    unsigned char *guest = lx_execution_mapping_host(mapping);
    uint32_t address = lx_execution_mapping_address(mapping);
    memcpy(guest, info, 80);
    lx_guest_store_u32(guest, flags);
    lx_guest_store_u32(guest + 8, procedure);
    if (name)
        snprintf((char *)guest + 256, 4096 - 256, "%s", name);
    uint32_t result = lx_execution_callback32_4(GProcessState.execution, procedure, address,
                                                MMIOM_OPEN, name ? address + 256 : 0, 0);
    memcpy(info, guest, 80);
    if (result)
    {
        lx_guest_store_u32(info + 12, result); /* ulErrorRet */
        lx_execution_unmap(GProcessState.execution, mapping);
        return 0;
    }
    ULONG handle = add_element(NULL, 0, 0, NULL, flags);
    if (!handle)
    {
        lx_execution_callback32_4(GProcessState.execution, procedure, address, MMIOM_CLOSE, 0, 0);
        lx_execution_unmap(GProcessState.execution, mapping);
        return 0;
    }
    lx_host_mutex_lock(mutex, UINT32_MAX);
    for (unsigned i = 0; i < 64; i++)
        if (elements[i].handle == handle)
        {
            elements[i].procedure = procedure;
            elements[i].info = mapping;
        }
    lx_host_mutex_unlock(mutex);
    return handle;
}

/* Guest code may reenter MMIO, so callers must not hold the element lock across a call. */
static BOOL custom_element(ULONG handle, uint32_t *procedure, uint32_t *address, ULONG *flags)
{
    BOOL found = FALSE;
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    for (unsigned i = 0; i < 64; i++)
        if (handle && elements[i].handle == handle && elements[i].procedure)
        {
            *procedure = elements[i].procedure;
            *address = lx_execution_mapping_address(elements[i].info);
            *flags = elements[i].flags;
            found = TRUE;
            break;
        }
    lx_host_mutex_unlock(mutex);
    return found;
}

ULONG mmioOpen(PSZ name, PVOID info, ULONG flags)
{
    TRACE_NATIVE("mmioOpen(%p, %p, 0x%x)", name, info, (unsigned)flags);
    uint32_t procedure = custom_procedure(info);
    if (procedure)
        return open_custom(name, info, flags, procedure);
    if (info && !name && lx_guest_load_u32((unsigned char *)info + 4) &&
        lx_guest_load_u32((unsigned char *)info + 4) != FOURCC_MEM &&
        lx_guest_load_u32((unsigned char *)info + 68) != FOURCC_MEM)
        return 0;
    if (name)
        return open_file(name, info, flags);
    if (!info || !(flags & 0x1c)) /* MMIO_READ, MMIO_WRITE, MMIO_READWRITE */
        return 0;
    unsigned char *p = info;
    /* MMIOINFO is a 32-bit wire structure, including its buffer pointer. */
    ULONG size = lx_guest_load_u32(p + 16), address = lx_guest_load_u32(p + 20);
    /* FOURCC_MEM may name the IOProc directly or as the child IOProc. */
    if (!size || size > 32 * 1024 * 1024 ||
        (lx_guest_load_u32(p + 4) != 0x204d454d && lx_guest_load_u32(p + 68) != 0x204d454d))
        return 0;
    unsigned char *copy = malloc(size);
    if (!copy || !lx_execution_read(GProcessState.execution, address, copy, size))
    {
        free(copy);
        return 0;
    }
    ULONG result = add_element(copy, address, size, NULL, flags);
    if (!result)
        free(copy);
    return result;
}

USHORT mmioClose(ULONG handle, USHORT flags)
{
    uint32_t procedure = 0, address = 0;
    ULONG mode = 0;
    uint32_t closed = 0;
    if (custom_element(handle, &procedure, &address, &mode))
        closed = lx_execution_callback32_4(GProcessState.execution, procedure, address, MMIOM_CLOSE,
                                           flags, 0);
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    USHORT result = 1;
    for (unsigned i = 0; i < 64; i++)
        if (elements[i].handle == handle && handle)
        {
            if (elements[i].file)
                lx_host_file_close(elements[i].file);
            if (elements[i].info)
                lx_execution_unmap(GProcessState.execution, elements[i].info);
            free(elements[i].bytes);
            memset(&elements[i], 0, sizeof elements[i]);
            result = closed ? (USHORT)closed : 0;
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}

static LONG transfer(ULONG handle, void *buffer, LONG count, int writing)
{
    if (!buffer || count < 0)
        return -1;
    uint32_t procedure = 0, address = 0, guest = 0;
    ULONG mode = 0;
    if (custom_element(handle, &procedure, &address, &mode))
    {
        if ((writing && (mode & 4)) || (!writing && (mode & 8)) ||
            !lx_execution_address(GProcessState.execution, buffer, (size_t)count,
                                  writing ? LX_GUEST_READ : LX_GUEST_WRITE, &guest))
            return -1;
        return (LONG)lx_execution_callback32_4(GProcessState.execution, procedure, address,
                                               writing ? MMIOM_WRITE : MMIOM_READ, guest,
                                               (uint32_t)count);
    }
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    LONG result = -1;
    for (unsigned i = 0; i < 64; i++)
        if (handle && elements[i].handle == handle)
        {
            /* MEM IOProc accesses a fixed caller-supplied buffer. */
            if ((writing && (elements[i].flags & 4)) || (!writing && (elements[i].flags & 8)))
                break;
            if (elements[i].file)
            {
                size_t actual = 0;
                LxHostFsError error =
                    writing ? lx_host_file_write(elements[i].file, buffer, (size_t)count, &actual)
                            : lx_host_file_read(elements[i].file, buffer, (size_t)count, &actual);
                if (!error)
                    result = (LONG)actual;
                break;
            }
            size_t remaining = elements[i].size - elements[i].position;
            size_t bytes = (size_t)count < remaining ? (size_t)count : remaining;
            if (writing)
            {
                if (bytes &&
                    !lx_execution_write(GProcessState.execution,
                                        elements[i].address + (uint32_t)elements[i].position,
                                        buffer, bytes))
                    break;
                memcpy(elements[i].bytes + elements[i].position, buffer, bytes);
            }
            else if (!bytes ||
                     !lx_execution_read(GProcessState.execution,
                                        elements[i].address + (uint32_t)elements[i].position,
                                        buffer, bytes))
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
    uint32_t procedure = 0, address = 0;
    ULONG mode = 0;
    if (custom_element(handle, &procedure, &address, &mode))
        return (LONG)lx_execution_callback32_4(GProcessState.execution, procedure, address,
                                               MMIOM_SEEK, (uint32_t)offset, (uint32_t)origin);
    lx_host_once(&once, initialize);
    lx_host_mutex_lock(mutex, UINT32_MAX);
    LONG result = -1;
    for (unsigned i = 0; i < 64; i++)
        if (handle && elements[i].handle == handle)
        {
            if (elements[i].file)
            {
                uint64_t position = 0;
                if (origin >= 0 && origin <= 2 &&
                    !lx_host_file_seek(elements[i].file, offset, (unsigned)origin, &position) &&
                    position <= INT32_MAX)
                    result = (LONG)position;
                break;
            }
            int64_t base = origin == 0   ? 0
                           : origin == 1 ? (int64_t)elements[i].position
                           : origin == 2 ? (int64_t)elements[i].size
                                         : -1;
            int64_t position = base + offset;
            if (base >= 0 && position >= 0 && position <= (int64_t)elements[i].size)
                result = (LONG)(elements[i].position = (size_t)position);
            break;
        }
    lx_host_mutex_unlock(mutex);
    return result;
}

ULONG mmioGetHeader(ULONG handle, PVOID header, LONG capacity, PLONG bytes, ULONG reserved,
                    ULONG flags)
{
    (void)reserved;
    (void)flags;
    if (!header || capacity < 1 || !bytes)
        return 0x1969; /* MMIOERR_INVALID_PARAMETER */
    uint32_t procedure = 0, address = 0, guest = 0;
    ULONG mode = 0;
    if (custom_element(handle, &procedure, &address, &mode))
    {
        if (!lx_execution_address(GProcessState.execution, header, (size_t)capacity, LX_GUEST_WRITE,
                                  &guest))
            return 0x1969;
        LONG read = (LONG)lx_execution_callback32_4(GProcessState.execution, procedure, address,
                                                    MMIOM_GETHEADER, guest, (uint32_t)capacity);
        *bytes = read == -2 ? 0 : read;
        return *bytes > 0 ? 0 : UINT32_MAX;
    }
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
        if (elements[i].handle == handle && handle && !elements[i].file)
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

#include "os2/mm/mmio/mmio/generated/mmio-lx.h"
