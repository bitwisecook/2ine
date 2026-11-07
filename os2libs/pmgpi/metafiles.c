/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metafiles own checked byte streams. Copy and edits are independent; a bad
   handle or range never dereferences an application supplied resource value. */
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "hal/host_fs.h"
typedef struct PmMetaFile
{
    size_t length;
    uint8 *bytes;
} PmMetaFile;
static void destroy_meta(void *v)
{
    PmMetaFile *m = v;
    free(m->bytes);
    free(m);
}
static ULONG create_meta(const void *data, size_t length)
{
    if (!data || !length || length > 64 * 1024 * 1024)
        return 0;
    PmMetaFile *m = calloc(1, sizeof *m);
    if (!m)
        return 0;
    m->bytes = malloc(length);
    if (!m->bytes)
    {
        free(m);
        return 0;
    }
    memcpy(m->bytes, data, length);
    m->length = length;
    ULONG h = lx_handle_create(LX_HANDLE_PM_METAFILE, m, destroy_meta);
    if (!h)
        destroy_meta(m);
    return h;
}
ULONG lx_pm_metafile_create(const void *data, size_t length)
{
    return create_meta(data, length);
}
BOOL GpiDeleteMetaFile(ULONG h)
{
    return lx_handle_close(h, LX_HANDLE_PM_METAFILE);
}
ULONG GpiLoadMetaFile(HAB hab, PSZ name)
{
    if (!lxHostThreadState()->anchor_block || !name || !GLoaderState.makeUnixPath)
        return 0;
    uint32 error = 0;
    char *path = GLoaderState.makeUnixPath(name, &error);
    if (!path)
        return 0;
    size_t length = 0;
    LxHostFsError status;
    void *bytes = lx_host_file_snapshot(path, 64 * 1024 * 1024, &length, &status);
    free(path);
    ULONG result = bytes ? create_meta(bytes, length) : 0;
    free(bytes);
    return result;
}
ULONG GpiCopyMetaFile(ULONG h)
{
    LxHandleBorrow b = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_PM_METAFILE, &b))
        return 0;
    PmMetaFile *m = b.resource;
    ULONG result = create_meta(m->bytes, m->length);
    lx_handle_release(&b);
    return result;
}
LONG GpiQueryMetaFileLength(ULONG h)
{
    LxHandleBorrow b = {0};
    if (!lx_handle_borrow(h, LX_HANDLE_PM_METAFILE, &b))
        return -1;
    LONG n = ((PmMetaFile *)b.resource)->length;
    lx_handle_release(&b);
    return n;
}
BOOL GpiQueryMetaFileBits(ULONG h, LONG offset, LONG length, PBYTE data)
{
    LxHandleBorrow b = {0};
    if (offset < 0 || length < 0 || (length && !data) ||
        !lx_handle_borrow(h, LX_HANDLE_PM_METAFILE, &b))
        return FALSE;
    PmMetaFile *m = b.resource;
    BOOL ok = (size_t)offset <= m->length && (size_t)length <= m->length - offset;
    if (ok && length)
        memcpy(data, m->bytes + offset, length);
    lx_handle_release(&b);
    return ok;
}
BOOL GpiSetMetaFileBits(ULONG h, LONG offset, LONG length, PVOID data)
{
    LxHandleBorrow b = {0};
    if (offset < 0 || length < 0 || (length && !data) ||
        !lx_handle_borrow(h, LX_HANDLE_PM_METAFILE, &b))
        return FALSE;
    PmMetaFile *m = b.resource;
    BOOL ok = (size_t)offset <= m->length && (size_t)length <= m->length - offset;
    if (ok && length)
        memcpy(m->bytes + offset, data, length);
    lx_handle_release(&b);
    return ok;
}
BOOL GpiSaveMetaFile(ULONG h, PSZ name)
{
    LxHandleBorrow b = {0};
    if (!name || !GLoaderState.makeUnixPath || !lx_handle_borrow(h, LX_HANDLE_PM_METAFILE, &b))
        return FALSE;
    uint32 error = 0;
    char *path = GLoaderState.makeUnixPath(name, &error);
    LxHostFsError status;
    /* An existing file is an error, so the create must be exclusive. */
    LxHostFile *f =
        path ? lx_host_file_open(path,
                                 LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE,
                                 0, &status)
             : NULL;
    free(path);
    PmMetaFile *m = b.resource;
    size_t done = 0;
    BOOL ok = f != NULL;
    while (ok && done < m->length)
    {
        size_t n = 0;
        ok = !lx_host_file_write(f, m->bytes + done, m->length - done, &n) && n;
        done += n;
    }
    if (f)
        lx_host_file_close(f);
    lx_handle_release(&b);
    /* A saved metafile leaves storage, so its handle stops being valid. */
    if (ok)
        lx_handle_close(h, LX_HANDLE_PM_METAFILE);
    return ok;
}
