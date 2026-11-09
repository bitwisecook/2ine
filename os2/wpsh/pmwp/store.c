/* Public domain. No rights reserved. Wholly AI-authored. */
/* The object store. Folder/program and class records are atomic profile values
   protected across workers. The desktop is a virtual root. Classes registered
   from a module are guest SOM classes built on the shell classes in pmwp/; their
   objects keep their state in this store (pmwpStore* accessors). */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include "os2/wpsh/pmwp/internal.h"
#include "os2/config/wps_objects.h"
#include "os2/include/bsedos.h"
#include "emulator/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include <ctype.h>
#include <strings.h>

static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *mutex;
static char *path, *lock_path;

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

HINI store_begin(LxHostFile **lock)
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

void store_end(HINI profile, LxHostFile *lock)
{
    if (profile)
        PrfCloseProfile(profile);
    if (lock)
        lx_host_file_close(lock);
    lx_host_monitor_unlock(mutex);
}

void store_object_key(ULONG handle, char key[9])
{
    snprintf(key, 9, "%08x", (unsigned)handle);
}

BOOL store_load(HINI profile, ULONG handle, PmwpRecord *object)
{
    char key[9];
    store_object_key(handle, key);
    ULONG size = 0;
    if (!PrfQueryProfileSize(profile, "objects", key, &size) || size < 4 || size > OBJECT_LIMIT)
        return FALSE;
    char *data = malloc(size);
    if (!data)
        return FALSE;
    if (!PrfQueryProfileData(profile, "objects", key, data, &size))
        goto fail;
    const char *fields[OS2_WPS_OBJECT_FIELDS];
    if (os2_wps_object_fields(data, size, fields) != OS2_WPS_RECORD_VALID)
        goto fail;
    object->class_name = (char *)fields[OS2_WPS_OBJECT_CLASS];
    object->title = (char *)fields[OS2_WPS_OBJECT_TITLE];
    object->location = (char *)fields[OS2_WPS_OBJECT_LOCATION];
    object->setup = (char *)fields[OS2_WPS_OBJECT_SETUP];
    object->data = data;
    return TRUE;
fail:
    free(data);
    return FALSE;
}

BOOL store_save(HINI profile, ULONG handle, const PmwpRecord *object)
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
    store_object_key(handle, key);
    BOOL result = PrfWriteProfileData(profile, "objects", key, data, (ULONG)size);
    free(data);
    return result;
}

BOOL store_property(const char *setup, const char *name, char *value, size_t capacity)
{
    return os2_wps_setup_value(setup, name, value, capacity, NULL) ? TRUE : FALSE;
}

char *store_keys(HINI profile, ULONG *size)
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

ULONG store_find(HINI profile, const char *id, const char *location, const char *title)
{
    if (id && !strcasecmp(id, "<WP_DESKTOP>"))
        return 1;
    ULONG size;
    char *list = store_keys(profile, &size);
    ULONG result = 0;
    for (char *key = list; key && key < list + size && *key; key += strlen(key) + 1)
    {
        ULONG handle = (ULONG)strtoul(key, NULL, 16);
        PmwpRecord object = {0};
        if (!store_load(profile, handle, &object))
            continue;
        char identifier[1024];
        BOOL match =
            id ? store_property(object.setup, "OBJECTID", identifier, sizeof identifier) &&
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

BOOL store_ensure_identifier(HINI profile, ULONG handle, char *identifier, size_t capacity)
{
    if (handle == 1)
    {
        snprintf(identifier, capacity, "<WP_DESKTOP>");
        return TRUE;
    }
    PmwpRecord object = {0};
    if (!store_load(profile, handle, &object))
        return FALSE;
    BOOL ok = store_property(object.setup, "OBJECTID", identifier, capacity);
    if (!ok)
    {
        snprintf(identifier, capacity, "<2INE_FOLDER_%08X>", (unsigned)handle);
        size_t size = strlen(object.setup) + strlen(identifier) + 12;
        char *setup = malloc(size);
        if (setup)
        {
            snprintf(setup, size, "%s;OBJECTID=%s;", object.setup, identifier);
            object.setup = setup;
            ok = store_save(profile, handle, &object);
            free(setup);
        }
    }
    free(object.data);
    return ok;
}

ULONG store_allocate_object(HINI profile)
{
    LONG next = PrfQueryProfileInt(profile, "metadata", "next", FIRST_OBJECT);
    if (next < FIRST_OBJECT || next == INT32_MAX)
        return 0;
    char counter[16];
    snprintf(counter, sizeof counter, "%u", (unsigned)next + 1);
    return PrfWriteProfileString(profile, "metadata", "next", counter) ? (ULONG)next : 0;
}

char *store_remove_identifier(const char *setup)
{
    size_t length = strlen(setup);
    char *out = malloc(length + 1);
    if (!out)
        return NULL;
    size_t used = 0;
    const char *p = setup;
    while (*p)
    {
        const char *start = p;
        BOOL escape = FALSE;
        while (*p)
        {
            if (!escape && *p == ';')
            {
                p++;
                break;
            }
            if (!escape && *p == '^')
                escape = TRUE;
            else
                escape = FALSE;
            p++;
        }
        const char *key = start;
        while (*key == ' ' || *key == '\t')
            key++;
        if (strncasecmp(key, "OBJECTID", 8) || key[8] != '=')
        {
            size_t n = p - start;
            memcpy(out + used, start, n);
            used += n;
        }
    }
    out[used] = 0;
    return out;
}

/* Store access for the SOM classes. Each entry takes and releases the store lock,
   so no lock is held while a method body runs guest code. */
BOOL pmwpStoreLoad(ULONG handle, PmwpRecord *record)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && store_load(profile, handle, record);
    store_end(profile, lock);
    return result;
}

void pmwpStoreRelease(PmwpRecord *record)
{
    free(record->data);
    record->data = NULL;
}

ULONG pmwpStoreCreate(const char *class_name, const char *title, const char *location,
                      const char *setup)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG handle = profile ? store_allocate_object(profile) : 0;
    PmwpRecord object = {NULL, (char *)class_name, (char *)title, (char *)location, (char *)setup};
    if (handle && !store_save(profile, handle, &object))
        handle = 0;
    store_end(profile, lock);
    return handle;
}

static BOOL update_field(ULONG handle, const char *title, const char *location)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    PmwpRecord object = {0};
    BOOL result = FALSE;
    if (profile && store_load(profile, handle, &object))
    {
        if (title)
            object.title = (char *)title;
        if (location)
            object.location = (char *)location;
        result = store_save(profile, handle, &object);
    }
    free(object.data);
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreSetClass(ULONG handle, const char *class_name)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    PmwpRecord object = {0};
    BOOL result = FALSE;
    if (profile && store_load(profile, handle, &object))
    {
        object.class_name = (char *)class_name;
        result = store_save(profile, handle, &object);
    }
    free(object.data);
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreSetTitle(ULONG handle, const char *title)
{
    return update_field(handle, title, NULL);
}

BOOL pmwpStoreSetLocation(ULONG handle, const char *location)
{
    return update_field(handle, NULL, location);
}

BOOL pmwpStoreAppendSetup(ULONG handle, const char *setup)
{
    return WinSetObjectData(handle, (PSZ)setup);
}

static void value_names(ULONG handle, const char *class_name, ULONG key, char app[16],
                        char name[320])
{
    snprintf(app, 16, "state.%08x", (unsigned)handle);
    snprintf(name, 320, "%.255s.%u", class_name, (unsigned)key);
}

BOOL pmwpStoreDelete(ULONG handle)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = FALSE;
    if (profile)
    {
        char app[16], key[9];
        snprintf(app, sizeof app, "state.%08x", (unsigned)handle);
        store_object_key(handle, key);
        PrfWriteProfileData(profile, app, NULL, NULL, 0);
        result = PrfWriteProfileData(profile, "objects", key, NULL, 0);
    }
    store_end(profile, lock);
    return result;
}

ULONG pmwpStoreFind(const char *identifier)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG result = profile ? store_find(profile, identifier, NULL, NULL) : 0;
    store_end(profile, lock);
    return result;
}

ULONG *pmwpStoreList(size_t *count)
{
    *count = 0;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG size = 0;
    char *list = profile ? store_keys(profile, &size) : NULL;
    store_end(profile, lock);
    size_t entries = 0;
    for (char *key = list; key && key < list + size && *key; key += strlen(key) + 1)
        entries++;
    ULONG *handles = entries ? calloc(entries, sizeof *handles) : NULL;
    if (handles)
    {
        for (char *key = list; key < list + size && *key; key += strlen(key) + 1)
            handles[(*count)++] = (ULONG)strtoul(key, NULL, 16);
    }
    free(list);
    return handles;
}

BOOL pmwpStoreIdentifier(ULONG handle, char *identifier, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && store_ensure_identifier(profile, handle, identifier, capacity);
    store_end(profile, lock);
    return result;
}

BOOL pmwpStorePath(ULONG handle, char *output, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && pmwp_object_path(profile, handle, output, capacity, 0);
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreSetValue(ULONG handle, const char *class_name, ULONG key, const void *data,
                       ULONG size)
{
    char app[16], name[320];
    value_names(handle, class_name, key, app, name);
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && PrfWriteProfileData(profile, app, name, (PVOID)data, size);
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreGetValue(ULONG handle, const char *class_name, ULONG key, void *data, ULONG *size)
{
    char app[16], name[320];
    value_names(handle, class_name, key, app, name);
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG stored = 0;
    BOOL result = profile && PrfQueryProfileSize(profile, app, name, &stored);
    if (result && data)
    {
        if (stored > *size)
            stored = *size;
        result = PrfQueryProfileData(profile, app, name, data, &stored);
    }
    if (result)
        *size = stored;
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreOpen(ULONG handle, ULONG view, BOOL activate, HWND *frame)
{
    return pmwp_open_object(handle, view, activate, 0, frame);
}

BOOL pmwpSetupValue(const char *setup, const char *key, char *value, size_t capacity)
{
    return store_property(setup, key, value, capacity);
}

BOOL pmwpStoreClassModule(const char *class_name, char *module, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && PrfQueryProfileString(profile, "classes", (PSZ)class_name, NULL,
                                                   module, (ULONG)capacity) != 0;
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreReplacement(const char *class_name, char *replacement, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = profile && PrfQueryProfileString(profile, "replacements", (PSZ)class_name, NULL,
                                                   replacement, (ULONG)capacity) != 0;
    store_end(profile, lock);
    return result;
}

BOOL pmwpStoreStartProgram(ULONG handle, const char *parameters)
{
    PmwpRecord object = {0};
    if (!pmwpStoreLoad(handle, &object))
        return FALSE;
    char executable[4096], own[32768];
    BOOL result = FALSE;
    if (store_property(object.setup, "EXENAME", executable, sizeof executable))
    {
        own[0] = 0;
        if (parameters)
            snprintf(own, sizeof own, "%s", parameters);
        else
            store_property(object.setup, "PARAMETERS", own, sizeof own);
        STARTDATA start = {0};
        start.Length = sizeof start;
        start.PgmName = executable;
        start.PgmTitle = object.title;
        start.PgmInputs = (PBYTE)own;
        ULONG session;
        PID pid;
        result = DosStartSession(&start, &session, &pid) == 0;
    }
    free(object.data);
    return result;
}

char *pmwpHostPath(const char *guest_path)
{
    uint32 error;
    return GProcessState.makeUnixPath ? GProcessState.makeUnixPath(guest_path, &error) : NULL;
}

int pmwpPathKind(const char *guest_path)
{
    char *host = pmwpHostPath(guest_path);
    LxHostFileInfo info;
    int result = 0;
    if (host && lx_host_path_info(host, &info) == LX_HOST_FS_OK)
        result = info.kind == LX_HOST_FILE_DIRECTORY ? 2 : 1;
    free(host);
    return result;
}
