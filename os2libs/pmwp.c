/* Public domain. No rights reserved. Wholly AI-authored. */
/* Folder/program and class records are atomic profile values protected across workers.
   The desktop is a virtual root. SOM class loading is not supported. */
#include "os2libs/os2native.h"
#include "os2libs/pmwp.h"
#include "os2libs/pmshapi/profiles.h"
#include "hal/host_fs.h"
#include "hal/host_sync.h"
#include <strings.h>

typedef struct Object
{
    char *data;
    char *class_name, *title, *location, *setup;
} Object;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *mutex;
static char *path, *lock_path;
enum
{
    OBJECT_LIMIT = 65536,
    FIRST_OBJECT = 0x20000
};
static void initialize(void)
{
    mutex = lx_host_monitor_create();
    const char *name = getenv("TWOINE_OBJECTS");
    path = strdup(name && *name ? name : ".2ine-objects.profile");
    if (!mutex || !path)
        abort();
    lock_path = malloc(strlen(path) + 6);
    if (!lock_path)
        abort();
    strcpy(lock_path, path);
    strcat(lock_path, ".lock");
}
static HINI begin(LxHostFile **lock)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(mutex);
    LxHostFsError error;
    *lock = lx_host_file_open(lock_path,
                              LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE |
                                  LX_HOST_FILE_NOINHERIT,
                              0, &error);
    if (!*lock || lx_host_file_lock(*lock, 1) != LX_HOST_FS_OK)
        return 0;
    return lx_profile_open_host(path);
}
static void end(HINI profile, LxHostFile *lock)
{
    if (profile)
        PrfCloseProfile(profile);
    if (lock)
        lx_host_file_close(lock);
    lx_host_monitor_unlock(mutex);
}
static void object_key(ULONG handle, char key[9])
{
    snprintf(key, 9, "%08x", (unsigned)handle);
}
static BOOL load(HINI profile, ULONG handle, Object *object)
{
    char key[9];
    object_key(handle, key);
    ULONG size = 0;
    if (!PrfQueryProfileSize(profile, "objects", key, &size) || size < 4 || size > OBJECT_LIMIT)
        return FALSE;
    char *data = malloc(size);
    if (!data)
        return FALSE;
    if (!PrfQueryProfileData(profile, "objects", key, data, &size))
        goto fail;
    char **fields[] = {&object->class_name, &object->title, &object->location, &object->setup};
    size_t offset = 0;
    for (size_t i = 0; i < 4; i++)
    {
        char *nul = memchr(data + offset, 0, size - offset);
        if (!nul)
            goto fail;
        *fields[i] = data + offset;
        offset = (size_t)(nul - data) + 1;
    }
    if (offset != size)
        goto fail;
    object->data = data;
    return TRUE;
fail:
    free(data);
    return FALSE;
}
static BOOL save(HINI profile, ULONG handle, const Object *object)
{
    const char *fields[] = {object->class_name, object->title, object->location, object->setup};
    size_t size = 0;
    for (size_t i = 0; i < 4; i++)
        size += strlen(fields[i]) + 1;
    if (size > OBJECT_LIMIT)
        return FALSE;
    char *data = malloc(size);
    if (!data)
        return FALSE;
    size_t offset = 0;
    for (size_t i = 0; i < 4; i++)
    {
        size_t n = strlen(fields[i]) + 1;
        memcpy(data + offset, fields[i], n);
        offset += n;
    }
    char key[9];
    object_key(handle, key);
    BOOL result = PrfWriteProfileData(profile, "objects", key, data, (ULONG)size);
    free(data);
    return result;
}
/* Caret escapes delimiters; the last occurrence of a property takes effect. */
static BOOL property(const char *setup, const char *name, char *value, size_t capacity)
{
    BOOL found = FALSE;
    while (setup && *setup)
    {
        char key[256];
        size_t at = 0;
        while (*setup && *setup != '=' && *setup != ';')
        {
            char c = *setup++;
            if (c == '^' && *setup)
                c = *setup++;
            if (at + 1 < sizeof key)
                key[at++] = c;
        }
        key[at] = 0;
        BOOL match = *setup == '=' && !strcasecmp(key, name);
        if (*setup == '=')
            setup++;
        at = 0;
        while (*setup && *setup != ';')
        {
            char c = *setup++;
            if (c == '^' && *setup)
                c = *setup++;
            if (match && at + 1 < capacity)
                value[at++] = c;
        }
        if (match)
        {
            value[at] = 0;
            found = TRUE;
        }
        if (*setup)
            setup++;
    }
    return found;
}
static char *keys(HINI profile, ULONG *size)
{
    *size = 0;
    if (!PrfQueryProfileSize(profile, "objects", NULL, size) || !*size || *size > OBJECT_LIMIT)
        return NULL;
    char *list = calloc((size_t)*size + 1, 1);
    if (list && !PrfQueryProfileString(profile, "objects", NULL, NULL, list, *size))
    {
        free(list);
        list = NULL;
    }
    return list;
}
static ULONG find(HINI profile, const char *id, const char *location, const char *title)
{
    if (id && !strcasecmp(id, "<WP_DESKTOP>"))
        return 1;
    ULONG size;
    char *list = keys(profile, &size);
    ULONG result = 0;
    for (char *key = list; key && key < list + size && *key; key += strlen(key) + 1)
    {
        ULONG handle = (ULONG)strtoul(key, NULL, 16);
        Object object = {0};
        if (!load(profile, handle, &object))
            continue;
        char identifier[1024];
        BOOL match =
            id ? property(object.setup, "OBJECTID", identifier, sizeof identifier) &&
                     !strcasecmp(identifier, id)
               : !strcasecmp(object.location, location) && !strcasecmp(object.title, title);
        free(object.data);
        if (match)
        {
            result = handle;
            break;
        }
    }
    free(list);
    return result;
}
ULONG WinQueryObject(PSZ id)
{
    TRACE_NATIVE("WinQueryObject('%s')", id);
    if (!id || !*id)
        return 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG result = profile ? find(profile, id, NULL, NULL) : 0;
    end(profile, lock);
    return result;
}
ULONG WinCreateObject(PSZ class_name, PSZ title, PSZ setup, PSZ location, ULONG flags)
{
    TRACE_NATIVE("WinCreateObject('%s', '%s', '%s', '%s', %u)", class_name, title, setup, location,
                 (unsigned)flags);
    if (!class_name || (strcmp(class_name, "WPFolder") && strcmp(class_name, "WPProgram")) ||
        !title || !*title || strlen(title) > 4096 || !setup || strlen(setup) > 32767 || !location ||
        !*location || flags > 2)
        return 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG result = 0;
    if (!profile)
        goto done;
    ULONG parent = find(profile, location, NULL, NULL);
    Object container = {0};
    if (!parent || (parent != 1 && (!load(profile, parent, &container) ||
                                    strcmp(container.class_name, "WPFolder"))))
    {
        free(container.data);
        goto done;
    }
    free(container.data);
    char identifier[1024];
    ULONG existing = property(setup, "OBJECTID", identifier, sizeof identifier)
                         ? find(profile, identifier, NULL, NULL)
                         : 0;
    if (!existing)
        existing = find(profile, NULL, location, title);
    if (existing == 1 || (existing && flags == 0))
        goto done;
    Object object = {NULL, class_name, title, location, setup}, previous = {0};
    char *merged = NULL;
    if (existing && flags == 2)
    {
        if (!load(profile, existing, &previous) || strcmp(previous.class_name, class_name))
        {
            free(previous.data);
            goto done;
        }
        size_t size = strlen(previous.setup) + strlen(setup) + 2;
        merged = malloc(size);
        if (!merged)
        {
            free(previous.data);
            goto done;
        }
        snprintf(merged, size, "%s;%s", previous.setup, setup);
        object.setup = merged;
    }
    ULONG handle = existing;
    if (!handle)
    {
        LONG next = PrfQueryProfileInt(profile, "metadata", "next", FIRST_OBJECT);
        if (next < FIRST_OBJECT || next == INT32_MAX)
            goto free_previous;
        char counter[16];
        snprintf(counter, sizeof counter, "%u", (unsigned)next + 1);
        if (!PrfWriteProfileString(profile, "metadata", "next", counter))
            goto free_previous;
        handle = (ULONG)next;
    }
    if (save(profile, handle, &object))
        result = handle;
free_previous:
    free(merged);
    free(previous.data);
done:
    end(profile, lock);
    return result;
}
BOOL WinSetObjectData(ULONG handle, PSZ setup)
{
    TRACE_NATIVE("WinSetObjectData(%u, '%s')", (unsigned)handle, setup);
    if (!setup || strlen(setup) > 32767 || handle < FIRST_OBJECT)
        return FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = FALSE;
    Object object = {0};
    if (profile && load(profile, handle, &object))
    {
        char id[1024];
        ULONG conflict =
            property(setup, "OBJECTID", id, sizeof id) ? find(profile, id, NULL, NULL) : 0;
        size_t size = strlen(object.setup) + strlen(setup) + 2;
        char *merged = malloc(size);
        if (merged && (!conflict || conflict == handle))
        {
            snprintf(merged, size, "%s;%s", object.setup, setup);
            object.setup = merged;
            result = save(profile, handle, &object);
        }
        free(merged);
    }
    free(object.data);
    end(profile, lock);
    return result;
}
BOOL WinDestroyObject(ULONG handle)
{
    TRACE_NATIVE("WinDestroyObject(%u)", (unsigned)handle);
    if (handle < FIRST_OBJECT)
        return FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    Object object = {0};
    BOOL result = FALSE;
    if (profile && load(profile, handle, &object))
    {
        /* A nonempty folder cannot be deleted without an explicit child operation. */
        ULONG size;
        char *list = keys(profile, &size);
        char id[1024];
        BOOL empty = TRUE;
        if (property(object.setup, "OBJECTID", id, sizeof id))
            for (char *key = list; key && key < list + size && *key; key += strlen(key) + 1)
            {
                Object child = {0};
                if (load(profile, (ULONG)strtoul(key, NULL, 16), &child))
                    empty &= strcasecmp(child.location, id) != 0;
                free(child.data);
            }
        free(list);
        char key[9];
        object_key(handle, key);
        if (empty)
            result = PrfWriteProfileData(profile, "objects", key, NULL, 0);
    }
    free(object.data);
    end(profile, lock);
    return result;
}
BOOL WinRegisterObjectClass(PSZ name, PSZ module)
{
    TRACE_NATIVE("WinRegisterObjectClass('%s', '%s')", name, module);
    if (!name || !*name || !module || !*module || strnlen(name, OBJECT_LIMIT) == OBJECT_LIMIT ||
        strnlen(module, OBJECT_LIMIT) == OBJECT_LIMIT)
        return FALSE;
    /* Registration records a case-sensitive class-to-module association. The
       module need not be present until the class is instantiated. */
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = profile && PrfWriteProfileString(profile, "classes", name, module);
    end(profile, lock);
    return result;
}
BOOL WinDeregisterObjectClass(PSZ name)
{
    TRACE_NATIVE("WinDeregisterObjectClass('%s')", name);
    if (!name || !*name || strnlen(name, OBJECT_LIMIT) == OBJECT_LIMIT)
        return FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG size;
    BOOL result = profile && PrfQueryProfileSize(profile, "classes", name, &size) &&
                  PrfWriteProfileData(profile, "classes", name, NULL, 0);
    end(profile, lock);
    return result;
}
#include "os2libs/generated/pmwp-lx.h"
