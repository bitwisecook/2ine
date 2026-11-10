/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 paths use the same current drive and directories as DOSCALLS. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/bsedos.h"
#include <stddef.h>

enum
{
    DRIVE_COUNT = 26, /* A: to Z: */
    /* "X:\\" before the directory, as GetCurrentDirectory returns it. */
    DRIVE_ROOT_LENGTH = 3,
    /* Every name of a logical-drive string: "X:\\" and its terminator. */
    DRIVE_STRING_SIZE = 4,
    VOLUME_LABEL_LIMIT = 11,
    MAX_COMPONENT_LENGTH = 255,
    /* Leaves room for "\\PRE" + four hex digits + ".TMP". */
    TEMP_DIRECTORY_LIMIT = 240,
    TEMP_UNIQUE_MASK = 0xFFFF,
    TEMP_UNIQUE_COUNT = 65535,
    FSQBUFFER2_NAME_OFFSET = offsetof(FSQBUFFER2, szName)
};

ULONG GetWindowsDirectory(PSZ output, ULONG capacity)
{
    TRACE_NATIVE("GetWindowsDirectory(%p, %u)", output, (uint)capacity);
    typedef APIRET(OS2API * QueryFn)(ULONG, ULONG, PVOID, ULONG);
    QueryFn query = (QueryFn)open32DosService("DosQuerySysInfo");
    ULONG drive = 0;
    ULONG error = query ? query(QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, &drive, sizeof drive)
                        : ERROR_CALL_NOT_IMPLEMENTED;
    if (error || drive < 1 || drive > DRIVE_COUNT)
    {
        open32SetError(error ? error : ERROR_INVALID_DRIVE);
        return 0;
    }
    char directory[] = "C:\\OS2";
    directory[0] = (char)('A' + drive - 1);
    if (capacity < sizeof directory)
        return sizeof directory;
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    memcpy(output, directory, sizeof directory);
    return sizeof directory - 1;
}

ULONG GetCurrentDirectory(ULONG capacity, PSZ output)
{
    ULONG drive = GProcessState.current_disk;
    const char *directory =
        drive >= 1 && drive <= DRIVE_COUNT ? GProcessState.current_dir[drive - 1] : NULL;
    if (!directory)
    {
        open32SetError(ERROR_INVALID_DRIVE);
        return 0;
    }
    size_t length = strlen(directory);
    if (length > UINT32_MAX - (DRIVE_ROOT_LENGTH + 1))
    {
        open32SetError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    ULONG required = (ULONG)length + DRIVE_ROOT_LENGTH + 1;
    if (capacity < required)
        return required;
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    output[0] = (char)('A' + drive - 1);
    output[1] = ':';
    output[2] = '\\';
    memcpy(output + DRIVE_ROOT_LENGTH, directory, length + 1);
    return required - 1;
}

BOOL SetCurrentDirectory(PSZ name)
{
    if (!name || !*name)
    {
        open32SetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    char *copy = strdup(name);
    if (!copy)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    size_t length = strlen(copy);
    while (length > 1 && (copy[length - 1] == '\\' || copy[length - 1] == '/') &&
           !(length == DRIVE_ROOT_LENGTH && copy[1] == ':'))
        copy[--length] = 0;
    typedef APIRET(OS2API * SetFn)(PSZ);
    SetFn set = (SetFn)open32DosService("DosSetCurrentDir");
    ULONG error = set ? set(copy) : ERROR_CALL_NOT_IMPLEMENTED;
    if (!error && length >= 2 && copy[1] == ':')
    {
        unsigned char drive = (unsigned char)copy[0];
        if (drive >= 'a' && drive <= 'z')
            drive -= 'a' - 'A';
        if (drive >= 'A' && drive <= 'Z')
            GProcessState.current_disk = drive - 'A' + 1;
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
        open32SetError(name ? ERROR_PATH_NOT_FOUND : ERROR_INVALID_PARAMETER);
        return UINT32_MAX;
    }
    typedef APIRET(OS2API * QueryFn)(PSZ, ULONG, PVOID, ULONG);
    QueryFn query = (QueryFn)open32DosService("DosQueryPathInfo");
    FILESTATUS3 information = {0};
    ULONG error = query ? query(name, FIL_STANDARD, &information, sizeof information)
                        : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return UINT32_MAX;
    }
    /* These OS/2 attribute bits have the same values as their Win32 FILE_ATTRIBUTE_ names. */
    ULONG attributes = information.attrFile &
                       (FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED);
    return attributes ? attributes : WIN32_FILE_ATTRIBUTE_NORMAL;
}

ULONG GetFullPathName(PSZ name, ULONG capacity, PSZ output, PSZ *filepart)
{
    TRACE_NATIVE("GetFullPathName('%s', %u, %p, %p)", name ? name : "", (uint)capacity, output,
                 filepart);
    if (!name || !*name)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    typedef APIRET(OS2API * QueryFn)(PSZ, ULONG, PVOID, ULONG);
    QueryFn query = (QueryFn)open32DosService("DosQueryPathInfo");
    char path[CCHMAXPATH];
    ULONG error =
        query ? query(name, FIL_QUERYFULLNAME, path, sizeof path) : ERROR_CALL_NOT_IMPLEMENTED;
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
        open32SetError(ERROR_INVALID_PARAMETER);
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
    ULONG drive = GProcessState.current_disk;
    if (root)
    {
        unsigned char letter = (unsigned char)root[0];
        if (letter >= 'a' && letter <= 'z')
            letter -= 'a' - 'A';
        if (letter < 'A' || letter > 'Z' || root[1] != ':' ||
            (root[2] && (root[2] != '\\' && root[2] != '/')) || (root[2] && root[3]))
            return WIN32_DRIVE_NO_ROOT_DIR;
        drive = letter - 'A' + 1;
    }
    if (!drive || drive > DRIVE_COUNT || !(GProcessState.diskmap & (1u << (drive - 1))))
        return WIN32_DRIVE_NO_ROOT_DIR;
    /* Configured drives are fixed, directory-backed virtual volumes. */
    return WIN32_DRIVE_FIXED;
}

ULONG GetLogicalDrives(void)
{
    return GProcessState.diskmap & ((1u << DRIVE_COUNT) - 1);
}

ULONG GetLogicalDriveStrings(ULONG capacity, PSZ output)
{
    char roots[DRIVE_COUNT * DRIVE_STRING_SIZE + 1];
    ULONG size = 0, drives = GetLogicalDrives();
    for (unsigned i = 0; i < DRIVE_COUNT; i++)
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
        open32SetError(ERROR_INVALID_PARAMETER);
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
    if (GetDriveType(root) == WIN32_DRIVE_NO_ROOT_DIR)
    {
        open32SetError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    ULONG drive =
        root ? (ULONG)((root[0] >= 'a' && root[0] <= 'z' ? root[0] - 'a' : root[0] - 'A') + 1)
             : GProcessState.current_disk;
    typedef APIRET(OS2API * VolumeFn)(ULONG, ULONG, PVOID, ULONG);
    typedef APIRET(OS2API * AttachFn)(PSZ, ULONG, ULONG, PFSQBUFFER2, PULONG);
    VolumeFn volume = (VolumeFn)open32DosService("DosQueryFSInfo");
    AttachFn attach = (AttachFn)open32DosService("DosQueryFSAttach");
    uint8 information[sizeof(FSINFO)], attachment[128];
    char device[3] = {(char)('A' + drive - 1), ':', 0};
    ULONG bytes = sizeof attachment;
    ULONG error = volume && attach ? volume(drive, FSIL_VOLSER, information, sizeof information)
                                   : ERROR_CALL_NOT_IMPLEMENTED;
    if (!error)
        error = attach(device, 0, FSAIL_QUERYNAME, (PFSQBUFFER2)attachment, &bytes);
    if (error)
    {
        open32SetError(error);
        return FALSE;
    }
    size_t length = information[offsetof(FSINFO, vol.cch)];

    /* The file-system name follows the device name and its terminator. */
    size_t fs_offset =
        FSQBUFFER2_NAME_OFFSET + lx_guest_load_u16(attachment + offsetof(FSQBUFFER2, cbName)) + 1;
    size_t fs_length = lx_guest_load_u16(attachment + offsetof(FSQBUFFER2, cbFSDName));
    if (length > VOLUME_LABEL_LIMIT || fs_offset >= bytes || fs_length >= bytes - fs_offset)
    {
        open32SetError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if ((label && label_capacity <= length) || (filesystem && filesystem_capacity <= fs_length))
    {
        open32SetError(ERROR_MORE_DATA);
        return FALSE;
    }
    if (label)
    {
        memcpy(label, information + offsetof(FSINFO, vol.szVolLabel), length);
        label[length] = 0;
    }
    if (filesystem)
    {
        memcpy(filesystem, attachment + fs_offset, fs_length);
        filesystem[fs_length] = 0;
    }
    /* The volume serial is the FSINFO creation date and time word pair. */
    if (serial)
        *serial = lx_guest_load_u32(information);
    if (component_length)
        *component_length = MAX_COMPONENT_LENGTH;
    /* The virtual DOS volume preserves the spelling of names. */
    if (flags)
        *flags = WIN32_FS_CASE_IS_PRESERVED;
    return TRUE;
}

ULONG GetSystemDirectory(PSZ output, ULONG capacity)
{
    char path[CCHMAXPATH];
    ULONG length = GetWindowsDirectory(path, sizeof path);
    if (!output || !length || length >= sizeof path - 8)
        return 0;
    memcpy(path + length, "\\SYSTEM", 8);
    length += 7;
    if (capacity <= length)
        return length + 1;
    memcpy(output, path, length + 1);
    return length;
}

ULONG GetTempPath(ULONG capacity, PSZ output)
{
    char path[CCHMAXPATH];
    ULONG n = GetEnvironmentVariable("TEMP", path, sizeof path);
    if (!n || n >= sizeof path)
        n = GetEnvironmentVariable("TMP", path, sizeof path);
    if (!n || n >= sizeof path)
        n = GetCurrentDirectory(sizeof path, path);
    if (!n || n >= sizeof path - 1)
    {
        open32SetError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    if (path[n - 1] != '\\' && path[n - 1] != '/')
        path[n++] = '\\';
    path[n] = 0;
    if (capacity <= n)
        return n + 1;
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    memcpy(output, path, n + 1);
    return n;
}

ULONG GetTempFileName(PSZ path, PSZ prefix, ULONG unique, PSZ output)
{
    if (!path || !prefix || !output || strlen(path) > TEMP_DIRECTORY_LIMIT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    typedef APIRET(OS2API * Open)(PSZ, PULONG, PULONG, ULONG, ULONG, ULONG, ULONG, PVOID);
    typedef APIRET(OS2API * Close)(ULONG);
    Open open = (Open)open32DosService("DosOpen");
    Close close = (Close)open32DosService("DosClose");
    ULONG start = unique ? unique & TEMP_UNIQUE_MASK : GetTickCount() & TEMP_UNIQUE_MASK;
    if (!start)
        start = 1;
    /* Zero is never a unique number: it asks for one to be chosen. */
    for (ULONG attempt = 0; attempt < TEMP_UNIQUE_COUNT; attempt++)
    {
        ULONG number = ((start + attempt - 1) % TEMP_UNIQUE_COUNT) + 1;
        size_t n = strlen(path);
        snprintf(output, CCHMAXPATH, "%s%s%3.3s%04X.TMP", path,
                 n && path[n - 1] != '\\' && path[n - 1] != '/' ? "\\" : "", prefix, number);
        if (unique)
            return number;
        ULONG file = 0, action = 0,
              error = open ? open(output, &file, &action, 0, FILE_NORMAL,
                                  OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_FAIL_IF_EXISTS,
                                  OPEN_SHARE_DENYNONE | OPEN_ACCESS_READWRITE, NULL)
                           : ERROR_CALL_NOT_IMPLEMENTED;
        if (!error)
        {
            if (close)
                close(file);
            return number;
        }
        if (error != ERROR_FILE_EXISTS && error != ERROR_OPEN_FAILED)
        {
            open32SetError(error);
            return 0;
        }
    }
    open32SetError(ERROR_FILE_EXISTS);
    return 0;
}

BOOL GetDiskFreeSpace(PSZ root, PVOID sectors, PVOID bytes, PVOID free_clusters,
                      PVOID total_clusters)
{
    typedef APIRET(OS2API * Fn)(ULONG, ULONG, PVOID, ULONG);
    Fn call = (Fn)open32DosService("DosQueryFSInfo");
    ULONG drive = 0;
    if (root && *root)
    {
        char c = root[0];
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (c < 'A' || c > 'Z')
        {
            open32SetError(ERROR_PATH_NOT_FOUND);
            return FALSE;
        }
        drive = c - 'A' + 1;
    }
    uint8 info[sizeof(FSALLOCATE)];
    ULONG error = call ? call(drive, FSIL_ALLOC, info, sizeof info) : ERROR_CALL_NOT_IMPLEMENTED;
    if (error)
    {
        open32SetError(error);
        return FALSE;
    }
    if (sectors)
        lx_guest_store_u32(sectors, lx_guest_load_u32(info + offsetof(FSALLOCATE, cSectorUnit)));
    if (bytes)
        lx_guest_store_u32(bytes, lx_guest_load_u16(info + offsetof(FSALLOCATE, cbSector)));
    if (free_clusters)
        lx_guest_store_u32(free_clusters,
                           lx_guest_load_u32(info + offsetof(FSALLOCATE, cUnitAvail)));
    if (total_clusters)
        lx_guest_store_u32(total_clusters, lx_guest_load_u32(info + offsetof(FSALLOCATE, cUnit)));
    return TRUE;
}

ULONG SearchPath(PSZ path, PSZ file, PSZ extension, ULONG capacity, PSZ output, PVOID part)
{
    if (!file || !*file || (capacity && !output))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    char name[CCHMAXPATH], found[CCHMAXPATH];
    size_t length = strlen(file);
    if (length >= sizeof name)
    {
        open32SetError(ERROR_FILENAME_EXCED_RANGE);
        return 0;
    }
    strcpy(name, file);
    const char *base = strrchr(name, '\\');
    if (!base)
        base = name;
    if (extension && !strchr(base, '.'))
    {
        if (length + strlen(extension) >= sizeof name)
        {
            open32SetError(ERROR_FILENAME_EXCED_RANGE);
            return 0;
        }
        strcat(name, extension);
    }
    typedef APIRET(OS2API * Fn)(ULONG, PSZ, PSZ, PVOID, ULONG);
    Fn search = (Fn)open32DosService("DosSearchPath");
    ULONG error = ERROR_FILE_NOT_FOUND;
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':'))
    {
        if (GetFileAttributes(name) != UINT32_MAX)
        {
            ULONG n = GetFullPathName(name, sizeof found, found, NULL);
            error = n && n < sizeof found ? NO_ERROR : ERROR_PATH_NOT_FOUND;
        }
    }
    else if (path)
        error = search ? search(SEARCH_PATH, path, name, found, sizeof found)
                       : ERROR_CALL_NOT_IMPLEMENTED;
    else
    {
        char directories[2048], module[CCHMAXPATH], current[CCHMAXPATH], system[CCHMAXPATH],
            windows[CCHMAXPATH], environment[1024];
        GetModuleFileName(0, module, sizeof module);
        char *slash = strrchr(module, '\\');
        if (slash)
            *slash = 0;
        else
            module[0] = 0;
        GetCurrentDirectory(sizeof current, current);
        GetSystemDirectory(system, sizeof system);
        GetWindowsDirectory(windows, sizeof windows);
        ULONG envsize = GetEnvironmentVariable("PATH", environment, sizeof environment);
        if (!envsize || envsize >= sizeof environment)
            environment[0] = 0;
        snprintf(directories, sizeof directories, "%s;%s;%s;%s;%s", module, current, system,
                 windows, environment);
        /* Win32 order: the module's directory, current, system, Windows, then PATH. */
        error = search ? search(SEARCH_PATH, directories, name, found, sizeof found)
                       : ERROR_CALL_NOT_IMPLEMENTED;
    }
    if (error)
    {
        open32SetError(error);
        return 0;
    }
    length = strlen(found);
    if (length >= capacity)
        return (ULONG)length + 1;
    memcpy(output, found, length + 1);
    if (part)
    {
        ULONG address = 0;
        const char *last = strrchr(found, '\\');
        size_t offset = last ? (size_t)(last - found + 1) : 0;
        if (!lx_execution_address(GProcessState.execution, output + offset, 1, LX_GUEST_READ,
                                  &address))
            address = 0;
        lx_guest_store_u32(part, address);
    }
    return (ULONG)length;
}
