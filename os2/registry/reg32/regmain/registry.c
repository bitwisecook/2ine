/* Public domain. No rights reserved. Wholly AI-authored. */
/* The Win32-style registry for Open32: root aliases and typed values backed by a
   separate persistent profile. */
#include "os2/include/private/export.h"
#include "os2/include/os2win.h"
#include "os2/include/pmshl.h"
#include "os2/include/private/pmp.h"
#include "emulator/handles.h"
#include "emulator/abi.h"
#include "host/sync.h"
#include "host/clock.h"

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
    KEY_ALL_ACCESS = 0x001f003f,
    REG_SZ = 1
};

/* Open32's root keys, and the Win32 values ported code also passes. */
#define HKEY_CLASSES_ROOT 0xFFFFFFECu
#define HKEY_CURRENT_USER 0xFFFFFFEEu
#define HKEY_LOCAL_MACHINE 0xFFFFFFEFu
#define HKEY_USERS 0xFFFFFFEDu
#define WIN32_HKEY_CLASSES_ROOT 0x80000000u
#define WIN32_HKEY_CURRENT_USER 0x80000001u
#define WIN32_HKEY_LOCAL_MACHINE 0x80000002u
#define WIN32_HKEY_USERS 0x80000003u

/* The profile holds each key as an application. Its entries are "k:" (the key exists),
   "c:" (its class), "t:" (its last-write FILETIME) and "v:name" (a value: a ULONG type
   followed by the data). */
#define KEY_ENTRY "k:"
#define CLASS_ENTRY "c:"
#define TIME_ENTRY "t:"
#define VALUE_PREFIX "v:"

enum
{
    VALUE_PREFIX_LENGTH = sizeof VALUE_PREFIX - 1,
    VALUE_TYPE_SIZE = sizeof(ULONG),
    FILETIME_SIZE = 8,
    /* Key and class names are limited to 32767 characters. */
    REGISTRY_NAME_MAXIMUM = 32767,
    /* The shortest path a trailing backslash is stripped from: a root alias and one
       character. */
    ROOT_PATH_LENGTH = 4
};

/* Values above 16 MB are refused. */
static const ULONG REGISTRY_VALUE_MAXIMUM = 16u * 1024u * 1024u;
/* RegSetValue's REG_SZ data must be shorter than this. */
static const ULONG SET_VALUE_LIMIT = 0x3fed;
/* FILETIME counts 100 ns intervals from 1601; this is 1970 in those units. */
static const uint64 FILETIME_UNIX_EPOCH = 116444736000000000ULL;

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
        case WIN32_HKEY_CLASSES_ROOT:
        case HKEY_CLASSES_ROOT:
            return "hkcr";
        case WIN32_HKEY_CURRENT_USER:
        case HKEY_CURRENT_USER:
            return "hkcu";
        case WIN32_HKEY_LOCAL_MACHINE:
        case HKEY_LOCAL_MACHINE:
            return "hklm";
        case WIN32_HKEY_USERS:
        case HKEY_USERS:
            return "hku";
        default:
            return NULL;
    }
}

static char *lower_name(const char *name, const char *prefix)
{
    size_t n = name ? strlen(name) : 0, p = strlen(prefix);
    if (n > REGISTRY_NAME_MAXIMUM || p > REGISTRY_NAME_MAXIMUM || n + p > REGISTRY_NAME_MAXIMUM)
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
    char *prefix = malloc(strlen(base) + sizeof "\\");
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
        while (length > ROOT_PATH_LENGTH && path[length - 1] == '\\')
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
    return !strchr(path, '\\') || PrfQueryProfileSize(database, (PSZ)path, KEY_ENTRY, &size);
}

static BOOL touch_key(const char *path)
{
    uint64 time = lx_host_realtime_us() * 10 + FILETIME_UNIX_EPOCH;
    uint8 wire[FILETIME_SIZE];
    lx_guest_store_u32(wire, (uint32)time);
    lx_guest_store_u32(wire + sizeof(uint32), (uint32)(time >> 32));
    return PrfWriteProfileData(database, (PSZ)path, TIME_ENTRY, wire, sizeof wire);
}

static BOOL create_key_record(char *path, const char *class_name)
{
    if (!PrfWriteProfileString(database, path, KEY_ENTRY, "") ||
        !PrfWriteProfileString(database, path, CLASS_ENTRY, (PSZ)(class_name ? class_name : "")) ||
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

LONG RegDeleteKey(ULONG key, PSZ name)
{
    TRACE_NATIVE("RegDeleteKey(%08X, '%s')", (uint)key, name);
    if (!name || !*name)
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    if (!database)
        return ERROR_OPEN_FAILED;
    ULONG access;
    char *path = key_path(key, name, &access);
    if (!path)
        return ERROR_INVALID_HANDLE;
    LONG result = ERROR_FILE_NOT_FOUND;
    char *names = NULL;
    if (!key_exists(path))
        goto done;
    result = ERROR_ACCESS_DENIED;
    if (!(access & KEY_CREATE_SUB_KEY))
        goto done;
    ULONG bytes = 0;
    if (!PrfQueryProfileSize(database, NULL, NULL, &bytes))
        goto done;
    result = ERROR_NOT_ENOUGH_MEMORY;
    if (bytes == UINT32_MAX || !(names = calloc((size_t)bytes + 1, 1)))
        goto done;
    result = ERROR_OPEN_FAILED;
    if (!PrfQueryProfileData(database, NULL, NULL, names, &bytes))
        goto done;
    size_t prefix = strlen(path);
    for (char *entry = names; *entry; entry += strlen(entry) + 1)
        if (!strncmp(entry, path, prefix) && entry[prefix] == '\\')
        {
            result = ERROR_ACCESS_DENIED; /* Leaf deletion never removes subkeys. */
            goto done;
        }
    result = PrfWriteProfileData(database, path, NULL, NULL, 0) ? NO_ERROR : ERROR_OPEN_FAILED;
done:
    free(names);
    free(path);
    return result;
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
        (class_name && strlen(class_name) > REGISTRY_NAME_MAXIMUM))
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
    if (reserved || (size && !data) || size > REGISTRY_VALUE_MAXIMUM)
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    ULONG access;
    char *path = key_path(key, NULL, &access), *value = lower_name(name, VALUE_PREFIX);
    LONG result = ERROR_INVALID_HANDLE;
    if (path && value && database)
    {
        result = ERROR_ACCESS_DENIED;
        if (access & KEY_SET_VALUE)
        {
            uint8 *wire = malloc((size_t)size + VALUE_TYPE_SIZE);
            result = ERROR_NOT_ENOUGH_MEMORY;
            if (wire)
            {
                lx_guest_store_u32(wire, type);
                if (size)
                    memcpy(wire + VALUE_TYPE_SIZE, data, size);
                result = PrfWriteProfileData(database, path, value, wire, size + VALUE_TYPE_SIZE) &&
                                 touch_key(path)
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
    char *path = key_path(key, NULL, &access), *value = lower_name(name, VALUE_PREFIX);
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
    char *path = key_path(key, NULL, &access), *value = lower_name(name, VALUE_PREFIX);
    LONG result = ERROR_INVALID_HANDLE;
    if (path && value && database)
    {
        result = ERROR_ACCESS_DENIED;
        if (access & KEY_QUERY_VALUE)
        {
            ULONG count = 0;
            result = ERROR_FILE_NOT_FOUND;
            if (PrfQueryProfileSize(database, path, value, &count) && count >= VALUE_TYPE_SIZE)
            {
                uint8 *wire = malloc(count);
                result = ERROR_NOT_ENOUGH_MEMORY;
                if (wire)
                {
                    ULONG allocated = count;
                    result = ERROR_READ_FAULT;
                    if (PrfQueryProfileData(database, path, value, wire, &count) &&
                        count >= VALUE_TYPE_SIZE && count <= allocated)
                    {
                        if (type)
                            *type = lx_guest_load_u32(wire);
                        ULONG capacity = size ? *size : 0;
                        const ULONG data_size = count - VALUE_TYPE_SIZE;
                        if (size)
                            *size = data_size;
                        result = output && capacity < data_size ? ERROR_MORE_DATA : NO_ERROR;
                        if (!result && output && data_size)
                            memcpy(output, wire + VALUE_TYPE_SIZE, data_size);
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
        PrfQueryProfileSize(database, entry, CLASS_ENTRY, &class_bytes);
        char *class_value = calloc((size_t)class_bytes + 1, 1);
        if (!class_value)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        if (class_bytes &&
            !PrfQueryProfileData(database, entry, CLASS_ENTRY, class_value, &class_bytes))
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
                uint8 timestamp[FILETIME_SIZE] = {0};
                ULONG count = sizeof timestamp;
                PrfQueryProfileData(database, entry, TIME_ENTRY, timestamp, &count);
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
                            if (!strncmp(entry, VALUE_PREFIX, VALUE_PREFIX_LENGTH) && !index--)
                            {
                                char *value_name = entry + VALUE_PREFIX_LENGTH;
                                size_t length = strlen(value_name);
                                if (*name_size <= length)
                                {
                                    result = RegQueryValueEx(key, value_name, NULL, type, NULL,
                                                             data_size);
                                    if (!result)
                                        result = ERROR_MORE_DATA;
                                }
                                else
                                {
                                    result = RegQueryValueEx(key, value_name, NULL, type, data,
                                                             data_size);
                                    if (!result || result == ERROR_MORE_DATA)
                                    {
                                        memcpy(name, value_name, length + 1);
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

LX_NATIVE_DESTRUCTOR(registry_keys)
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

LONG RegQueryValue(ULONG key, PSZ subkey, PSZ output, PULONG size)
{
    if (!size)
        return ERROR_INVALID_PARAMETER;
    ULONG opened = key;
    LONG error = NO_ERROR;
    if (subkey && *subkey)
        error = RegOpenKey(key, subkey, &opened);
    if (!error)
        error = RegQueryValueEx(opened, NULL, NULL, NULL, output, size);
    if (opened != key)
        RegCloseKey(opened);
    return error;
}

LONG RegSetValue(ULONG key, PSZ subkey, ULONG type, PVOID data, ULONG size)
{
    if (type != REG_SZ || !data || !size || size >= SET_VALUE_LIMIT)
        return ERROR_INVALID_PARAMETER;
    const char *terminator = memchr(data, 0, size);
    size_t length = terminator ? (size_t)(terminator - (const char *)data) : size;
    char *text = malloc(length + 1);
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(text, data, length);
    text[length] = 0;
    ULONG opened = key;
    LONG error = NO_ERROR;
    if (subkey && *subkey)
        error = RegCreateKey(key, subkey, &opened);
    if (!error)
        error = RegSetValueEx(opened, NULL, 0, 1, text, (ULONG)length + 1);
    if (opened != key)
        RegCloseKey(opened);
    free(text);
    return error;
}

LONG RegQueryInfoKey(ULONG key, PSZ class_name, PULONG class_size, PULONG reserved, PULONG subkeys,
                     PULONG max_subkey, PULONG max_class, PULONG values, PULONG max_value_name,
                     PULONG max_value_data, PULONG security, PVOID last_write)
{
    if (reserved || (class_name && !class_size))
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    ULONG access;
    char *path = key_path(key, NULL, &access);
    if (!path)
        return ERROR_INVALID_HANDLE;
    LONG result = NO_ERROR;
    ULONG nsub = 0, nvalues = 0, longest_sub = 0, longest_class = 0, longest_value = 0,
          largest_data = 0;
    char *classes = NULL, *applications = NULL, *entries = NULL;
    if (!(access & KEY_QUERY_VALUE) || !(access & KEY_ENUMERATE_SUB_KEYS))
    {
        result = ERROR_ACCESS_DENIED;
        goto info_done;
    }
    if (!database || !key_exists(path))
    {
        result = ERROR_FILE_NOT_FOUND;
        goto info_done;
    }
    ULONG bytes = 0;
    if (PrfQueryProfileSize(database, path, CLASS_ENTRY, &bytes))
    {
        classes = calloc((size_t)bytes + 1, 1);
        if (!classes)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            goto info_done;
        }
        if (bytes && !PrfQueryProfileData(database, path, CLASS_ENTRY, classes, &bytes))
        {
            result = ERROR_READ_FAULT;
            goto info_done;
        }
    }
    ULONG class_length = classes ? (ULONG)strlen(classes) : 0;
    if (class_name && *class_size <= class_length)
        result = ERROR_MORE_DATA;
    else if (class_name)
        memcpy(class_name, classes ? classes : "", class_length + 1);
    if (class_size)
        *class_size = class_length;
    bytes = 0;
    if (PrfQueryProfileSize(database, NULL, NULL, &bytes))
    {
        applications = calloc((size_t)bytes + 1, 1);
        if (!applications)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            goto info_done;
        }
        if (!PrfQueryProfileData(database, NULL, NULL, applications, &bytes))
        {
            result = ERROR_READ_FAULT;
            goto info_done;
        }
        size_t prefix = strlen(path);
        for (char *entry = applications; *entry; entry += strlen(entry) + 1)
        {
            if (strncmp(entry, path, prefix) || entry[prefix] != '\\' || !entry[prefix + 1] ||
                strchr(entry + prefix + 1, '\\'))
                continue;
            nsub++;
            ULONG length = (ULONG)strlen(entry + prefix + 1);
            if (length > longest_sub)
                longest_sub = length;
            ULONG class_bytes = 0;
            if (PrfQueryProfileSize(database, entry, CLASS_ENTRY, &class_bytes) && class_bytes &&
                class_bytes - 1 > longest_class)
                longest_class = class_bytes - 1;
        }
    }
    bytes = 0;
    if (PrfQueryProfileSize(database, path, NULL, &bytes))
    {
        entries = calloc((size_t)bytes + 1, 1);
        if (!entries)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            goto info_done;
        }
        if (!PrfQueryProfileData(database, path, NULL, entries, &bytes))
        {
            result = ERROR_READ_FAULT;
            goto info_done;
        }
        for (char *entry = entries; *entry; entry += strlen(entry) + 1)
            if (!strncmp(entry, VALUE_PREFIX, VALUE_PREFIX_LENGTH))
            {
                nvalues++;
                ULONG length = (ULONG)strlen(entry + VALUE_PREFIX_LENGTH);
                if (length > longest_value)
                    longest_value = length;
                ULONG value_bytes = 0;
                if (PrfQueryProfileSize(database, path, entry, &value_bytes) &&
                    value_bytes >= VALUE_TYPE_SIZE && value_bytes - VALUE_TYPE_SIZE > largest_data)
                    largest_data = value_bytes - VALUE_TYPE_SIZE;
            }
    }
    if (subkeys)
        *subkeys = nsub;
    if (max_subkey)
        *max_subkey = longest_sub;
    if (max_class)
        *max_class = longest_class;
    if (values)
        *values = nvalues;
    if (max_value_name)
        *max_value_name = longest_value;
    if (max_value_data)
        *max_value_data = largest_data;
    if (security)
        *security = 0;
    if (last_write)
    {
        memset(last_write, 0, FILETIME_SIZE);
        ULONG timestamp_size = FILETIME_SIZE;
        PrfQueryProfileData(database, path, TIME_ENTRY, last_write, &timestamp_size);
    }
info_done:
    free(classes);
    free(applications);
    free(entries);
    free(path);
    return result;
}
