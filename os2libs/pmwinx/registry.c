/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 root aliases and typed values backed by a separate persistent profile. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx.h"
#include "os2libs/pmshapi.h"
#include "guest/handles.h"
#include "guest/abi.h"
#include "hal/host_sync.h"

typedef struct RegistryKey
{
    char *path;
    ULONG access, handle;
    struct RegistryKey *next;
} RegistryKey;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *mutex;
static RegistryKey *keys;
static HINI database;
enum
{
    KEY_QUERY_VALUE = 1,
    KEY_SET_VALUE = 2,
    KEY_CREATE_SUB_KEY = 4
};

static void initialize(void)
{
    mutex = lx_host_monitor_create();
    if (!mutex)
        abort();
    const char *path = getenv("TWOINE_REGISTRY");
    database = PrfOpenProfile(0, (PSZ)(path && *path ? path : ".2ine-registry.profile"));
}
static const char *root_name(ULONG key)
{
    switch (key)
    {
        case 0x80000000u:
        case 0xffffffecu:
            return "hkcr";
        case 0x80000001u:
        case 0xffffffeeu:
            return "hkcu";
        case 0x80000002u:
        case 0xffffffefu:
            return "hklm";
        case 0x80000003u:
        case 0xffffffedu:
            return "hku";
        default:
            return NULL;
    }
}
static char *lower_name(const char *name, const char *prefix)
{
    size_t n = name ? strlen(name) : 0, p = strlen(prefix);
    if (n > 32767 || p > 32767 || n + p > 32767)
        return NULL;
    char *result = malloc(p + n + 1);
    if (!result)
        return NULL;
    memcpy(result, prefix, p);
    for (size_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)name[i];
        result[p + i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
    result[p + n] = 0;
    return result;
}
static char *key_path(ULONG key, const char *name, ULONG *access)
{
    const char *root = root_name(key);
    LxHandleBorrow borrow = {0};
    const char *base = root;
    *access = UINT32_MAX;
    if (!base)
    {
        if (!lx_handle_borrow(key, LX_HANDLE_REGISTRY_KEY, &borrow))
            return NULL;
        RegistryKey *k = borrow.resource;
        base = k->path;
        *access = k->access;
    }
    char *prefix = malloc(strlen(base) + 2);
    char *path = NULL;
    if (prefix)
    {
        strcpy(prefix, base);
        if (name && *name)
            strcat(prefix, "\\");
        path = lower_name(name, prefix);
    }
    free(prefix);
    lx_handle_release(&borrow);
    if (path)
    {
        size_t length = strlen(path);
        while (length > 4 && path[length - 1] == '\\')
            path[--length] = 0;
    }
    return path;
}
static void key_destroy(void *value)
{
    RegistryKey *key = value;
    lx_host_monitor_lock(mutex);
    RegistryKey **slot = &keys;
    while (*slot && *slot != key)
        slot = &(*slot)->next;
    if (*slot)
        *slot = key->next;
    lx_host_monitor_unlock(mutex);
    free(key->path);
    free(key);
}
static ULONG key_new(char *path, ULONG access)
{
    RegistryKey *key = calloc(1, sizeof *key);
    if (!key)
        return 0;
    key->path = strdup(path);
    key->access = access;
    key->handle = key->path ? lx_handle_create(LX_HANDLE_REGISTRY_KEY, key, key_destroy) : 0;
    if (!key->handle)
    {
        free(key->path);
        free(key);
        return 0;
    }
    lx_host_monitor_lock(mutex);
    key->next = keys;
    keys = key;
    lx_host_monitor_unlock(mutex);
    return key->handle;
}
static int key_exists(const char *path)
{
    ULONG size;
    return !strchr(path, '\\') || PrfQueryProfileSize(database, (PSZ)path, "k:", &size);
}
LONG RegCloseKey(ULONG key)
{
    if (root_name(key))
        return NO_ERROR;
    return lx_handle_close(key, LX_HANDLE_REGISTRY_KEY) ? NO_ERROR : ERROR_INVALID_HANDLE;
}
LONG RegOpenKeyEx(ULONG key, PSZ name, ULONG options, ULONG access, PULONG output)
{
    TRACE_NATIVE("RegOpenKeyEx(%08X, '%s', %u, %X, %p)", (uint)key, name, (uint)options,
                 (uint)access, output);
    if (!output || options || (access & ~0x000f003fu))
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    if (!database)
        return ERROR_OPEN_FAILED;
    ULONG parent_access;
    char *path = key_path(key, name, &parent_access);
    if (!path)
        return ERROR_INVALID_HANDLE;
    LONG result = ERROR_FILE_NOT_FOUND;
    if (key_exists(path))
    {
        ULONG handle = key_new(path, access);
        if (handle)
            *output = handle;
        result = handle ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY;
    }
    free(path);
    return result;
}
LONG RegCreateKeyEx(ULONG key, PSZ name, ULONG reserved, PSZ class_name, ULONG options,
                    ULONG access, PVOID security, PULONG output, PULONG disposition)
{
    (void)class_name;
    if (!output || reserved || (access & ~0x000f003fu))
        return ERROR_INVALID_PARAMETER;
    if (options || security)
        return ERROR_NOT_SUPPORTED;
    lx_host_once(&once, initialize);
    if (!database)
        return ERROR_OPEN_FAILED;
    ULONG parent_access;
    char *path = key_path(key, name, &parent_access);
    if (!path)
        return ERROR_INVALID_HANDLE;
    LONG result = ERROR_ACCESS_DENIED;
    int exists = key_exists(path);
    if (exists || (parent_access & KEY_CREATE_SUB_KEY))
    {
        result = NO_ERROR;
        for (char *p = path; !result && *p; p++)
            if (*p == '\\')
            {
                *p = 0;
                if (!key_exists(path) && !PrfWriteProfileString(database, path, "k:", ""))
                    result = ERROR_WRITE_FAULT;
                *p = '\\';
            }
        if (!result && !exists && !PrfWriteProfileString(database, path, "k:", ""))
            result = ERROR_WRITE_FAULT;
        if (!result)
        {
            ULONG handle = key_new(path, access);
            if (!handle)
                result = ERROR_NOT_ENOUGH_MEMORY;
            else
            {
                *output = handle;
                if (disposition)
                    *disposition = exists ? 2 : 1;
            }
        }
    }
    free(path);
    return result;
}
LONG RegSetValueEx(ULONG key, PSZ name, ULONG reserved, ULONG type, PVOID data, ULONG size)
{
    if (reserved || (size && !data) || size > 16u * 1024u * 1024u)
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    ULONG access;
    char *path = key_path(key, NULL, &access), *value = lower_name(name, "v:");
    LONG result = ERROR_INVALID_HANDLE;
    if (path && value && database)
    {
        result = ERROR_ACCESS_DENIED;
        if (access & KEY_SET_VALUE)
        {
            uint8 *wire = malloc((size_t)size + 4);
            result = ERROR_NOT_ENOUGH_MEMORY;
            if (wire)
            {
                lx_guest_store_u32(wire, type);
                if (size)
                    memcpy(wire + 4, data, size);
                result = PrfWriteProfileData(database, path, value, wire, size + 4)
                             ? NO_ERROR
                             : ERROR_WRITE_FAULT;
                free(wire);
            }
        }
    }
    free(path);
    free(value);
    return result;
}
LONG RegQueryValueEx(ULONG key, PSZ name, PULONG reserved, PULONG type, PVOID output, PULONG size)
{
    TRACE_NATIVE("RegQueryValueEx(%08X, '%s', %p, %p, %p, %p)", (uint)key, name, reserved, type,
                 output, size);
    if (reserved || (output && !size))
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    ULONG access;
    char *path = key_path(key, NULL, &access), *value = lower_name(name, "v:");
    LONG result = ERROR_INVALID_HANDLE;
    if (path && value && database)
    {
        result = ERROR_ACCESS_DENIED;
        if (access & KEY_QUERY_VALUE)
        {
            ULONG count = 0;
            result = ERROR_FILE_NOT_FOUND;
            if (PrfQueryProfileSize(database, path, value, &count) && count >= 4)
            {
                uint8 *wire = malloc(count);
                result = ERROR_NOT_ENOUGH_MEMORY;
                if (wire)
                {
                    ULONG allocated = count;
                    result = ERROR_READ_FAULT;
                    if (PrfQueryProfileData(database, path, value, wire, &count) && count >= 4 &&
                        count <= allocated)
                    {
                        if (type)
                            *type = lx_guest_load_u32(wire);
                        ULONG capacity = size ? *size : 0;
                        if (size)
                            *size = count - 4;
                        result = output && capacity < count - 4 ? ERROR_MORE_DATA : NO_ERROR;
                        if (!result && output && count > 4)
                            memcpy(output, wire + 4, count - 4);
                    }
                    free(wire);
                }
            }
        }
    }
    free(path);
    free(value);
    return result;
}
LX_NATIVE_DESTRUCTOR(pmwinx_registry)
{
    /* Closing handles precedes unloading the destructor code they reference. */
    while (keys)
    {
        RegistryKey *key = keys;
        keys = key->next;
        lx_handle_close(key->handle, LX_HANDLE_REGISTRY_KEY);
    }
    if (database)
        PrfCloseProfile(database);
    if (mutex)
        lx_host_monitor_destroy(mutex);
}
