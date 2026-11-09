/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 paths use the same current drive and directories as DOSCALLS. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/bsedos.h"

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
    ULONG drive = GProcessState.current_disk;
    const char *directory = drive >= 1 && drive <= 26 ? GProcessState.current_dir[drive - 1] : NULL;
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
    ULONG drive = GProcessState.current_disk;
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
    if (!drive || drive > 26 || !(GProcessState.diskmap & (1u << (drive - 1))))
        return 1;
    /* Configured drives are fixed, directory-backed virtual volumes. */
    return 3;
}

ULONG GetLogicalDrives(void)
{
    return GProcessState.diskmap & 0x03ffffffu;
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
             : GProcessState.current_disk;
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

ULONG GetSystemDirectory(PSZ output, ULONG capacity)
{
    char path[260];
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
    char path[260];
    ULONG n = GetEnvironmentVariable("TEMP", path, sizeof path);
    if (!n || n >= sizeof path)
        n = GetEnvironmentVariable("TMP", path, sizeof path);
    if (!n || n >= sizeof path)
        n = GetCurrentDirectory(sizeof path, path);
    if (!n || n >= sizeof path - 1)
    {
        open32SetError(206);
        return 0;
    }
    if (path[n - 1] != '\\' && path[n - 1] != '/')
        path[n++] = '\\';
    path[n] = 0;
    if (capacity <= n)
        return n + 1;
    if (!output)
    {
        open32SetError(87);
        return 0;
    }
    memcpy(output, path, n + 1);
    return n;
}

ULONG GetTempFileName(PSZ path, PSZ prefix, ULONG unique, PSZ output)
{
    if (!path || !prefix || !output || strlen(path) > 240)
    {
        open32SetError(87);
        return 0;
    }
    typedef APIRET(OS2API * Open)(PSZ, PULONG, PULONG, ULONG, ULONG, ULONG, ULONG, PVOID);
    typedef APIRET(OS2API * Close)(ULONG);
    Open open = (Open)open32DosService("DosOpen");
    Close close = (Close)open32DosService("DosClose");
    ULONG start = unique ? unique & 0xFFFF : GetTickCount() & 0xFFFF;
    if (!start)
        start = 1;
    for (ULONG attempt = 0; attempt < 65535; attempt++)
    {
        ULONG number = ((start + attempt - 1) % 65535) + 1;
        size_t n = strlen(path);
        snprintf(output, 260, "%s%s%3.3s%04X.TMP", path,
                 n && path[n - 1] != '\\' && path[n - 1] != '/' ? "\\" : "", prefix, number);
        if (unique)
            return number;
        ULONG file = 0, action = 0,
              error = open ? open(output, &file, &action, 0, 0, 0x10, 0x42, NULL) : 120;
        if (!error)
        {
            if (close)
                close(file);
            return number;
        }
        if (error != 80 && error != 110) /* ERROR_FILE_EXISTS, ERROR_OPEN_FAILED */
        {
            open32SetError(error);
            return 0;
        }
    }
    open32SetError(80);
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
            open32SetError(3);
            return FALSE;
        }
        drive = c - 'A' + 1;
    }
    uint8 info[18];
    ULONG error = call ? call(drive, 1, info, sizeof info) : 120;
    if (error)
    {
        open32SetError(error);
        return FALSE;
    }
    if (sectors)
        lx_guest_store_u32(sectors, lx_guest_load_u32(info + 4));
    if (bytes)
        lx_guest_store_u32(bytes, lx_guest_load_u16(info + 16));
    if (free_clusters)
        lx_guest_store_u32(free_clusters, lx_guest_load_u32(info + 12));
    if (total_clusters)
        lx_guest_store_u32(total_clusters, lx_guest_load_u32(info + 8));
    return TRUE;
}

ULONG SearchPath(PSZ path, PSZ file, PSZ extension, ULONG capacity, PSZ output, PVOID part)
{
    if (!file || !*file || (capacity && !output))
    {
        open32SetError(87);
        return 0;
    }
    char name[260], found[260];
    size_t length = strlen(file);
    if (length >= sizeof name)
    {
        open32SetError(206);
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
            open32SetError(206);
            return 0;
        }
        strcat(name, extension);
    }
    typedef APIRET(OS2API * Fn)(ULONG, PSZ, PSZ, PVOID, ULONG);
    Fn search = (Fn)open32DosService("DosSearchPath");
    ULONG error = 2;
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':'))
    {
        if (GetFileAttributes(name) != UINT32_MAX)
        {
            ULONG n = GetFullPathName(name, sizeof found, found, NULL);
            error = n && n < sizeof found ? 0 : 3;
        }
    }
    else if (path)
        error = search ? search(0, path, name, found, sizeof found) : 120;
    else
    {
        char directories[2048], module[260], current[260], system[260], windows[260],
            environment[1024];
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
        error = search ? search(0, directories, name, found, sizeof found) : 120;
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
