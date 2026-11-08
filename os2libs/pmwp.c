/* Public domain. No rights reserved. Wholly AI-authored. */
/* Folder/program and class records are atomic profile values protected across workers.
   The desktop is a virtual root. Classes registered from a module are guest SOM classes
   built on the shell classes in pmwp/; their objects keep their state in this store. */
#include "os2libs/os2native.h"
#include "os2libs/pmwp.h"
#include "os2libs/pmwp/internal.h"
#include "os2libs/pmwin.h"
#include "os2libs/sesmgr.h"
#include "os2libs/pmwin/graphics.h"
#include "guest/abi.h"
#include "os2libs/pmshapi/profiles.h"
#include "hal/host_fs.h"
#include "hal/host_sync.h"
#include <ctype.h>
#include <strings.h>

typedef PmwpRecord Object;
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
        while (at && (key[at - 1] == ' ' || key[at - 1] == '\t'))
            key[--at] = 0;
        char *start = key;
        while (*start == ' ' || *start == '\t')
            start++;
        BOOL match = *setup == '=' && !strcasecmp(start, name);
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
    if (!class_name || !*class_name || strlen(class_name) > 255 || !title || !*title ||
        strlen(title) > 4096 || !setup || strlen(setup) > 32767 || !location || !*location ||
        flags > 2)
        return 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG result = 0;
    if (!profile)
        goto done;
    char resolved_class[256];
    snprintf(resolved_class, sizeof resolved_class, "%s", class_name);
    for (unsigned i = 0; i < 256; i++)
    {
        char next[256];
        if (!PrfQueryProfileString(profile, "replacements", resolved_class, NULL, next,
                                   sizeof next))
            break;
        snprintf(resolved_class, sizeof resolved_class, "%s", next);
        if (i == 255)
            goto done;
    }
    class_name = resolved_class;
    if (strcmp(class_name, "WPFolder") && strcmp(class_name, "WPProgram") &&
        strcmp(class_name, "WPShadow"))
    {
        /* Any other class exists only as a guest SOM class in its registered module.
           Its methods run guest code, so the store is released first. */
        char module[260];
        if (!PrfQueryProfileString(profile, "classes", class_name, NULL, module, sizeof module))
            goto done;
        end(profile, lock);
        return pmwpCreateRegistered(class_name, module, title, setup, location, flags);
    }
    char shadow_title[4097], shadow_target[4097];
    if (!strcmp(class_name, "WPShadow"))
    {
        if (!property(setup, "SHADOWID", shadow_target, sizeof shadow_target))
            goto done;
        char *target = shadow_target;
        while (*target == ' ' || *target == '\t')
            target++;
        size_t length = strlen(target);
        while (length && (target[length - 1] == ' ' || target[length - 1] == '\t'))
            target[--length] = 0;
        if (!length)
            goto done;
        ULONG source = find(profile, target, NULL, NULL);
        Object original = {0};
        if (source == 1)
            strcpy(shadow_title, "Desktop");
        else if (source && load(profile, source, &original))
        {
            snprintf(shadow_title, sizeof shadow_title, "%s", original.title);
            free(original.data);
        }
        else
        {
            uint32 error;
            char *host =
                GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(target, &error) : NULL;
            BOOL exists = host && lx_host_path_exists(host);
            free(host);
            if (!exists)
                goto done;
            const char *base = target;
            for (const char *p = target; *p; p++)
                if (*p == '\\' || *p == '/')
                    base = p + 1;
            if (!*base)
                goto done;
            snprintf(shadow_title, sizeof shadow_title, "%s", base);
        }
        title = shadow_title;
    }
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
    BOOL has_identifier = property(setup, "OBJECTID", identifier, sizeof identifier);
    ULONG existing = has_identifier ? find(profile, identifier, NULL, NULL) : 0;
    if (!existing && !has_identifier)
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
    char configured_title[32768];
    if (property(object.setup, "TITLE", configured_title, sizeof configured_title))
    {
        if (!*configured_title || strlen(configured_title) > 4096)
            goto free_previous;
        object.title = configured_title;
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
            char title[32768];
            BOOL titled = property(merged, "TITLE", title, sizeof title);
            if (!titled || (*title && strlen(title) <= 4096))
            {
                if (titled)
                    object.title = title;
                result = save(profile, handle, &object);
            }
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

BOOL WinIsSomDDReady(void)
{
    return FALSE;
}

APIRET WinRestartSomDD(BOOL state)
{
    return state ? 50 : 0; /* ERROR_NOT_SUPPORTED: no DSOM daemon provider. */
}

/* Class enumeration retains the original variable-length OBJCLASS layout:
   three guest pointers followed by the two ASCIIZ strings, with no host padding. */
static const char *workplace_classes[] = {
    "WPObject", "WPAbstract", "WPTransient", "WPFileSystem", "WPFolder",      "WPDataFile",
    "WPDisk",   "WPProgram",  "WPDesktop",   "WPShadow",     "WPClassManager"};

static BOOL builtin_class(const char *name)
{
    for (size_t i = 0; i < sizeof workplace_classes / sizeof *workplace_classes; i++)
        if (!strcmp(name, workplace_classes[i]))
            return TRUE;
    return FALSE;
}

BOOL WinEnumObjectClasses(PVOID guest_output, PULONG capacity)
{
    if (!capacity || (uintptr_t)guest_output > UINT32_MAX)
        return FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    if (!profile)
    {
        end(profile, lock);
        return FALSE;
    }
    ULONG list_size = 0;
    PrfQueryProfileSize(profile, "classes", NULL, &list_size);
    char *list = list_size && list_size <= OBJECT_LIMIT ? calloc(list_size + 1, 1) : NULL;
    if (list && !PrfQueryProfileString(profile, "classes", NULL, NULL, list, list_size))
    {
        free(list);
        list = NULL;
    }

    typedef struct Entry
    {
        char *name, *module;
    } Entry;

    Entry *entries = NULL;
    size_t count = 0, total = 0;
    BOOL ok = TRUE;
    for (size_t i = 0; i < sizeof workplace_classes / sizeof *workplace_classes; i++)
    {
        Entry *next = realloc(entries, (count + 1) * sizeof *entries);
        if (!next)
        {
            ok = FALSE;
            break;
        }
        entries = next;
        entries[count].name = strdup(workplace_classes[i]);
        entries[count].module = strdup("PMWP");
        if (!entries[count].name || !entries[count].module)
        {
            free(entries[count].name);
            free(entries[count].module);
            ok = FALSE;
            break;
        }
        total += 14 + strlen(entries[count].name) + strlen(entries[count].module);
        count++;
    }
    for (char *p = list; ok && p && p < list + list_size && *p; p += strlen(p) + 1)
    {
        if (builtin_class(p))
            continue;
        ULONG bytes = 0;
        if (!PrfQueryProfileSize(profile, "classes", p, &bytes) || !bytes || bytes > OBJECT_LIMIT)
            continue;
        char *module = calloc(bytes + 1, 1);
        if (!module)
        {
            ok = FALSE;
            break;
        }
        PrfQueryProfileString(profile, "classes", p, NULL, module, bytes + 1);
        Entry *next = realloc(entries, (count + 1) * sizeof *entries);
        if (!next)
        {
            free(module);
            ok = FALSE;
            break;
        }
        entries = next;
        entries[count].name = strdup(p);
        entries[count].module = module;
        if (!entries[count].name)
        {
            free(module);
            ok = FALSE;
            break;
        }
        total += 14 + strlen(p) + strlen(module);
        count++;
    }
    ULONG supplied = *capacity;
    *capacity = (ULONG)total;
    if (total > OBJECT_LIMIT)
        ok = FALSE;
    if (ok && guest_output)
    {
        if (supplied < total)
            ok = FALSE;
        else
        {
            uint8 *wire = calloc(total ? total : 1, 1);
            if (!wire)
                ok = FALSE;
            else
            {
                size_t at = 0;
                for (size_t i = 0; i < count; i++)
                {
                    size_t n = strlen(entries[i].name) + 1, m = strlen(entries[i].module) + 1,
                           next = at + 12 + n + m;
                    uint32 base = (uint32)(uintptr_t)guest_output;
                    if ((uint64_t)base + next > UINT32_MAX)
                    {
                        ok = FALSE;
                        break;
                    }
                    lx_guest_store_u32(wire + at, i + 1 < count ? base + next : 0);
                    lx_guest_store_u32(wire + at + 4, base + at + 12);
                    lx_guest_store_u32(wire + at + 8, base + at + 12 + n);
                    memcpy(wire + at + 12, entries[i].name, n);
                    memcpy(wire + at + 12 + n, entries[i].module, m);
                    at = next;
                }
                if (ok)
                    ok = lx_execution_write(GLoaderState.execution, (uint32)(uintptr_t)guest_output,
                                            wire, total);
                free(wire);
            }
        }
    }
    for (size_t i = 0; i < count; i++)
    {
        free(entries[i].name);
        free(entries[i].module);
    }
    free(entries);
    free(list);
    end(profile, lock);
    return ok;
}

BOOL WinReplaceObjectClass(PSZ old_name, PSZ new_name, BOOL replace)
{
    if (!old_name || !*old_name || !new_name || !*new_name || strlen(old_name) > 255 ||
        strlen(new_name) > 255 || !strcmp(old_name, new_name))
        return FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL ok = FALSE;
    ULONG size;
    if (profile &&
        (builtin_class(old_name) || PrfQueryProfileSize(profile, "classes", old_name, &size)) &&
        (builtin_class(new_name) || PrfQueryProfileSize(profile, "classes", new_name, &size)))
    {
        char name[256];
        snprintf(name, sizeof name, "%s", new_name);
        BOOL cycle = FALSE;
        for (unsigned i = 0; i < 256; i++)
        {
            char next[256];
            if (!strcmp(name, old_name))
            {
                cycle = TRUE;
                break;
            }
            if (!PrfQueryProfileString(profile, "replacements", name, NULL, next, sizeof next))
                break;
            snprintf(name, sizeof name, "%s", next);
            if (i == 255)
                cycle = TRUE;
        }
        if (!cycle)
            ok =
                PrfWriteProfileString(profile, "replacements", old_name, replace ? new_name : NULL);
    }
    end(profile, lock);
    return ok;
}

BOOL WinStoreWindowPos(PSZ app, PSZ key, HWND window)
{
    if (!app || !*app || !key || !*key)
        return FALSE;
    SWP p;
    if (!WinQueryWindowPos(window, &p))
        return FALSE;
    uint8 data[50] = {0};
    ULONG style = WinQueryWindowULong(window, -2);
    ULONG flags =
        (style & WS_MINIMIZED ? SWP_MINIMIZE : 0) | (style & WS_MAXIMIZED ? SWP_MAXIMIZE : 0);
    lx_guest_store_u16(data, 0x7b6a);
    lx_guest_store_u32(data + 2, flags);
    lx_guest_store_u16(data + 6, p.x);
    lx_guest_store_u16(data + 8, p.y);
    lx_guest_store_u16(data + 10, p.cx);
    lx_guest_store_u16(data + 12, p.cy);
    lx_guest_store_u32(data + 14, 1);
    for (unsigned i = 0; i < 4; i++)
        lx_guest_store_u16(data + 18 + i * 2, WinQueryWindowUShort(window, 0x0c + i * 2));
    lx_guest_store_u32(data + 26, 1);
    lx_guest_store_u16(data + 30, WinQueryWindowUShort(window, 0x14));
    lx_guest_store_u16(data + 32, WinQueryWindowUShort(window, 0x16));
    lx_guest_store_u32(data + 34, WinQuerySysValue(HWND_DESKTOP, 20));
    lx_guest_store_u32(data + 38, WinQuerySysValue(HWND_DESKTOP, 21));
    lx_guest_store_u32(data + 42, WinQuerySysValue(HWND_DESKTOP, 4));
    lx_guest_store_u32(data + 46, WinQuerySysValue(HWND_DESKTOP, 5));
    return PrfWriteProfileData(HINI_USERPROFILE, app, key, data, sizeof data);
}

BOOL WinRestoreWindowPos(PSZ app, PSZ key, HWND window)
{
    if (!app || !*app || !key || !*key)
        return FALSE;
    uint8 data[50];
    ULONG size = sizeof data;
    if (!PrfQueryProfileData(HINI_USERPROFILE, app, key, data, &size) || size != sizeof data ||
        lx_guest_load_u16(data) != 0x7b6a)
        return FALSE;
    LONG x = (SHORT)lx_guest_load_u16(data + 6), y = (SHORT)lx_guest_load_u16(data + 8),
         cx = lx_guest_load_u16(data + 10), cy = lx_guest_load_u16(data + 12),
         width = WinQuerySysValue(HWND_DESKTOP, 20), height = WinQuerySysValue(HWND_DESKTOP, 21);
    ULONG old_width = lx_guest_load_u32(data + 34), old_height = lx_guest_load_u32(data + 38);
    if (old_width && old_width != (ULONG)width)
        x = (LONG)((int64_t)x * width / old_width);
    if (old_height && old_height != (ULONG)height)
        y = (LONG)((int64_t)y * height / old_height);
    if (cx < 1 || cy < 1)
        return FALSE;
    if (x >= width)
        x = width - cx;
    if (y >= height)
        y = height - cy;
    if (lx_guest_load_u32(data + 14))
        for (unsigned i = 0; i < 4; i++)
            WinSetWindowUShort(window, 0x0c + i * 2, lx_guest_load_u16(data + 18 + i * 2));
    if (lx_guest_load_u32(data + 26))
    {
        WinSetWindowUShort(window, 0x14, lx_guest_load_u16(data + 30));
        WinSetWindowUShort(window, 0x16, lx_guest_load_u16(data + 32));
    }
    ULONG flags = SWP_MOVE | SWP_SIZE | SWP_SHOW;
    ULONG state = lx_guest_load_u32(data + 2) & (SWP_MINIMIZE | SWP_MAXIMIZE);
    if (state)
        flags |= state;
    else
        flags |= SWP_RESTORE;
    return WinSetWindowPos(window, HWND_TOP, x, y, cx, cy, flags);
}

static BOOL object_path(HINI profile, ULONG handle, char *output, size_t capacity, unsigned depth)
{
    if (depth > 64 || !capacity)
        return FALSE;
    if (handle == 1)
    {
        char desktop[260];
        if (!PrfQueryProfileString(profile, "metadata", "desktopPath", NULL, desktop,
                                   sizeof desktop))
            return FALSE;
        if (strlen(desktop) >= capacity)
            return FALSE;
        strcpy(output, desktop);
        return TRUE;
    }
    Object object = {0};
    if (!load(profile, handle, &object))
        return FALSE;
    BOOL ok = FALSE;
    if (property(object.setup, "PATH", output, capacity))
        ok = TRUE;
    else if (!strcmp(object.class_name, "WPFolder") || !strcmp(object.class_name, "WPDataFile") ||
             !strcmp(object.class_name, "WPFileSystem"))
    {
        char parent[4096];
        ULONG container = find(profile, object.location, NULL, NULL);
        if (container && object_path(profile, container, parent, sizeof parent, depth + 1))
        {
            size_t n = strlen(parent), m = strlen(object.title);
            if (n + m + 2 <= capacity)
            {
                memcpy(output, parent, n);
                if (n && parent[n - 1] != '\\')
                    output[n++] = '\\';
                memcpy(output + n, object.title, m + 1);
                ok = TRUE;
            }
        }
    }
    free(object.data);
    return ok;
}

BOOL WinQueryObjectPath(ULONG handle, PSZ output, ULONG capacity)
{
    if (!output || !capacity || capacity > 65536)
        return FALSE;
    output[0] = 0;
    char path_buffer[4096];
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL ok = profile && object_path(profile, handle, path_buffer, sizeof path_buffer, 0);
    if (ok)
    {
        size_t n = strlen(path_buffer);
        if (n >= capacity)
            ok = FALSE;
        else
            memcpy(output, path_buffer, n + 1);
    }
    end(profile, lock);
    return ok;
}

BOOL WinQueryActiveDesktopPathname(PSZ output, ULONG capacity)
{
    return WinQueryObjectPath(1, output, capacity);
}

BOOL WinSaveObject(ULONG handle, BOOL async)
{
    (void)async;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    Object object = {0};
    BOOL ok = profile && load(profile, handle, &object) && save(profile, handle, &object);
    free(object.data);
    end(profile, lock);
    return ok;
}

static BOOL ensure_identifier(HINI profile, ULONG handle, char *identifier, size_t capacity)
{
    if (handle == 1)
    {
        snprintf(identifier, capacity, "<WP_DESKTOP>");
        return TRUE;
    }
    Object object = {0};
    if (!load(profile, handle, &object))
        return FALSE;
    BOOL ok = property(object.setup, "OBJECTID", identifier, capacity);
    if (!ok)
    {
        snprintf(identifier, capacity, "<2INE_FOLDER_%08X>", (unsigned)handle);
        size_t size = strlen(object.setup) + strlen(identifier) + 12;
        char *setup = malloc(size);
        if (setup)
        {
            snprintf(setup, size, "%s;OBJECTID=%s;", object.setup, identifier);
            object.setup = setup;
            ok = save(profile, handle, &object);
            free(setup);
        }
    }
    free(object.data);
    return ok;
}

static ULONG allocate_object(HINI profile)
{
    LONG next = PrfQueryProfileInt(profile, "metadata", "next", FIRST_OBJECT);
    if (next < FIRST_OBJECT || next == INT32_MAX)
        return 0;
    char counter[16];
    snprintf(counter, sizeof counter, "%u", (unsigned)next + 1);
    return PrfWriteProfileString(profile, "metadata", "next", counter) ? (ULONG)next : 0;
}

static BOOL destination_folder(HINI profile, ULONG handle)
{
    if (handle == 1)
        return TRUE;
    Object d = {0};
    BOOL ok = load(profile, handle, &d) && !strcmp(d.class_name, "WPFolder");
    free(d.data);
    return ok;
}

ULONG WinMoveObject(ULONG handle, ULONG destination, ULONG reserved)
{
    if (reserved || handle == 1 || handle == destination)
        return 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    Object object = {0};
    ULONG result = 0;
    char id[1024];
    if (profile && destination_folder(profile, destination) && load(profile, handle, &object) &&
        ensure_identifier(profile, destination, id, sizeof id))
    {
        BOOL cycle = FALSE;
        ULONG at = destination;
        for (unsigned depth = 0; at && at != 1 && depth < 64; depth++)
        {
            if (at == handle)
            {
                cycle = TRUE;
                break;
            }
            Object parent = {0};
            if (!load(profile, at, &parent))
                break;
            at = find(profile, parent.location, NULL, NULL);
            free(parent.data);
            if (depth == 63)
                cycle = TRUE;
        }
        if (!cycle)
        {
            object.location = id;
            if (save(profile, handle, &object))
                result = handle;
        }
    }
    free(object.data);
    end(profile, lock);
    return result;
}

static char *remove_identifier(const char *setup)
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

static ULONG copy_object(HINI profile, ULONG handle, ULONG destination, unsigned depth)
{
    if (depth > 64)
        return 0;
    Object source = {0};
    char id[1024];
    if (!load(profile, handle, &source) || !ensure_identifier(profile, destination, id, sizeof id))
    {
        free(source.data);
        return 0;
    }
    char *setup = remove_identifier(source.setup);
    ULONG result = 0;
    if (!setup)
        goto done;
    ULONG copy = allocate_object(profile);
    if (!copy)
        goto done;
    Object object = {NULL, source.class_name, source.title, id, setup};
    if (!save(profile, copy, &object))
        goto done;
    result = copy;
    if (!strcmp(source.class_name, "WPFolder"))
    {
        char source_id[1024];
        if (property(source.setup, "OBJECTID", source_id, sizeof source_id))
        {
            ULONG size;
            char *list = keys(profile, &size);
            for (char *p = list; p && p < list + size && *p; p += strlen(p) + 1)
            {
                ULONG child = (ULONG)strtoul(p, NULL, 16);
                Object original = {0};
                BOOL match = child != copy && load(profile, child, &original) &&
                             !strcmp(original.location, source_id);
                free(original.data);
                if (match && !copy_object(profile, child, copy, depth + 1))
                {
                    result = 0;
                    break;
                }
            }
            free(list);
        }
    }
done:
    free(setup);
    free(source.data);
    return result;
}

static BOOL contains_folder(HINI profile, ULONG object, ULONG destination)
{
    ULONG at = destination;
    for (unsigned i = 0; at && at != 1 && i < 64; i++)
    {
        if (at == object)
            return TRUE;
        Object folder = {0};
        if (!load(profile, at, &folder))
            return TRUE;
        at = find(profile, folder.location, NULL, NULL);
        free(folder.data);
        if (i == 63)
            return TRUE;
    }
    return FALSE;
}

ULONG WinCopyObject(ULONG handle, ULONG destination, ULONG reserved)
{
    if (reserved || handle == 1 || handle == destination)
        return 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG result = profile && destination_folder(profile, destination) &&
                           !contains_folder(profile, handle, destination)
                       ? copy_object(profile, handle, destination, 0)
                       : 0;
    end(profile, lock);
    return result;
}

ULONG WinCreateShadow(ULONG handle, ULONG destination, ULONG reserved)
{
    if (reserved)
        return 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG result = 0;
    Object source = {0};
    char source_id[1024], destination_id[1024];
    if (profile && destination_folder(profile, destination) &&
        (handle == 1 || load(profile, handle, &source)) &&
        ensure_identifier(profile, handle, source_id, sizeof source_id) &&
        ensure_identifier(profile, destination, destination_id, sizeof destination_id))
    {
        char setup[1100];
        snprintf(setup, sizeof setup, "SHADOWID=%s;", source_id);
        Object shadow = {NULL, "WPShadow", handle == 1 ? "Desktop" : source.title, destination_id,
                         setup};
        ULONG copy = allocate_object(profile);
        if (copy && save(profile, copy, &shadow))
            result = copy;
    }
    free(source.data);
    end(profile, lock);
    return result;
}

BOOL WinFreeFileIcon(HPOINTER pointer)
{
    return WinDestroyPointer(pointer);
}

APIRET WinRestartWPDServer(BOOL state)
{
    return WinRestartSomDD(state);
}

BOOL WinIsWPDServerReady(void)
{
    return WinIsSomDDReady();
}

HPOINTER WinLoadFileIcon(PSZ filename, BOOL private_icon)
{
    /* Each retained image is independently owned, including the caller's
       requested private copy. The PM pointer destructor releases its pixels. */
    (void)private_icon;
    return lx_pm_pointer_file(filename);
}

static MRESULT OS2API folder_view(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_SIZE)
    {
        ULONG size = (ULONG)(uintptr_t)second;
        HWND list = WinWindowFromID(window, 1);
        if (list)
            WinSetWindowPos(list, HWND_TOP, 0, 0, (USHORT)size, (USHORT)(size >> 16),
                            SWP_MOVE | SWP_SIZE);
        return 0;
    }
    if (message == WM_CONTROL && ((ULONG)(uintptr_t)first >> 16) == 5)
    {
        HWND list = WinWindowFromID(window, 1);
        LONG selected = (SHORT)(uintptr_t)WinSendMsg(list, 0x165, 0, 0);
        if (selected >= 0)
        {
            ULONG object =
                (ULONG)(uintptr_t)WinSendMsg(list, 0x16a, (MPARAM)(uintptr_t)selected, 0);
            if (object)
                WinOpenObject(object, 0, TRUE);
        }
        return 0;
    }
    if (message == WM_CLOSE)
    {
        HWND frame = WinQueryWindow(window, 5);
        return (MRESULT)(uintptr_t)WinDestroyWindow(frame ? frame : window);
    }
    return WinDefWindowProc(window, message, first, second);
}

static BOOL open_object(ULONG handle, ULONG view, BOOL activate, unsigned depth, HWND *opened)
{
    if (depth > 64)
        return FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    Object object = {0};
    BOOL valid = profile && (handle == 1 || load(profile, handle, &object));
    if (!valid)
    {
        end(profile, lock);
        return FALSE;
    }
    if (handle == 1)
    {
        object.class_name = "WPFolder";
        object.title = "Desktop";
        object.location = "";
        object.setup = "";
    }
    if (!strcmp(object.class_name, "WPShadow"))
    {
        char id[1024];
        ULONG target =
            property(object.setup, "SHADOWID", id, sizeof id) ? find(profile, id, NULL, NULL) : 0;
        free(object.data);
        end(profile, lock);
        return target && open_object(target, view, activate, depth + 1, opened);
    }
    /* A view opened by a class method carries `opened`; its subclass record opens as
       the base class it extends. Any other class opens through its SOM methods. */
    char executable[4096], parameters[32768];
    BOOL has_program = property(object.setup, "EXENAME", executable, sizeof executable);
    BOOL folder = !strcmp(object.class_name, "WPFolder") || (opened && !has_program);
    BOOL program = !strcmp(object.class_name, "WPProgram") || (opened && has_program);
    if (!folder && !program)
    {
        free(object.data);
        end(profile, lock);
        return pmwpViewStored(handle, view);
    }
    BOOL result = FALSE;
    if (folder)
    {
        char folder_id[1024];
        if (!ensure_identifier(profile, handle, folder_id, sizeof folder_id))
            goto done;

        typedef struct Child
        {
            ULONG handle;
            char *title;
        } Child;

        Child *children = NULL;
        size_t count = 0;
        ULONG size;
        char *list = keys(profile, &size);
        for (char *p = list; p && p < list + size && *p; p += strlen(p) + 1)
        {
            ULONG child = (ULONG)strtoul(p, NULL, 16);
            Object entry = {0};
            if (load(profile, child, &entry) && !strcmp(entry.location, folder_id))
            {
                Child *next = realloc(children, (count + 1) * sizeof *children);
                if (next)
                {
                    children = next;
                    children[count].handle = child;
                    children[count].title = strdup(entry.title);
                    if (children[count].title)
                        count++;
                }
            }
            free(entry.data);
        }
        free(list);
        char *title = strdup(object.title);
        free(object.data);
        object.data = NULL;
        end(profile, lock);
        HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
        WinRegisterClass(hab, "2ine.WorkplaceFolder", folder_view, 0, 4);

        {
            ULONG flags = 0x3b;
            HWND client = 0,
                 frame =
                     WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "2ine.WorkplaceFolder",
                                        title ? title : "Folder", WS_VISIBLE, 0, 0, &client);
            if (frame)
            {
                HWND child =
                    WinCreateWindow(client, (PSZ)(uintptr_t)WC_LISTBOX, "", WS_VISIBLE | 0x20000, 0,
                                    0, 480, 320, client, HWND_TOP, 1, NULL, NULL);
                if (child)
                {
                    for (size_t i = 0; i < count; i++)
                    {
                        LONG at = (LONG)(intptr_t)WinSendMsg(child, 0x161, (MPARAM)(intptr_t)-1,
                                                             children[i].title);
                        if (at >= 0)
                            WinSendMsg(child, 0x169, (MPARAM)(uintptr_t)at,
                                       (MPARAM)(uintptr_t)children[i].handle);
                    }
                    WinSetWindowPos(frame, HWND_TOP, 40, 40, 520, 380,
                                    SWP_MOVE | SWP_SIZE | SWP_SHOW | (activate ? SWP_ACTIVATE : 0));
                    if (opened)
                        *opened = frame;
                    result = TRUE;
                }
                else
                    WinDestroyWindow(frame);
            }
        }
        for (size_t i = 0; i < count; i++)
            free(children[i].title);
        free(children);
        free(title);
        return result;
    }
    if (program)
    {
        if (has_program)
        {
            parameters[0] = 0;
            property(object.setup, "PARAMETERS", parameters, sizeof parameters);
            STARTDATA start = {0};
            start.Length = sizeof start;
            start.PgmName = executable;
            start.PgmTitle = object.title;
            start.PgmInputs = (PBYTE)parameters;
            start.FgBg = activate ? 0 : 1;
            start.SessionType = 0;
            ULONG session;
            PID pid;
            result = DosStartSession(&start, &session, &pid) == 0;
        }
    }
done:
    free(object.data);
    end(profile, lock);
    return result;
}

BOOL WinOpenObject(ULONG handle, ULONG view, BOOL activate)
{
    return open_object(handle, view, activate, 0, NULL);
}

ULONG WPFilesystemNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPFILESYSTEM, major, minor);
}

ULONG WPFolderNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPFOLDER, major, minor);
}

ULONG WPDataFileNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPDATAFILE, major, minor);
}

ULONG WPObjectNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPOBJECT, major, minor);
}

ULONG WPAbstractNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPABSTRACT, major, minor);
}

ULONG WPTransientNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPTRANSIENT, major, minor);
}

ULONG WPDesktopNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPDESKTOP, major, minor);
}

ULONG WPClassManagerNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPCLASSMANAGER, major, minor);
}

ULONG M_WPFolderNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_M_WPFOLDER, major, minor);
}

ULONG M_WPDataFileNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_M_WPDATAFILE, major, minor);
}

#if LX_LEGACY
static uint32 exported_data(void *host)
{
    uint32 address = 0;
    return lx_execution_address(GLoaderState.execution, host, 1, LX_GUEST_READ, &address) ? address
                                                                                          : 0;
}

uint32 pmwpExportedClassData(PmwpClassId id)
{
    switch (id)
    {
        case PMWP_WPFILESYSTEM:
            return exported_data(data_address_WPFilesystemClassData);
        case PMWP_WPFOLDER:
            return exported_data(data_address_WPFolderClassData);
        case PMWP_WPOBJECT:
            return exported_data(data_address_WPObjectClassData);
        case PMWP_WPDISK:
            return exported_data(data_address_WPDiskClassData);
        case PMWP_WPPROGRAM:
            return exported_data(data_address_WPProgramClassData);
        case PMWP_M_WPFILESYSTEM:
            return exported_data(data_address_M_WPFileSystemClassData);
        case PMWP_M_WPOBJECT:
            return exported_data(data_address_M_WPObjectClassData);
        default:
            return 0;
    }
}
#else
uint32 pmwpExportedClassData(PmwpClassId id)
{
    (void)id;
    return 0;
}
#endif

/* Store access for the SOM classes. Each entry takes and releases the store lock,
   so no lock is held while a method body runs guest code. */
BOOL pmwpStoreLoad(ULONG handle, PmwpRecord *record)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = profile && load(profile, handle, record);
    end(profile, lock);
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
    HINI profile = begin(&lock);
    ULONG handle = profile ? allocate_object(profile) : 0;
    Object object = {NULL, (char *)class_name, (char *)title, (char *)location, (char *)setup};
    if (handle && !save(profile, handle, &object))
        handle = 0;
    end(profile, lock);
    return handle;
}

static BOOL update_field(ULONG handle, const char *title, const char *location)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    Object object = {0};
    BOOL result = FALSE;
    if (profile && load(profile, handle, &object))
    {
        if (title)
            object.title = (char *)title;
        if (location)
            object.location = (char *)location;
        result = save(profile, handle, &object);
    }
    free(object.data);
    end(profile, lock);
    return result;
}

BOOL pmwpStoreSetClass(ULONG handle, const char *class_name)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    Object object = {0};
    BOOL result = FALSE;
    if (profile && load(profile, handle, &object))
    {
        object.class_name = (char *)class_name;
        result = save(profile, handle, &object);
    }
    free(object.data);
    end(profile, lock);
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
    HINI profile = begin(&lock);
    BOOL result = FALSE;
    if (profile)
    {
        char app[16], key[9];
        snprintf(app, sizeof app, "state.%08x", (unsigned)handle);
        object_key(handle, key);
        PrfWriteProfileData(profile, app, NULL, NULL, 0);
        result = PrfWriteProfileData(profile, "objects", key, NULL, 0);
    }
    end(profile, lock);
    return result;
}

ULONG pmwpStoreFind(const char *identifier)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG result = profile ? find(profile, identifier, NULL, NULL) : 0;
    end(profile, lock);
    return result;
}

ULONG *pmwpStoreList(size_t *count)
{
    *count = 0;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG size = 0;
    char *list = profile ? keys(profile, &size) : NULL;
    end(profile, lock);
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
    HINI profile = begin(&lock);
    BOOL result = profile && ensure_identifier(profile, handle, identifier, capacity);
    end(profile, lock);
    return result;
}

BOOL pmwpStorePath(ULONG handle, char *output, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = profile && object_path(profile, handle, output, capacity, 0);
    end(profile, lock);
    return result;
}

BOOL pmwpStoreSetValue(ULONG handle, const char *class_name, ULONG key, const void *data,
                       ULONG size)
{
    char app[16], name[320];
    value_names(handle, class_name, key, app, name);
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = profile && PrfWriteProfileData(profile, app, name, (PVOID)data, size);
    end(profile, lock);
    return result;
}

BOOL pmwpStoreGetValue(ULONG handle, const char *class_name, ULONG key, void *data, ULONG *size)
{
    char app[16], name[320];
    value_names(handle, class_name, key, app, name);
    LxHostFile *lock;
    HINI profile = begin(&lock);
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
    end(profile, lock);
    return result;
}

BOOL pmwpStoreOpen(ULONG handle, ULONG view, BOOL activate, HWND *frame)
{
    return open_object(handle, view, activate, 0, frame);
}

BOOL pmwpSetupValue(const char *setup, const char *key, char *value, size_t capacity)
{
    return property(setup, key, value, capacity);
}

BOOL pmwpStoreClassModule(const char *class_name, char *module, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = profile && PrfQueryProfileString(profile, "classes", (PSZ)class_name, NULL,
                                                   module, (ULONG)capacity) != 0;
    end(profile, lock);
    return result;
}

BOOL pmwpStoreReplacement(const char *class_name, char *replacement, size_t capacity)
{
    LxHostFile *lock;
    HINI profile = begin(&lock);
    BOOL result = profile && PrfQueryProfileString(profile, "replacements", (PSZ)class_name, NULL,
                                                   replacement, (ULONG)capacity) != 0;
    end(profile, lock);
    return result;
}

BOOL pmwpStoreStartProgram(ULONG handle, const char *parameters)
{
    Object object = {0};
    if (!pmwpStoreLoad(handle, &object))
        return FALSE;
    char executable[4096], own[32768];
    BOOL result = FALSE;
    if (property(object.setup, "EXENAME", executable, sizeof executable))
    {
        own[0] = 0;
        if (parameters)
            snprintf(own, sizeof own, "%s", parameters);
        else
            property(object.setup, "PARAMETERS", own, sizeof own);
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
    return GLoaderState.makeUnixPath ? GLoaderState.makeUnixPath(guest_path, &error) : NULL;
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

/* '*' and '?' are the only wildcards in an association filter. */
static BOOL filter_matches(const char *filter, const char *name)
{
    for (; *filter; filter++, name++)
    {
        if (*filter == '*')
        {
            while (*filter == '*')
                filter++;
            if (!*filter)
                return TRUE;
            for (; *name; name++)
                if (filter_matches(filter, name))
                    return TRUE;
            return FALSE;
        }
        if (!*name ||
            (*filter != '?' && toupper((unsigned char)*filter) != toupper((unsigned char)*name)))
            return FALSE;
    }
    return !*name;
}

static BOOL filter_list_matches(char *filters, const char *name)
{
    char *state = NULL;
    for (char *item = strtok_r(filters, ",", &state); item; item = strtok_r(NULL, ",", &state))
        if (filter_matches(item, name))
            return TRUE;
    return FALSE;
}

BOOL lx_pmwp_open_associated(PSZ path)
{
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            name = p + 1;
    char executable[4096], parameters[32768], filters[1024];
    BOOL found = FALSE;
    LxHostFile *lock;
    HINI profile = begin(&lock);
    ULONG size = 0;
    char *list = profile ? keys(profile, &size) : NULL;
    for (char *key = list; key && key < list + size && *key && !found; key += strlen(key) + 1)
    {
        Object object = {0};
        if (!load(profile, (ULONG)strtoul(key, NULL, 16), &object))
            continue;
        if (!strcmp(object.class_name, "WPProgram") &&
            property(object.setup, "ASSOCFILTER", filters, sizeof filters) &&
            property(object.setup, "EXENAME", executable, sizeof executable) &&
            filter_list_matches(filters, name))
        {
            parameters[0] = 0;
            property(object.setup, "PARAMETERS", parameters, sizeof parameters);
            found = TRUE;
        }
        free(object.data);
    }
    free(list);
    end(profile, lock);
    if (!found)
        return FALSE;
    size_t bytes = strlen(parameters) + strlen(path) + 4;
    char *inputs = malloc(bytes);
    if (!inputs)
        return FALSE;
    snprintf(inputs, bytes, "%s%s\"%s\"", parameters, *parameters ? " " : "", path);
    STARTDATA start = {0};
    start.Length = sizeof start;
    start.PgmName = executable;
    start.PgmInputs = (PBYTE)inputs;
    ULONG session;
    PID pid;
    BOOL result = DosStartSession(&start, &session, &pid) == 0;
    free(inputs);
    return result;
}
