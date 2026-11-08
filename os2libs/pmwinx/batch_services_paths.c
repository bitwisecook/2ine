/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "emulator/abi.h"
#include "host/sync.h"
#include <stdio.h>
static LxHostOnce env_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *env_monitor;

static void env_initialize(void)
{
    env_monitor = lx_host_monitor_create();
    if (!env_monitor)
        abort();
}

ULONG GetEnvironmentStrings(void)
{
    ULONG address = 0;
    if (!lx_execution_address(GProcessState.execution, GProcessState.pib.environment, 1,
                              LX_GUEST_READ, &address))
        open32SetError(8);
    return address;
}

ULONG GetEnvironmentVariable(PSZ name, PSZ output, ULONG capacity)
{
    if (!name || !*name || strchr(name, '=') || !capacity || !output)
    {
        open32SetError(87);
        return 0;
    }
    lx_host_once(&env_once, env_initialize);
    lx_host_monitor_lock(env_monitor);
    size_t length = strlen(name);
    const char *value = NULL;
    for (const char *entry = GProcessState.pib.environment; entry && *entry;
         entry += strlen(entry) + 1)
        if (!strncasecmp(entry, name, length) && entry[length] == '=')
        {
            value = entry + length + 1;
            break;
        }
    ULONG result = 0;
    if (value)
    {
        size_t n = strlen(value);
        result = (ULONG)(n < capacity ? n : n + 1);
        if (n < capacity)
            memcpy(output, value, n + 1);
    }
    else
        open32SetError(203);
    lx_host_monitor_unlock(env_monitor);
    return result;
}

BOOL SetEnvironmentVariable(PSZ name, PSZ value)
{
    if (!name || !*name || strchr(name, '=') || strlen(name) > 32767 ||
        (value && strlen(value) > 32767))
    {
        open32SetError(87);
        return FALSE;
    }
    lx_host_once(&env_once, env_initialize);
    lx_host_monitor_lock(env_monitor);
    const char *old = GProcessState.pib.environment;
    size_t name_size = strlen(name), size = 1;
    for (const char *p = old; p && *p; p += strlen(p) + 1)
        if (strncasecmp(p, name, name_size) || p[name_size] != '=')
            size += strlen(p) + 1;
    if (value)
        size += name_size + strlen(value) + 2;
    if (size == 1)
        size = 2;
    LxGuestMapping *mapping = size <= 65536 ? lx_execution_map(GProcessState.execution, 0,
                                                               (size + 4095) & ~(size_t)4095, 3, 0)
                                            : NULL;
    if (!mapping)
    {
        lx_host_monitor_unlock(env_monitor);
        open32SetError(8);
        return FALSE;
    }
    char *begin = lx_execution_mapping_host(mapping), *out = begin;
    for (const char *p = old; p && *p; p += strlen(p) + 1)
        if (strncasecmp(p, name, name_size) || p[name_size] != '=')
        {
            size_t n = strlen(p) + 1;
            memcpy(out, p, n);
            out += n;
        }
    if (value)
    {
        memcpy(out, name, name_size);
        out += name_size;
        *out++ = '=';
        size_t n = strlen(value) + 1;
        memcpy(out, value, n);
        out += n;
    }
    *out = 0;
    if (out == begin)
        out[1] = 0;
    GProcessState.pib.environment = begin;
    if (GProcessState.guest_pib)
    {
        LxGuestPib *pib = lx_execution_mapping_host(GProcessState.guest_pib);
        lx_guest_store_u32(&pib->environment, lx_execution_mapping_address(mapping));
    }
    /* Old blocks remain readable to guest code holding an earlier environment
       pointer. All mappings are reclaimed by execution teardown. */
    lx_host_monitor_unlock(env_monitor);
    return TRUE;
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

VOID GlobalMemoryStatus(PVOID output)
{
    if (!output)
    {
        open32SetError(87);
        return;
    }
    typedef APIRET(OS2API * Fn)(ULONG, ULONG, PVOID, ULONG);
    Fn query = (Fn)open32DosService("DosQuerySysInfo");
    ULONG info[5] = {0};
    ULONG error = query ? query(17, 21, info, sizeof info) : 120;
    if (error)
    {
        open32SetError(error);
        return;
    }
    /* TOTPHYSMEM, TOTRESMEM, TOTAVAILMEM: the paging file is what exceeds physical memory. */
    ULONG available = info[0] - info[1], paging = info[2] - info[0];
    ULONG fields[] = {32,      info[0] ? (ULONG)((uint64_t)info[1] * 100 / info[0]) : 0,
                      info[0], available,
                      paging,  paging,
                      info[0], available};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
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
