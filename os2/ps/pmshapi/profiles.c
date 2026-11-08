/* Public domain. No rights reserved. Wholly AI-authored. */
/* Case-sensitive profile entries, merged system queries and atomic persistence. */
#include "os2/include/private/export.h"
#include "os2/include/pmerr.h"
#include "os2/include/pmshl.h"
#include "os2/ps/pmshapi/profiles.h"
#include "os2/include/bsedos.h"
#include "emulator/handles.h"
#include "emulator/abi.h"
#include "emulator/process.h"
#include "host/fs.h"
#include "host/sync.h"
#include <limits.h>

typedef struct ProfileEntry
{
    char *app, *key;
    uint8 *data;
    ULONG size;
    struct ProfileEntry *next;
} ProfileEntry;

typedef struct Profile
{
    char *path;
    size_t references;
    ProfileEntry *entries;
    uint8 *snapshot;
    size_t snapshot_size;
    LxHostFileIdentity identity;
    int identity_known;
    struct Profile *next;
} Profile;

typedef struct ProfileHandle
{
    Profile *profile;
    HINI handle;
    struct ProfileHandle *next;
} ProfileHandle;

static ProfileHandle *profile_handles;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *mutex;
static Profile *profiles, *user_profile, *system_profile;
static const uint8 signature[8] = {'2', 'I', 'N', 'E', 'P', 'R', 'F', '1'};
#define PROFILE_LIMIT (32u * 1024u * 1024u)

static void free_entries(ProfileEntry *entry)
{
    while (entry)
    {
        ProfileEntry *next = entry->next;
        free(entry->app);
        free(entry->key);
        free(entry->data);
        free(entry);
        entry = next;
    }
}

static ProfileEntry *entry_new(const char *app, const char *key, const void *data, ULONG size)
{
    ProfileEntry *e = calloc(1, sizeof *e);
    if (!e)
        return NULL;
    e->app = strdup(app);
    e->key = strdup(key);
    e->data = malloc(size ? size : 1);
    if (!e->app || !e->key || !e->data)
    {
        free_entries(e);
        return NULL;
    }
    if (size)
        memcpy(e->data, data, size);
    e->size = size;
    return e;
}

static int native_string(const uint8 *file, size_t size, uint32 offset, uint32 length)
{
    return length && offset <= size && length <= size - offset && !file[offset + length - 1] &&
           !memchr(file + offset, 0, length - 1);
}

static int native_mark(uint8 *seen, size_t size, uint32 offset, uint32 extent)
{
    if (offset < 20 || offset > size || extent > size - offset ||
        (seen[offset / 8] & (1u << (offset % 8))))
        return 0;
    seen[offset / 8] |= 1u << (offset % 8);
    return 1;
}

static int native_load(Profile *profile, const uint8 *file, size_t size)
{
    if (size < 20 || lx_guest_load_u32(file + 8) > size)
        return 0;
    uint8 *seen = calloc((size + 7) / 8, 1);
    if (!seen)
        return 0;
    ProfileEntry **tail = &profile->entries;
    uint32 app = lx_guest_load_u32(file + 4);
    int ok = 1;
    while (app && ok)
    {
        if (!native_mark(seen, size, app, 20))
        {
            ok = 0;
            break;
        }
        uint32 name = lx_guest_load_u32(file + app + 16);
        uint32 length = lx_guest_load_u16(file + app + 12);
        if (!native_string(file, size, name, length))
        {
            ok = 0;
            break;
        }
        uint32 key = lx_guest_load_u32(file + app + 4);
        while (key && ok)
        {
            if (!native_mark(seen, size, key, 24))
            {
                ok = 0;
                break;
            }
            uint32 keyname = lx_guest_load_u32(file + key + 12),
                   data = lx_guest_load_u32(file + key + 20),
                   bytes = lx_guest_load_u16(file + key + 16);
            if (!native_string(file, size, keyname, lx_guest_load_u16(file + key + 8)) ||
                data > size || bytes > size - data)
            {
                ok = 0;
                break;
            }
            *tail = entry_new((const char *)file + name, (const char *)file + keyname, file + data,
                              bytes);
            if (!*tail)
            {
                ok = 0;
                break;
            }
            tail = &(*tail)->next;
            key = lx_guest_load_u32(file + key);
        }
        app = lx_guest_load_u32(file + app);
    }
    free(seen);
    return ok;
}

/* Resolve an existing file or its existing parent so aliases share one snapshot. */
static char *profile_path(const char *path)
{
    char *resolved = lx_host_path_canonical(path);
    if (resolved)
        return resolved;
    const char *slash = strrchr(path, '/');
    char *parent = slash ? strndup(path, slash == path ? 1 : (size_t)(slash - path)) : strdup(".");
    if (!parent)
        return NULL;
    char *directory = lx_host_path_canonical(parent);
    free(parent);
    if (!directory)
        return strdup(path);
    const char *leaf = slash ? slash + 1 : path;
    size_t size = strlen(directory) + strlen(leaf) + 2;
    resolved = malloc(size);
    if (resolved)
        snprintf(resolved, size, "%s/%s", directory, leaf);
    free(directory);
    return resolved;
}

static int same_identity(const LxHostFileIdentity *a, const LxHostFileIdentity *b)
{
    return a->device == b->device && a->file == b->file && a->size == b->size &&
           a->modified == b->modified && a->changed == b->changed &&
           a->modified_nanoseconds == b->modified_nanoseconds &&
           a->changed_nanoseconds == b->changed_nanoseconds;
}

/* Associate a snapshot only with an identity stable across its read. */
static void profile_identity(Profile *p, const LxHostFileIdentity *before, int known)
{
    LxHostFileIdentity after;
    p->identity_known = known && lx_host_path_identity(p->path, &after) == LX_HOST_FS_OK &&
                        same_identity(before, &after);
    if (p->identity_known)
        p->identity = after;
}

static Profile *profile_load(const char *path)
{
    Profile *p = calloc(1, sizeof *p);
    if (!p || !(p->path = profile_path(path)))
    {
        free(p);
        return NULL;
    }
    LxHostFileIdentity before;
    int known = lx_host_path_identity(p->path, &before) == LX_HOST_FS_OK;
    size_t size = 0;
    LxHostFsError error;
    uint8 *file = lx_host_file_snapshot(path, PROFILE_LIMIT, &size, &error);
    if (!file)
    {
        if (error == LX_HOST_FS_MISSING)
            return p;
        goto failed;
    }
    if (size >= 20 && lx_guest_load_u32(file) == UINT32_MAX)
    {
        if (!native_load(p, file, size))
            goto failed;
        p->snapshot = file;
        p->snapshot_size = size;
        profile_identity(p, &before, known);
        return p;
    }
    if (size < 12 || memcmp(file, signature, 8))
        goto failed;
    uint32 count = lx_guest_load_u32(file + 8);
    size_t at = 12;
    ProfileEntry **tail = &p->entries;
    while (count--)
    {
        if (size - at < 12)
            goto failed;
        uint32 a = lx_guest_load_u32(file + at), k = lx_guest_load_u32(file + at + 4),
               n = lx_guest_load_u32(file + at + 8);
        at += 12;
        if (!a || !k || a > 65536 || k > 65536 || a > size - at || k > size - at - a ||
            n > size - at - a - k || file[at + a - 1] || file[at + a + k - 1] ||
            memchr(file + at, 0, a - 1) || memchr(file + at + a, 0, k - 1))
            goto failed;
        *tail = entry_new((char *)file + at, (char *)file + at + a, file + at + a + k, n);
        if (!*tail)
            goto failed;
        tail = &(*tail)->next;
        at += a + k + n;
    }
    if (at != size)
        goto failed;
    p->snapshot = file;
    p->snapshot_size = size;
    profile_identity(p, &before, known);
    return p;
failed:
    free(file);
    free_entries(p->entries);
    free(p->snapshot);
    free(p->path);
    free(p);
    return NULL;
}

/* Atomic replacement gives readers a complete version without taking a write lock.
   Compare bytes rather than timestamps: two equal-sized commits can share a host
   timestamp. The retained snapshot avoids reparsing unchanged binary profiles. */
static int profile_refresh(Profile *p, int force)
{
    if (!p)
        return 0;
    LxHostFileIdentity before;
    int known = lx_host_path_identity(p->path, &before) == LX_HOST_FS_OK;
    if (!force && known && p->identity_known && same_identity(&p->identity, &before))
        return 1;
    if (!force)
    {
        size_t size = 0;
        LxHostFsError error;
        uint8 *file = lx_host_file_snapshot(p->path, PROFILE_LIMIT, &size, &error);
        int unchanged =
            file ? p->snapshot && size == p->snapshot_size && !memcmp(file, p->snapshot, size)
                 : error == LX_HOST_FS_MISSING && !p->snapshot && !p->entries;
        free(file);
        if (unchanged)
        {
            profile_identity(p, &before, known);
            return 1;
        }
    }
    Profile *fresh = profile_load(p->path);
    if (!fresh)
        return 0;
    free_entries(p->entries);
    free(p->snapshot);
    p->entries = fresh->entries;
    p->snapshot = fresh->snapshot;
    p->snapshot_size = fresh->snapshot_size;
    p->identity = fresh->identity;
    p->identity_known = fresh->identity_known;
    free(fresh->path);
    free(fresh);
    return 1;
}

/* Use a stable inode distinct from the atomically replaced INI and WPS's outer
   .lock. Closing this descriptor releases the transaction, including on death. */
static LxHostFile *profile_lock(Profile *p)
{
    size_t length = strlen(p->path) + sizeof ".prf-lock";
    char *path = malloc(length);
    if (!path)
        return NULL;
    snprintf(path, length, "%s.prf-lock", p->path);
    LxHostFile *lock = lx_host_file_open(
        path, LX_HOST_FILE_READ | LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_NOINHERIT,
        0, NULL);
    free(path);
    if (lock && lx_host_file_lock(lock, 1) != LX_HOST_FS_OK)
    {
        lx_host_file_close(lock);
        lock = NULL;
    }
    return lock;
}

static int write_all(LxHostFile *file, const void *data, size_t size)
{
    size_t written = 0;
    return !lx_host_file_write(file, data, size, &written) && written == size;
}

static int profile_save(Profile *p, ProfileEntry *entries)
{
    size_t total = 12;
    uint32 count = 0;
    for (ProfileEntry *e = entries; e; e = e->next)
    {
        total += 12 + strlen(e->app) + 1 + strlen(e->key) + 1 + e->size;
        if (total > PROFILE_LIMIT)
            return 0;
        count++;
    }
    char *temporary = malloc(strlen(p->path) + 40);
    if (!temporary)
        return 0;
    snprintf(temporary, strlen(p->path) + 40, "%s.tmp-%p", p->path, (void *)p);
    /* Only the sidecar lock owner publishes; discard a previous interrupted save. */
    lx_host_path_remove(temporary, 0);
    LxHostFile *file = lx_host_file_open(
        temporary, LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE, 0, NULL);
    if (!file)
    {
        free(temporary);
        return 0;
    }
    uint8 header[12];
    memcpy(header, signature, 8);
    lx_guest_store_u32(header + 8, count);
    int ok = write_all(file, header, sizeof header);
    for (ProfileEntry *e = entries; ok && e; e = e->next)
    {
        uint32 a = (uint32)strlen(e->app) + 1, k = (uint32)strlen(e->key) + 1;
        lx_guest_store_u32(header, a);
        lx_guest_store_u32(header + 4, k);
        lx_guest_store_u32(header + 8, e->size);
        ok = write_all(file, header, sizeof header) && write_all(file, e->app, a) &&
             write_all(file, e->key, k) && write_all(file, e->data, e->size);
    }
    if (ok)
        ok = lx_host_file_flush(file) == LX_HOST_FS_OK;
    lx_host_file_close(file);
    if (ok)
        ok = lx_host_path_move(temporary, p->path) == LX_HOST_FS_OK;
    if (!ok)
        lx_host_path_remove(temporary, 0);
    free(temporary);
    return ok;
}

static void initialize(void)
{
    mutex = lx_host_monitor_create();
    if (!mutex)
        abort();
    const char *u = getenv("TWOINE_USER_PROFILE"), *s = getenv("TWOINE_SYSTEM_PROFILE");
    user_profile = profile_load(u && *u ? u : ".2ine-user.profile");
    system_profile = profile_load(s && *s ? s : ".2ine-system.profile");
}

static Profile *profile_get(HINI handle, LxHandleBorrow *borrow)
{
    lx_host_once(&once, initialize);
    if (!handle || handle == HINI_USERPROFILE)
        return user_profile;
    if (handle == HINI_SYSTEMPROFILE)
        return system_profile;
    return lx_handle_borrow(handle, LX_HANDLE_PROFILE, borrow)
               ? ((ProfileHandle *)borrow->resource)->profile
               : NULL;
}

static void profile_destroy(void *value)
{
    ProfileHandle *handle = value;
    Profile *p = handle->profile;
    lx_host_monitor_lock(mutex);
    ProfileHandle **link = &profile_handles;
    while (*link && *link != handle)
        link = &(*link)->next;
    if (*link)
        *link = handle->next;
    free(handle);
    if (--p->references || p == user_profile || p == system_profile)
    {
        lx_host_monitor_unlock(mutex);
        return;
    }
    Profile **slot = &profiles;
    while (*slot && *slot != p)
        slot = &(*slot)->next;
    if (*slot)
        *slot = p->next;
    lx_host_monitor_unlock(mutex);
    free_entries(p->entries);
    free(p->snapshot);
    free(p->path);
    free(p);
}

HINI PrfOpenProfile(HANDLE hab, PSZ name)
{
    TRACE_NATIVE("PrfOpenProfile(%u, '%s')", (uint)hab, name);
    if (!name || !*name || !GProcessState.makeUnixPath)
        return 0;
    uint32 error = 0;
    char *path = GProcessState.makeUnixPath(name, &error);
    HINI result = path ? lx_profile_open_host(path) : 0;
    free(path);
    return result;
}

HINI lx_profile_open_host(const char *name)
{
    lx_host_once(&once, initialize);
    char *path = profile_path(name);
    if (!path)
        return 0;
    lx_host_monitor_lock(mutex);
    Profile *p = user_profile && !strcmp(user_profile->path, path)       ? user_profile
                 : system_profile && !strcmp(system_profile->path, path) ? system_profile
                                                                         : profiles;
    for (; p && strcmp(p->path, path); p = p->next)
    {
    }
    if (!p)
    {
        p = profile_load(path);
        if (p)
        {
            p->next = profiles;
            profiles = p;
        }
    }
    ProfileHandle *handle = p ? malloc(sizeof *handle) : NULL;
    HINI result = 0;
    if (handle)
    {
        handle->profile = p;
        result = lx_handle_create(LX_HANDLE_PROFILE, handle, profile_destroy);
        if (result)
        {
            p->references++;
            handle->handle = result;
            handle->next = profile_handles;
            profile_handles = handle;
        }
        else
            free(handle);
    }
    if (p && !p->references && p != user_profile && p != system_profile)
    {
        Profile **slot = &profiles;
        while (*slot != p)
            slot = &(*slot)->next;
        *slot = p->next;
        free_entries(p->entries);
        free(p->snapshot);
        free(p->path);
        free(p);
    }
    lx_host_monitor_unlock(mutex);
    free(path);
    return result;
}

BOOL PrfCloseProfile(HINI profile)
{
    return profile && profile != HINI_USERPROFILE && profile != HINI_SYSTEMPROFILE &&
           lx_handle_close(profile, LX_HANDLE_PROFILE);
}

BOOL PrfQueryProfile(HANDLE hab, PPRFPROFILE output)
{
    TRACE_NATIVE("PrfQueryProfile(%u, %p)", (uint)hab, output);
    if (!output)
        return FALSE;
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(mutex);
    char *user = user_profile ? lxHostPathToGuest(user_profile->path) : NULL;
    char *system = system_profile ? lxHostPathToGuest(system_profile->path) : NULL;
    lx_host_monitor_unlock(mutex);
    if (!user || !system)
    {
        free(user);
        free(system);
        return FALSE;
    }
    ULONG user_capacity = output->cchUserName, system_capacity = output->cchSysName;
    output->cchUserName = (ULONG)strlen(user) + 1;
    output->cchSysName = (ULONG)strlen(system) + 1;
    BOOL result = output->pszUserName && output->pszSysName &&
                  user_capacity >= output->cchUserName && system_capacity >= output->cchSysName;
    if (result)
    {
        memcpy(output->pszUserName, user, output->cchUserName);
        memcpy(output->pszSysName, system, output->cchSysName);
    }
    free(user);
    free(system);
    return result;
}

static ProfileEntry *find_entry(Profile *p, const char *app, const char *key)
{
    for (ProfileEntry *e = p ? p->entries : NULL; e; e = e->next)
        if (!strcmp(e->app, app) && !strcmp(e->key, key))
            return e;
    return NULL;
}

typedef struct PrinterProfileDefault
{
    const char *app, *key, *value;
} PrinterProfileDefault;

static const PrinterProfileDefault printer_profile_defaults[] = {
    {"PM_SPOOLER", "QUEUE", "2INEPDF;"},
    {"PM_SPOOLER", "PRINTER", "2INEPDF;"},
    {"PM_SPOOLER_QUEUE_DD", "2INEPDF", "2INEPDF.PDF"},
    {"PM_SPOOLER_QUEUE_DD", "2INEPCL", "2INEPCL.PCL"},
    {"PM_SPOOLER_QUEUE_DESCR", "2INEPDF", "Save as PDF"},
    {"PM_SPOOLER_QUEUE_DESCR", "2INEPCL", "PCL 5 Printer"},
    {"PM_SPOOLER_PRINTER", "2INEPDF", "FILE;2INEPDF.PDF;2INEPDF;"},
    {"PM_SPOOLER_PRINTER", "2INEPCL", "FILE;2INEPCL.PCL;2INEPCL;"}};

enum
{
    NATIONAL_DEFAULT_COUNT = 11
};

static BOOL national_profile_defaults(ProfileEntry *entries, char text[NATIONAL_DEFAULT_COUNT][16])
{
    COUNTRYCODE code = {0};
    COUNTRYINFO info;
    if (DosQueryCtryInfo(sizeof info, &code, &info, NULL))
        return FALSE;
    static const char *keys[NATIONAL_DEFAULT_COUNT] = {
        "iCountry",  "iDate",    "iCurrency", "iDigits", "iTime", "sCurrency",
        "sThousand", "sDecimal", "sDate",     "sTime",   "sList"};
    snprintf(text[0], 16, "%u", info.country);
    snprintf(text[1], 16, "%u", info.fsDateFmt);
    snprintf(text[2], 16, "%u", info.fsCurrencyFmt & 3u);
    snprintf(text[3], 16, "%u", info.cDecimalPlace);
    snprintf(text[4], 16, "%u", info.fsTimeFmt & 1u);
    snprintf(text[5], 16, "%.3s", info.szCurrency);
    snprintf(text[6], 16, "%.1s", info.szThousandsSeparator);
    snprintf(text[7], 16, "%.1s", info.szDecimal);
    snprintf(text[8], 16, "%.1s", info.szDateSeparator);
    snprintf(text[9], 16, "%.1s", info.szTimeSeparator);
    snprintf(text[10], 16, "%.1s", info.szDataSeparator);
    for (unsigned i = 0; i < NATIONAL_DEFAULT_COUNT; i++)
        entries[i] =
            (ProfileEntry){.app = "PM_National",
                           .key = (char *)keys[i],
                           .data = (uint8 *)text[i],
                           .size = (ULONG)strlen(text[i]) + 1,
                           .next = i + 1 < NATIONAL_DEFAULT_COUNT ? &entries[i + 1] : NULL};
    return TRUE;
}

/* The caller owns the returned snapshot, including a byte beyond binary data. */
static uint8 *query(HINI handle, const char *app, const char *key, ULONG *size)
{
    LxHandleBorrow borrow = {0};
    Profile *p = profile_get(handle, &borrow);
    uint8 *data = NULL;
    *size = 0;
    lx_host_monitor_lock(mutex);
    int current = profile_refresh(p, 0);
    Profile *visible_user = user_profile, *visible_system = system_profile;
    if (current &&
        (handle == HINI_PROFILE || handle == HINI_USERPROFILE || handle == HINI_SYSTEMPROFILE))
    {
        if (p != user_profile && !profile_refresh(user_profile, 0))
            visible_user = NULL;
        if (p != system_profile && !profile_refresh(system_profile, 0))
            visible_system = NULL;
    }
    if (!current)
    {
        lx_host_monitor_unlock(mutex);
        lx_handle_release(&borrow);
        lxHostThreadState()->pm_error = 0x1115; /* PMERR_INVALID_INI_FILE_HANDLE */
        return NULL;
    }
    /* Reserved profiles expose native national settings and printer services.
       Virtual service defaults remain reads,
       never stored INI entries, and explicit/private profile handles do not
       gain them even when they refer to the same backing file. */
    ProfileEntry defaults[sizeof printer_profile_defaults / sizeof printer_profile_defaults[0]];
    ProfileEntry national_defaults[NATIONAL_DEFAULT_COUNT];
    char national_text[NATIONAL_DEFAULT_COUNT][16];
    Profile printer_profile = {0};
    if (p && (handle == HINI_PROFILE || handle == HINI_USERPROFILE || handle == HINI_SYSTEMPROFILE))
    {
        ProfileEntry *preferred = find_entry(visible_user, "PM_SPOOLER", "QUEUE");
        if (!preferred)
            preferred = find_entry(visible_system, "PM_SPOOLER", "QUEUE");
        for (unsigned i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        {
            const PrinterProfileDefault *value = &printer_profile_defaults[i];
            defaults[i] = (ProfileEntry){
                .app = (char *)value->app,
                .key = (char *)value->key,
                .data = (uint8 *)value->value,
                .size = (ULONG)strlen(value->value) + 1,
                .next = i + 1 < sizeof defaults / sizeof defaults[0] ? &defaults[i + 1] : NULL};
            if (preferred && !strcmp(value->app, "PM_SPOOLER"))
            {
                defaults[i].data = preferred->data;
                defaults[i].size = preferred->size;
            }
        }
        printer_profile.entries = defaults;
        if (national_profile_defaults(national_defaults, national_text))
            defaults[sizeof defaults / sizeof *defaults - 1].next = national_defaults;
    }
    ProfileEntry *entry = app && key ? find_entry(p, app, key) : NULL;
    if (!entry && !handle && app && key)
        entry = find_entry(visible_system, app, key);
    if (!entry && app && key)
        entry = find_entry(&printer_profile, app, key);
    if (entry)
    {
        data = calloc((size_t)entry->size + 1, 1);
        if (data)
        {
            memcpy(data, entry->data, entry->size);
            *size = entry->size;
        }
    }
    else if (p && (!app || !key))
    {
        size_t used = 0;
        data = calloc(1, 1);
        Profile *sources[3] = {p, !handle ? visible_system : NULL, &printer_profile};
        for (unsigned i = 0; data && i < 3; i++)
            for (ProfileEntry *e = sources[i] ? sources[i]->entries : NULL; e; e = e->next)
            {
                if (app && strcmp(app, e->app))
                    continue;
                const char *name = app ? e->key : e->app;
                size_t n = strlen(name) + 1, offset = 0;
                while (offset < used && strcmp((char *)data + offset, name))
                    offset += strlen((char *)data + offset) + 1;
                if (offset < used)
                    continue;
                uint8 *next = realloc(data, used + n + 1);
                if (!next)
                {
                    free(data);
                    data = NULL;
                    break;
                }
                data = next;
                memcpy(data + used, name, n);
                used += n;
                data[used] = 0;
            }
        if (data)
            *size = (ULONG)used + 1;
    }
    lx_host_monitor_unlock(mutex);
    lx_handle_release(&borrow);
    if (!data)
        lxHostThreadState()->pm_error = !p      ? 0x1115  /* PMERR_INVALID_INI_FILE_HANDLE */
                                        : entry ? 0x1309  /* PMERR_MEMORY_ALLOC */
                                                : 0x1304; /* PMERR_NOT_IN_IDX */
    return data;
}

BOOL PrfQueryProfileSize(HINI profile, PSZ app, PSZ key, PULONG size)
{
    if (!size)
        return FALSE;
    uint8 *data = query(profile, app, key, size);
    int ok = data != NULL;
    free(data);
    return ok;
}

BOOL PrfQueryProfileData(HINI profile, PSZ app, PSZ key, PVOID output, PULONG size)
{
    if (!size || (!output && *size))
        return FALSE;
    ULONG actual;
    uint8 *data = query(profile, app, key, &actual);
    if (!data)
        return FALSE;
    ULONG copied = *size < actual ? *size : actual;
    if (copied)
        memcpy(output, data, copied);
    *size = actual;
    free(data);
    return copied == actual;
}

ULONG PrfQueryProfileString(HINI profile, PSZ app, PSZ key, PSZ fallback, PVOID output,
                            ULONG capacity)
{
    TRACE_NATIVE("PrfQueryProfileString(%u, '%s', '%s', '%s', %p, %u)", (uint)profile, app, key,
                 fallback, output, (uint)capacity);
    if (!output || !capacity)
        return 0;
    ULONG size;
    uint8 *data = query(profile, app, key, &size);
    if (!data && app && key && fallback)
    {
        data = (uint8 *)strdup(fallback);
        size = (ULONG)strlen(fallback) + 1;
    }
    ULONG count = 0;
    if (data)
    {
        if (app && key)
        {
            count = (ULONG)strlen((char *)data);
            if (count >= capacity)
                count = capacity - 1;
            memcpy(output, data, count);
            ((uint8 *)output)[count] = 0;
        }
        else
        {
            count = size < capacity ? size : capacity;
            memcpy(output, data, count);
            if (count == size && count)
                count--;
        }
    }
    free(data);
    return count;
}

LONG PrfQueryProfileInt(HINI profile, PSZ app, PSZ key, LONG fallback)
{
    TRACE_NATIVE("PrfQueryProfileInt(%u, '%s', '%s', %d)", (uint)profile, app, key, (int)fallback);
    if (!app || !key)
        return fallback;
    ULONG size;
    uint8 *data = query(profile, app, key, &size);
    if (!data)
        return fallback;
    char *end;
    errno = 0;
    long long value = strtoll((char *)data, &end, 10);
    LONG result = errno || end == (char *)data || *end || value < INT32_MIN || value > INT32_MAX
                      ? fallback
                      : (LONG)value;
    free(data);
    return result;
}

BOOL PrfWriteProfileData(HINI handle, PSZ app, PSZ key, PVOID data, ULONG size)
{
    if ((!app && (key || data || size)) || (app && (!*app || strlen(app) >= 65536)) ||
        (key && strlen(key) >= 65536) || size > PROFILE_LIMIT || (data && !key))
        return FALSE;
    LxHandleBorrow borrow = {0};
    Profile *p = profile_get(handle, &borrow);
    if (!p)
        return FALSE;
    lx_host_monitor_lock(mutex);
    LxHostFile *transaction = profile_lock(p);
    ProfileEntry *copy = NULL, **tail = &copy;
    int ok = transaction && profile_refresh(p, 1);
    for (ProfileEntry *e = ok ? p->entries : NULL; e; e = e->next)
    {
        if (!app || (!strcmp(e->app, app) && (!key || !strcmp(e->key, key))))
            continue;
        *tail = entry_new(e->app, e->key, e->data, e->size);
        if (!*tail)
        {
            ok = 0;
            break;
        }
        tail = &(*tail)->next;
    }
    if (ok && data)
    {
        *tail = entry_new(app, key, data, size);
        ok = *tail != NULL;
    }
    if (ok)
        ok = profile_save(p, copy);
    if (ok)
    {
        free_entries(p->entries);
        p->entries = copy;
        free(p->snapshot);
        p->snapshot = NULL;
        p->snapshot_size = 0;
        p->identity_known = 0;
    }
    else
        free_entries(copy);
    lx_host_file_close(transaction);
    lx_host_monitor_unlock(mutex);
    lx_handle_release(&borrow);
    return ok;
}

BOOL PrfWriteProfileString(HINI profile, PSZ app, PSZ key, PSZ data)
{
    return PrfWriteProfileData(profile, app, key, data, data ? (ULONG)strlen(data) + 1 : 0);
}

LX_NATIVE_DESTRUCTOR(pmshapi_profiles)
{
    /* No API calls may remain active when their native provider is unloaded. */
    while (profile_handles)
    {
        ProfileHandle *handle = profile_handles;
        profile_handles = handle->next;
        lx_handle_close(handle->handle, LX_HANDLE_PROFILE);
    }
    Profile *builtins[2] = {user_profile, system_profile};
    for (unsigned i = 0; i < 2; i++)
        if (builtins[i])
        {
            free_entries(builtins[i]->entries);
            free(builtins[i]->snapshot);
            free(builtins[i]->path);
            free(builtins[i]);
        }
    if (mutex)
        lx_host_monitor_destroy(mutex);
}
