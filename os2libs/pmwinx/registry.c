/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 root aliases and typed values backed by a separate persistent profile. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx.h"
#include "os2libs/pmshapi.h"
#include "os2libs/pmshapi/profiles.h"
#include "guest/handles.h"
#include "guest/abi.h"
#include "hal/host_sync.h"
#include "hal/host.h"

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
    KEY_CREATE_SUB_KEY = 4,
    KEY_ENUMERATE_SUB_KEYS = 8,
    /* Open32 KEY_ALL_ACCESS includes SYNCHRONIZE in STANDARD_RIGHTS_ALL. */
    KEY_ALL_ACCESS = 0x001f003f
};

static void initialize(void)
{
    mutex = lx_host_monitor_create();
    if (!mutex)
        abort();
    const char *path = getenv("TWOINE_REGISTRY");
    database = lx_profile_open_host(path && *path ? path : ".2ine-registry.profile");
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
static BOOL touch_key(const char *path)
{
    uint64 time = lx_host_realtime_us() * 10 + 116444736000000000ULL;
    uint8 wire[8];
    lx_guest_store_u32(wire, (uint32)time);
    lx_guest_store_u32(wire + 4, (uint32)(time >> 32));
    return PrfWriteProfileData(database, (PSZ)path, "t:", wire, sizeof wire);
}
static BOOL create_key_record(char *path, const char *class_name)
{
    if (!PrfWriteProfileString(database, path, "k:", "") ||
        !PrfWriteProfileString(database, path, "c:", (PSZ)(class_name ? class_name : "")) ||
        !touch_key(path))
        return FALSE;
    char *parent = strrchr(path, '\\');
    if (!parent)
        return TRUE;
    *parent = 0;
    BOOL result = touch_key(path);
    *parent = '\\';
    return result;
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
    if (!output || options || (access & ~KEY_ALL_ACCESS))
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
LONG RegOpenKey(ULONG key, PSZ name, PULONG output)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (!name || !*name)
    {
        LxHandleBorrow borrow = {0};
        BOOL valid = root_name(key) || lx_handle_borrow(key, LX_HANDLE_REGISTRY_KEY, &borrow);
        lx_handle_release(&borrow);
        if (!valid)
            return ERROR_INVALID_HANDLE;
        *output = key;
        return NO_ERROR;
    }
    /* The registry provider has no ACLs; its default grants the supported key rights. */
    const ULONG default_access = KEY_ALL_ACCESS;
    return RegOpenKeyEx(key, name, 0, default_access, output);
}
LONG RegCreateKey(ULONG key, PSZ name, PULONG output)
{
    if ((!name || !*name) && root_name(key))
        return RegOpenKey(key, name, output);
    if (!name)
        return ERROR_INVALID_PARAMETER;
    return RegCreateKeyEx(key, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, output, NULL);
}
LONG RegCreateKeyEx(ULONG key, PSZ name, ULONG reserved, PSZ class_name, ULONG options,
                    ULONG access, PVOID security, PULONG output, PULONG disposition)
{
    TRACE_NATIVE("RegCreateKeyEx(%08X, '%s', %u, '%s', %u, %X, %p, %p, %p)", (uint)key, name,
                 (uint)reserved, class_name, (uint)options, (uint)access, security, output,
                 disposition);
    if (!output || reserved || (access & ~KEY_ALL_ACCESS) ||
        (class_name && strlen(class_name) > 32767))
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
                if (!key_exists(path) && !create_key_record(path, NULL))
                    result = ERROR_WRITE_FAULT;
                *p = '\\';
            }
        if (!result && !exists && !create_key_record(path, class_name))
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
                result =
                    PrfWriteProfileData(database, path, value, wire, size + 4) && touch_key(path)
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
LONG RegDeleteValue(ULONG key, PSZ name)
{
    lx_host_once(&once, initialize);
    ULONG access;
    char *path = key_path(key, NULL, &access), *value = lower_name(name, "v:");
    LONG result = ERROR_INVALID_HANDLE;
    if (path && value && database)
    {
        result = ERROR_ACCESS_DENIED;
        if (access & KEY_SET_VALUE)
        {
            ULONG size = 0;
            result = ERROR_FILE_NOT_FOUND;
            if (PrfQueryProfileSize(database, path, value, &size))
                result = PrfWriteProfileData(database, path, value, NULL, 0) && touch_key(path)
                             ? NO_ERROR
                             : ERROR_WRITE_FAULT;
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
LONG RegEnumKeyEx(ULONG key, ULONG index, PSZ name, PULONG name_size, PULONG reserved,
                  PSZ class_name, PULONG class_size, PVOID last_write)
{
    TRACE_NATIVE("RegEnumKeyEx(%x, %u, %p, %p, %p, %p, %p, %p)", (uint)key, (uint)index, name,
                 name_size, reserved, class_name, class_size, last_write);
    if (!name || !name_size || reserved || (class_name && !class_size))
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    if (!database)
        return ERROR_OPEN_FAILED;
    ULONG access;
    char *path = key_path(key, NULL, &access);
    if (!path)
        return ERROR_INVALID_HANDLE;
    LONG result = ERROR_ACCESS_DENIED;
    char *names = NULL;
    if (!(access & KEY_ENUMERATE_SUB_KEYS))
        goto done;
    ULONG bytes = 0;
    result = ERROR_NO_MORE_ITEMS;
    if (!PrfQueryProfileSize(database, NULL, NULL, &bytes))
        goto done;
    result = ERROR_NOT_ENOUGH_MEMORY;
    if (bytes == UINT32_MAX || !(names = calloc((size_t)bytes + 1, 1)))
        goto done;
    result = ERROR_MORE_DATA;
    if (!PrfQueryProfileData(database, NULL, NULL, names, &bytes))
        goto done;
    result = ERROR_NO_MORE_ITEMS;
    size_t prefix = strlen(path);
    for (char *entry = names; *entry; entry += strlen(entry) + 1)
    {
        if (strncmp(entry, path, prefix) || entry[prefix] != '\\' || !entry[prefix + 1] ||
            strchr(entry + prefix + 1, '\\'))
            continue;
        if (index--)
            continue;
        ULONG length = (ULONG)strlen(entry + prefix + 1), class_bytes = 0;
        PrfQueryProfileSize(database, entry, "c:", &class_bytes);
        char *class_value = calloc((size_t)class_bytes + 1, 1);
        if (!class_value)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        if (class_bytes && !PrfQueryProfileData(database, entry, "c:", class_value, &class_bytes))
        {
            free(class_value);
            result = ERROR_MORE_DATA;
            break;
        }
        ULONG class_length = (ULONG)strlen(class_value);
        result = ERROR_MORE_DATA;
        if (*name_size > length && (!class_name || *class_size > class_length))
        {
            memcpy(name, entry + prefix + 1, length + 1);
            *name_size = length;
            if (class_name)
                memcpy(class_name, class_value, class_length + 1);
            if (class_size)
                *class_size = class_length;
            if (last_write)
            {
                uint8 timestamp[8] = {0};
                ULONG count = sizeof timestamp;
                PrfQueryProfileData(database, entry, "t:", timestamp, &count);
                memcpy(last_write, timestamp, sizeof timestamp);
            }
            result = NO_ERROR;
        }
        free(class_value);
        break;
    }
done:
    free(names);
    free(path);
    return result;
}
LONG RegEnumKey(ULONG key, ULONG index, PSZ name, ULONG capacity)
{
    return RegEnumKeyEx(key, index, name, &capacity, NULL, NULL, NULL, NULL);
}
LONG RegEnumValue(ULONG key, ULONG index, PSZ name, PULONG name_size, PULONG reserved, PULONG type,
                  PVOID data, PULONG data_size)
{
    if (!name || !name_size || reserved || (data && !data_size))
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    ULONG access;
    char *path = key_path(key, NULL, &access);
    if (!path)
        return ERROR_INVALID_HANDLE;
    LONG result = ERROR_OPEN_FAILED;
    if (database)
    {
        result = ERROR_ACCESS_DENIED;
        if (access & KEY_QUERY_VALUE)
        {
            ULONG bytes = 0;
            result = ERROR_NO_MORE_ITEMS;
            if (PrfQueryProfileSize(database, path, NULL, &bytes))
            {
                char *names = calloc((size_t)bytes + 1, 1);
                result = ERROR_NOT_ENOUGH_MEMORY;
                if (names)
                {
                    result = ERROR_NO_MORE_ITEMS;
                    if (PrfQueryProfileData(database, path, NULL, names, &bytes))
                        for (char *entry = names; *entry; entry += strlen(entry) + 1)
                            if (!strncmp(entry, "v:", 2) && !index--)
                            {
                                size_t length = strlen(entry + 2);
                                if (*name_size <= length)
                                {
                                    result = RegQueryValueEx(key, entry + 2, NULL, type, NULL,
                                                             data_size);
                                    if (!result)
                                        result = ERROR_MORE_DATA;
                                }
                                else
                                {
                                    result = RegQueryValueEx(key, entry + 2, NULL, type, data,
                                                             data_size);
                                    if (!result || result == ERROR_MORE_DATA)
                                    {
                                        memcpy(name, entry + 2, length + 1);
                                        *name_size = (ULONG)length;
                                    }
                                }
                                break;
                            }
                    free(names);
                }
            }
        }
    }
    free(path);
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
