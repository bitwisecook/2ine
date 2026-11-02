/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 paths use the same current drive and directories as DOSCALLS. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/doscalls.h"
ULONG GetWindowsDirectory(PSZ output, ULONG capacity)
{
    TRACE_NATIVE("GetWindowsDirectory(%p, %u)", output, (uint)capacity);
    typedef APIRET(OS2API * QueryFn)(ULONG, ULONG, PVOID, ULONG);
    QueryFn query = (QueryFn)open32DosService("DosQuerySysInfo");
    ULONG drive = 0;
    ULONG error = query ? query(QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, &drive, sizeof drive) : 120;
    if (error || drive < 1 || drive > 26)
    {
        open32SetError(error ? error : 15);
        return 0;
    }
    char directory[] = "C:\\OS2";
    directory[0] = (char)('A' + drive - 1);
    if (capacity < sizeof directory)
        return sizeof directory;
    if (!output)
    {
        open32SetError(87);
        return 0;
    }
    memcpy(output, directory, sizeof directory);
    return sizeof directory - 1;
}

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

ULONG GetFullPathName(PSZ name, ULONG capacity, PSZ output, PSZ *filepart)
{
    TRACE_NATIVE("GetFullPathName('%s', %u, %p, %p)", name ? name : "", (uint)capacity, output,
                 filepart);
    if (!name || !*name)
    {
        open32SetError(87);
        return 0;
    }
    typedef APIRET(OS2API * QueryFn)(PSZ, ULONG, PVOID, ULONG);
    QueryFn query = (QueryFn)open32DosService("DosQueryPathInfo");
    char path[CCHMAXPATH];
    ULONG error = query ? query(name, FIL_QUERYFULLNAME, path, sizeof path) : 120;
    if (error)
    {
        open32SetError(error);
        return 0;
    }
    ULONG length = (ULONG)strlen(path);
    if (capacity <= length)
        return length + 1;
    if (!output)
    {
        open32SetError(87);
        return 0;
    }
    memcpy(output, path, length + 1);
    if (filepart)
    {
        char *last = strrchr(output, '\\');
        *filepart = last && last[1] ? last + 1 : NULL;
    }
    return length;
}

ULONG GetDriveType(PSZ root)
{
    TRACE_NATIVE("GetDriveType('%s')", root ? root : "");
    ULONG drive = GLoaderState.current_disk;
    if (root)
    {
        unsigned char letter = (unsigned char)root[0];
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        if (letter < 'A' || letter > 'Z' || root[1] != ':' ||
            (root[2] && (root[2] != '\\' && root[2] != '/')) || (root[2] && root[3]))
            return 1; /* DRIVE_NO_ROOT_DIR */
        drive = letter - 'A' + 1;
    }
    if (!drive || drive > 26 || !(GLoaderState.diskmap & (1u << (drive - 1))))
        return 1;
    /* Configured drives are fixed, directory-backed virtual volumes. */
    return 3;
}
ULONG GetLogicalDrives(void)
{
    return GLoaderState.diskmap & 0x03ffffffu;
}
ULONG GetLogicalDriveStrings(ULONG capacity, PSZ output)
{
    char roots[26 * 4 + 1];
    ULONG size = 0, drives = GetLogicalDrives();
    for (unsigned i = 0; i < 26; i++)
        if (drives & (1u << i))
        {
            roots[size++] = (char)('A' + i);
            roots[size++] = ':';
            roots[size++] = '\\';
            roots[size++] = 0;
        }
    roots[size] = 0;
    if (capacity <= size)
        return size + 1;
    if (!output)
    {
        open32SetError(87);
        return 0;
    }
    memcpy(output, roots, size + 1);
    return size;
}
BOOL GetVolumeInformation(PSZ root, PSZ label, ULONG label_capacity, PULONG serial,
                          PULONG component_length, PULONG flags, PSZ filesystem,
                          ULONG filesystem_capacity)
{
    TRACE_NATIVE("GetVolumeInformation('%s', %p, %u, %p, %p, %p, %p, %u)", root ? root : "", label,
                 (uint)label_capacity, serial, component_length, flags, filesystem,
                 (uint)filesystem_capacity);
    if (GetDriveType(root) == 1)
    {
        open32SetError(3);
        return FALSE;
    }
    ULONG drive =
        root ? (ULONG)((root[0] >= 'a' && root[0] <= 'z' ? root[0] - 'a' : root[0] - 'A') + 1)
             : GLoaderState.current_disk;
    typedef APIRET(OS2API * VolumeFn)(ULONG, ULONG, PVOID, ULONG);
    typedef APIRET(OS2API * AttachFn)(PSZ, ULONG, ULONG, PFSQBUFFER2, PULONG);
    VolumeFn volume = (VolumeFn)open32DosService("DosQueryFSInfo");
    AttachFn attach = (AttachFn)open32DosService("DosQueryFSAttach");
    uint8 information[17], attachment[128];
    char device[3] = {(char)('A' + drive - 1), ':', 0};
    ULONG bytes = sizeof attachment;
    ULONG error = volume && attach ? volume(drive, 2, information, sizeof information) : 120;
    if (!error)
        error = attach(device, 0, 1, (PFSQBUFFER2)attachment, &bytes);
    if (error)
    {
        open32SetError(error);
        return FALSE;
    }
    size_t length = information[4];
    size_t fs_offset = 8 + lx_guest_load_u16(attachment + 2) + 1;
    size_t fs_length = lx_guest_load_u16(attachment + 4);
    if (length > 11 || fs_offset >= bytes || fs_length >= bytes - fs_offset)
    {
        open32SetError(13);
        return FALSE;
    }
    if ((label && label_capacity <= length) || (filesystem && filesystem_capacity <= fs_length))
    {
        open32SetError(234);
        return FALSE;
    }
    if (label)
    {
        memcpy(label, information + 5, length);
        label[length] = 0;
    }
    if (filesystem)
    {
        memcpy(filesystem, attachment + fs_offset, fs_length);
        filesystem[fs_length] = 0;
    }
    if (serial)
        *serial = lx_guest_load_u32(information);
    if (component_length)
        *component_length = 255;
    if (flags)
        *flags = 2; /* FILE_CASE_PRESERVED_NAMES: the virtual DOS volume preserves spelling. */
    return TRUE;
}
