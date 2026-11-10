/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy Open32 HFILE operations share DOSCALLS' file table and path semantics. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/kernel/processes.h"
#include "os2/include/bsedos.h"
#include "host/fs.h"

enum
{
    /* OF_READ/OF_WRITE/OF_READWRITE and the OF_SHARE_* modes have the values of
       DosOpen's OPEN_ACCESS_* and OPEN_SHARE_* bits, so they pass straight through. */
    OF_ACCESS_MASK = WIN32_OF_WRITE | WIN32_OF_READWRITE,
    OF_SHARE_MASK = 0x70,
    OPENFILE_VALID_FLAGS = OF_ACCESS_MASK | OF_SHARE_MASK | WIN32_OF_PARSE | WIN32_OF_DELETE |
                           WIN32_OF_VERIFY | WIN32_OF_CANCEL | WIN32_OF_CREATE | WIN32_OF_PROMPT |
                           WIN32_OF_EXIST | WIN32_OF_REOPEN,
    LOPEN_VALID_FLAGS = OF_ACCESS_MASK | OF_SHARE_MASK,
    /* The returned path is "C:" plus the host path, within OFSTRUCT's name. */
    DRIVE_PREFIX_BYTES = 2
};

LONG OpenFile(PSZ name, PVOID record, ULONG flags)
{
    TRACE_NATIVE("OpenFile('%s', %p, %X)", name, record, flags);
    if (!record || (flags & ~(unsigned)OPENFILE_VALID_FLAGS) ||
        ((flags & OF_ACCESS_MASK) == OF_ACCESS_MASK))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    uint8 *wire = record;
    const char *source = flags & WIN32_OF_REOPEN ? (char *)wire + WIN32_OFSTRUCT_PATH_NAME : name;
    if (!source || ((flags & WIN32_OF_REOPEN) && !memchr(source, 0, WIN32_OFS_MAXPATHNAME)))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    char path[CCHMAXPATH];
    if (strlen(source) >= sizeof path)
    {
        open32SetError(ERROR_FILENAME_EXCED_RANGE);
        return -1;
    }
    strcpy(path, source);
    memset(wire, 0, WIN32_OFSTRUCT_SIZE);
    wire[WIN32_OFSTRUCT_BYTES] = WIN32_OFSTRUCT_SIZE;
    wire[WIN32_OFSTRUCT_FIXED_DISK] = TRUE;
    uint32 error = 0;
    char *host_path = GProcessState.makeUnixPath ? GProcessState.makeUnixPath(path, &error) : NULL;
    if (!host_path)
        error = error ? error : ERROR_INVALID_PARAMETER;
    else
    {
        char *canonical = lx_host_path_canonical(host_path);
        const char *full = canonical ? canonical : host_path;
        size_t length = strlen(full);
        if (length + DRIVE_PREFIX_BYTES + 1 > WIN32_OFS_MAXPATHNAME)
            error = ERROR_FILENAME_EXCED_RANGE;
        else
        {
            char *output = (char *)wire + WIN32_OFSTRUCT_PATH_NAME;
            output[0] = 'C';
            output[1] = ':';
            for (size_t i = 0; i <= length; i++)
                output[i + 2] = full[i] == '/' ? '\\' : full[i];
        }
        free(canonical);
        free(host_path);
    }
    if (!error && (flags & WIN32_OF_PARSE))
        return 0;
    /* Verify and prompt need user interface this call does not provide. */
    if (!error && (flags & (WIN32_OF_VERIFY | WIN32_OF_PROMPT)))
        error = ERROR_NOT_SUPPORTED;
    if (!error && (flags & WIN32_OF_DELETE))
    {
        typedef APIRET(OS2API * DeleteFn)(PSZ);
        DeleteFn remove = (DeleteFn)open32DosService("DosDelete");
        error = remove ? remove(path) : ERROR_CALL_NOT_IMPLEMENTED;
        if (!error)
            return 0;
    }
    HFILE handle = 0;
    if (!error)
    {
        typedef APIRET(OS2API * OpenFn)(PSZ, PHFILE, PULONG, ULONG, ULONG, ULONG, ULONG, PEAOP2);
        OpenFn open = (OpenFn)open32DosService("DosOpen");
        ULONG action;
        error =
            open ? open(path, &handle, &action, 0, FILE_NORMAL,
                        flags & WIN32_OF_CREATE
                            ? OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS
                            : OPEN_ACTION_OPEN_IF_EXISTS,
                        (flags & OF_ACCESS_MASK) |
                            ((flags & OF_SHARE_MASK) ? flags & OF_SHARE_MASK : OPEN_SHARE_DENYNONE),
                        NULL)
                 : ERROR_CALL_NOT_IMPLEMENTED;
        if (!error)
        {
            if (flags & WIN32_OF_EXIST)
                _lclose(handle);
            return (LONG)handle;
        }
    }
    lx_guest_store_u16(wire + WIN32_OFSTRUCT_ERROR_CODE, (uint16)error);
    open32SetError(error);
    return -1;
}

LONG _lclose(ULONG handle)
{
    typedef APIRET(OS2API * CloseFn)(HFILE);
    CloseFn close = (CloseFn)open32DosService("DosClose");
    ULONG error = close ? close(handle) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
        open32SetError(error);
    return error ? -1 : 0;
}

LONG _llseek(ULONG handle, LONG distance, LONG origin)
{
    typedef APIRET(OS2API * SeekFn)(HFILE, LONG, ULONG, PULONG);
    SeekFn seek = (SeekFn)open32DosService("DosSetFilePtr");
    ULONG position, error = seek ? seek(handle, distance, (ULONG)origin, &position)
                                 : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return -1;
    }
    return (LONG)position;
}

ULONG _lread(ULONG handle, PVOID output, ULONG size)
{
    typedef APIRET(OS2API * ReadFn)(HFILE, PVOID, ULONG, PULONG);
    ReadFn read = (ReadFn)open32DosService("DosRead");
    ULONG actual, error = read ? read(handle, output, size, &actual) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return UINT32_MAX;
    }
    return actual;
}

ULONG _lwrite(ULONG handle, PVOID input, ULONG size)
{
    if (!size)
    {
        typedef APIRET(OS2API * SeekFn)(HFILE, LONG, ULONG, PULONG);
        typedef APIRET(OS2API * ResizeFn)(HFILE, ULONG);
        SeekFn seek = (SeekFn)open32DosService("DosSetFilePtr");
        ResizeFn resize = (ResizeFn)open32DosService("DosSetFileSize");
        /* A zero-length write truncates the file at the current position. */
        ULONG position = 0, error = seek && resize ? seek(handle, 0, FILE_CURRENT, &position)
                                                   : ERROR_CALL_NOT_IMPLEMENTED;
        if (!error)
            error = resize(handle, position);
        if (error)
        {
            open32SetError(error);
            return UINT32_MAX;
        }
        return 0;
    }

    typedef APIRET(OS2API * WriteFn)(HFILE, PVOID, ULONG, PULONG);
    WriteFn write = (WriteFn)open32DosService("DosWrite");
    ULONG actual, error = write ? write(handle, input, size, &actual) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return UINT32_MAX;
    }
    return actual;
}

BOOL ReadFile(ULONG handle, PVOID output, ULONG size, PULONG actual, PVOID overlapped)
{
    if (!actual || overlapped)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *actual = 0;
    int typed = open32FileIO(handle, output, size, actual, FALSE);
    if (typed >= 0)
        return typed;
    ULONG count = _lread(handle, output, size);
    if (count == UINT32_MAX)
        return FALSE;
    *actual = count;
    return TRUE;
}

BOOL WriteFile(ULONG handle, PVOID input, ULONG size, PULONG actual, PVOID overlapped)
{
    if (!actual || overlapped)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *actual = 0;
    int typed = open32FileIO(handle, input, size, actual, TRUE);
    if (typed >= 0)
        return typed;
    if (!size)
    {
        typedef APIRET(OS2API * WriteFn)(HFILE, PVOID, ULONG, PULONG);
        WriteFn write = (WriteFn)open32DosService("DosWrite");
        ULONG count = 0,
              error = write ? write(handle, input, 0, &count) : ERROR_CALL_NOT_IMPLEMENTED;
        if (error)
            open32SetError(error);
        return error == 0;
    }
    ULONG count = _lwrite(handle, input, size);
    if (count == UINT32_MAX)
        return FALSE;
    *actual = count;
    return TRUE;
}

BOOL CloseHandle(ULONG handle)
{
    static BOOL (*const closers[])(ULONG) = {open32CloseSemaphore, open32CloseSync,
                                             open32CloseThread, open32CloseProcess,
                                             open32CloseFile};
    for (size_t i = 0; i < sizeof closers / sizeof *closers; i++)
        if (closers[i](handle))
            return TRUE;
    return _lclose(handle) == 0;
}

LONG _lopen(PSZ path, LONG flags)
{
    typedef APIRET(OS2API * Fn)(PSZ, PULONG, PULONG, ULONG, ULONG, ULONG, ULONG, PVOID);
    Fn call = (Fn)open32DosService("DosOpen");
    ULONG handle = 0, action = 0;
    ULONG error =
        !path || (flags & ~LOPEN_VALID_FLAGS) || (flags & OF_ACCESS_MASK) == OF_ACCESS_MASK
            ? ERROR_INVALID_PARAMETER
        : !call ? ERROR_CALL_NOT_IMPLEMENTED
                : call(path, &handle, &action, 0, FILE_NORMAL, OPEN_ACTION_OPEN_IF_EXISTS,
                       (flags & LOPEN_VALID_FLAGS) |
                           ((flags & OF_SHARE_MASK) ? 0 : OPEN_SHARE_DENYNONE),
                       NULL);
    if (error)
        open32SetError(error);
    return error ? -1 : (LONG)handle;
}

LONG _lcreat(PSZ path, LONG attributes)
{
    typedef APIRET(OS2API * Fn)(PSZ, PULONG, PULONG, ULONG, ULONG, ULONG, ULONG, PVOID);
    Fn call = (Fn)open32DosService("DosOpen");
    ULONG handle = 0, action = 0;
    /* Read-only creation selects read access; others are exclusive read/write. */
    ULONG mode = attributes & FILE_READONLY ? OPEN_ACCESS_READONLY
                                            : OPEN_SHARE_DENYREADWRITE | OPEN_ACCESS_READWRITE;
    ULONG error = !path ? ERROR_INVALID_PARAMETER
                  : !call
                      ? ERROR_CALL_NOT_IMPLEMENTED
                      : call(path, &handle, &action, 0,
                             attributes & (FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM),
                             OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_REPLACE_IF_EXISTS, mode, NULL);
    if (error)
        open32SetError(error);
    return error ? -1 : (LONG)handle;
}

ULONG SetHandleCount(ULONG count)
{
    typedef APIRET(OS2API * Fn)(PLONG, PULONG);
    Fn call = (Fn)open32DosService("DosSetRelMaxFH");
    LONG delta = 0;
    ULONG current = 0;
    if (!call)
        return 0;
    call(&delta, &current);
    delta = (LONG)(count - current);
    call(&delta, &count);
    return count;
}
