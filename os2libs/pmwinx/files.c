/* Public domain. No rights reserved. Wholly AI-authored. */
/* Legacy Open32 HFILE operations share DOSCALLS' file table and path semantics. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/doscalls.h"
#include "hal/host_fs.h"
LONG OpenFile(PSZ name, PVOID record, ULONG flags)
{
    TRACE_NATIVE("OpenFile('%s', %p, %X)", name, record, flags);
    if (!record || (flags & ~0xFF73u) || ((flags & 3) == 3))
    {
        open32SetError(87);
        return -1;
    }
    uint8 *wire = record;
    const char *source = flags & 0x8000 ? (char *)wire + 8 : name;
    if (!source || ((flags & 0x8000) && !memchr(source, 0, 128)))
    {
        open32SetError(87);
        return -1;
    }
    char path[260];
    if (strlen(source) >= sizeof path)
    {
        open32SetError(206);
        return -1;
    }
    strcpy(path, source);
    memset(wire, 0, 136);
    wire[0] = 136;
    wire[1] = 1;
    uint32 error = 0;
    char *host_path = GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(path, &error) : NULL;
    if (!host_path)
        error = error ? error : 87;
    else
    {
        char *canonical = lx_host_path_canonical(host_path);
        const char *full = canonical ? canonical : host_path;
        size_t length = strlen(full);
        if (length + 3 > 128)
            error = 206;
        else
        {
            char *output = (char *)wire + 8;
            output[0] = 'C';
            output[1] = ':';
            for (size_t i = 0; i <= length; i++)
                output[i + 2] = full[i] == '/' ? '\\' : full[i];
        }
        free(canonical);
        free(host_path);
    }
    if (!error && (flags & 0x100))
        return 0;
    /* Verify/prompt UI and restrictive sharing require a matching provider. */
    if (!error && ((flags & (0x400 | 0x2000)) || ((flags & 0x70) && (flags & 0x70) != 0x40)))
        error = 50;
    if (!error && (flags & 0x200))
    {
        typedef APIRET(OS2API * DeleteFn)(PSZ);
        DeleteFn remove = (DeleteFn)open32DosService("DosDelete");
        error = remove ? remove(path) : 120;
        if (!error)
            return 0;
    }
    HFILE handle = 0;
    if (!error)
    {
        typedef APIRET(OS2API * OpenFn)(PSZ, PHFILE, PULONG, ULONG, ULONG, ULONG, ULONG, PEAOP2);
        OpenFn open = (OpenFn)open32DosService("DosOpen");
        ULONG action;
        error = open ? open(path, &handle, &action, 0, 0, flags & 0x1000 ? 0x12 : 1,
                            (flags & 3) | 0x40, NULL)
                     : 120;
        if (!error)
        {
            if (flags & 0x4000)
                _lclose(handle);
            return (LONG)handle;
        }
    }
    lx_guest_store_u16(wire + 2, (uint16)error);
    open32SetError(error);
    return -1;
}
LONG _lclose(ULONG handle)
{
    typedef APIRET(OS2API * CloseFn)(HFILE);
    CloseFn close = (CloseFn)open32DosService("DosClose");
    ULONG error = close ? close(handle) : 120;
    if (error)
        open32SetError(error);
    return error ? -1 : 0;
}
LONG _llseek(ULONG handle, LONG distance, LONG origin)
{
    typedef APIRET(OS2API * SeekFn)(HFILE, LONG, ULONG, PULONG);
    SeekFn seek = (SeekFn)open32DosService("DosSetFilePtr");
    ULONG position, error = seek ? seek(handle, distance, (ULONG)origin, &position) : 120;
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
    ULONG actual, error = read ? read(handle, output, size, &actual) : 120;
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
        ULONG position = 0, error = seek && resize ? seek(handle, 0, 1, &position) : 120;
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
    ULONG actual, error = write ? write(handle, input, size, &actual) : 120;
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
        open32SetError(87);
        return FALSE;
    }
    *actual = 0;
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
        open32SetError(87);
        return FALSE;
    }
    *actual = 0;
    if (!size)
    {
        typedef APIRET(OS2API * WriteFn)(HFILE, PVOID, ULONG, PULONG);
        WriteFn write = (WriteFn)open32DosService("DosWrite");
        ULONG count = 0, error = write ? write(handle, input, 0, &count) : 120;
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
    return open32CloseThread(handle) || _lclose(handle) == 0;
}
