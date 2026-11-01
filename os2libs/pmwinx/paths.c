/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 paths use the same current drive and directories as DOSCALLS. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/doscalls.h"
ULONG GetCurrentDirectory(ULONG capacity, PSZ output)
{
    ULONG drive = GLoaderState.current_disk;
    const char *directory = drive >= 1 && drive <= 26 ? GLoaderState.current_dir[drive - 1] : NULL;
    if (!directory)
    {
        open32SetError(15);
        return 0;
    }
    size_t length = strlen(directory);
    if (length > UINT32_MAX - 4)
    {
        open32SetError(206);
        return 0;
    }
    ULONG required = (ULONG)length + 4;
    if (capacity < required)
        return required;
    if (!output)
    {
        open32SetError(87);
        return 0;
    }
    output[0] = (char)('A' + drive - 1);
    output[1] = ':';
    output[2] = '\\';
    memcpy(output + 3, directory, length + 1);
    return required - 1;
}
BOOL SetCurrentDirectory(PSZ name)
{
    if (!name || !*name)
    {
        open32SetError(3);
        return FALSE;
    }
    char *copy = strdup(name);
    if (!copy)
    {
        open32SetError(8);
        return FALSE;
    }
    size_t length = strlen(copy);
    while (length > 1 && (copy[length - 1] == '\\' || copy[length - 1] == '/') &&
           !(length == 3 && copy[1] == ':'))
        copy[--length] = 0;
    typedef APIRET(OS2API * SetFn)(PSZ);
    SetFn set = (SetFn)open32DosService("DosSetCurrentDir");
    ULONG error = set ? set(copy) : 120;
    if (!error && length >= 2 && copy[1] == ':')
    {
        unsigned char drive = (unsigned char)copy[0];
        if (drive >= 'a' && drive <= 'z')
            drive -= 'a' - 'A';
        if (drive >= 'A' && drive <= 'Z')
            GLoaderState.current_disk = drive - 'A' + 1;
    }
    free(copy);
    if (error)
        open32SetError(error);
    return error == 0;
}

ULONG GetFileAttributes(PSZ name)
{
    TRACE_NATIVE("GetFileAttributes('%s')", name);
    if (!name || !*name)
    {
        open32SetError(name ? 3 : 87);
        return UINT32_MAX;
    }
    typedef APIRET(OS2API * QueryFn)(PSZ, ULONG, PVOID, ULONG);
    QueryFn query = (QueryFn)open32DosService("DosQueryPathInfo");
    FILESTATUS3 information = {0};
    ULONG error = query ? query(name, FIL_STANDARD, &information, sizeof information) : 120;
    if (error)
    {
        open32SetError(error);
        return UINT32_MAX;
    }
    ULONG attributes = information.attrFile & 0x37;
    return attributes ? attributes : 0x80;
}
