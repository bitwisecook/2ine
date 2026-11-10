/* Public domain. No rights reserved. Wholly AI-authored. */
/* Workplace object API: create, set, destroy, query, move, copy, shadow and
   save objects, window positions and file icons. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include "os2/wpsh/pmwp/internal.h"
#include "os2/include/bsedos.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include <ctype.h>
#include <strings.h>

enum
{
    CLASS_NAME_MAX = 255,
    TITLE_MAX = 4096,
    SETUP_MAX = 32767,
    OBJECT_ID_SIZE = 1024,
    OBJECT_PATH_SIZE = 4096,
    /* Largest output buffer WinQueryObjectPath accepts. */
    OBJECT_PATH_CAPACITY_MAX = 65536,
    /* Bounds class replacement chains and folder nesting, so a cycle in the store
       cannot loop forever. */
    REPLACEMENT_CHAIN_MAX = 256,
    FOLDER_DEPTH_MAX = 64,
    /* "SHADOWID=<object id>;" */
    SHADOW_SETUP_SIZE = 1100
};

/* The profile record WinStoreWindowPos writes: frame position and state, the frame's
   restore and minimize positions, and the screen metrics they were saved under. */
enum
{
    WINDOW_POS_SIGNATURE = 0x7b6a,
    WINDOW_POS_FLAGS = 2,
    WINDOW_POS_X = 6,
    WINDOW_POS_Y = 8,
    WINDOW_POS_CX = 10,
    WINDOW_POS_CY = 12,
    WINDOW_POS_HAS_RESTORE = 14,
    WINDOW_POS_RESTORE = 18,
    WINDOW_POS_HAS_MINIMIZE = 26,
    WINDOW_POS_X_MINIMIZE = 30,
    WINDOW_POS_Y_MINIMIZE = 32,
    WINDOW_POS_SCREEN_CX = 34,
    WINDOW_POS_SCREEN_CY = 38,
    WINDOW_POS_BORDER_CX = 42,
    WINDOW_POS_BORDER_CY = 46,
    WINDOW_POS_SIZE = 50,
    /* QWS_XRESTORE, QWS_YRESTORE, QWS_CXRESTORE and QWS_CYRESTORE. */
    WINDOW_POS_RESTORE_WORDS = 4
};

ULONG WinQueryObject(PSZ id)
{
    TRACE_NATIVE("WinQueryObject('%s')", id);
    if (!id || !*id)
        return 0;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG result = profile ? store_find(profile, id, NULL, NULL) : 0;
    store_end(profile, lock);
    return result;
}

ULONG WinCreateObject(PSZ class_name, PSZ title, PSZ setup, PSZ location, ULONG flags)
{
    TRACE_NATIVE("WinCreateObject('%s', '%s', '%s', '%s', %u)", class_name, title, setup, location,
                 (unsigned)flags);
    if (!class_name || !*class_name || strlen(class_name) > CLASS_NAME_MAX || !title || !*title ||
        strlen(title) > TITLE_MAX || !setup || strlen(setup) > SETUP_MAX || !location ||
        !*location || flags > CO_UPDATEIFEXISTS)
        return 0;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG result = 0;
    if (!profile)
        goto done;
    char resolved_class[CLASS_NAME_MAX + 1];
    snprintf(resolved_class, sizeof resolved_class, "%s", class_name);
    for (unsigned i = 0; i < REPLACEMENT_CHAIN_MAX; i++)
    {
        char next[CLASS_NAME_MAX + 1];
        if (!PrfQueryProfileString(profile, "replacements", resolved_class, NULL, next,
                                   sizeof next))
            break;
        snprintf(resolved_class, sizeof resolved_class, "%s", next);
        if (i == REPLACEMENT_CHAIN_MAX - 1)
            goto done;
    }
    class_name = resolved_class;
    if (strcmp(class_name, "WPFolder") && strcmp(class_name, "WPProgram") &&
        strcmp(class_name, "WPShadow"))
    {
        /* Any other class exists only as a guest SOM class in its registered module.
           Its methods run guest code, so the store is released first. */
        char module[CCHMAXPATH];
        if (!PrfQueryProfileString(profile, "classes", class_name, NULL, module, sizeof module))
            goto done;
        store_end(profile, lock);
        return pmwpCreateRegistered(class_name, module, title, setup, location, flags);
    }
    char shadow_title[TITLE_MAX + 1], shadow_target[TITLE_MAX + 1];
    if (!strcmp(class_name, "WPShadow"))
    {
        if (!store_property(setup, "SHADOWID", shadow_target, sizeof shadow_target))
            goto done;
        char *target = shadow_target;
        while (*target == ' ' || *target == '\t')
            target++;
        size_t length = strlen(target);
        while (length && (target[length - 1] == ' ' || target[length - 1] == '\t'))
            target[--length] = 0;
        if (!length)
            goto done;
        ULONG source = store_find(profile, target, NULL, NULL);
        PmwpRecord original = {0};
        if (source == DESKTOP_OBJECT)
            strcpy(shadow_title, "Desktop");
        else if (source && store_load(profile, source, &original))
        {
            snprintf(shadow_title, sizeof shadow_title, "%s", original.title);
            free(original.data);
        }
        else
        {
            uint32 error;
            char *host =
                GProcessState.makeUnixPath ? GProcessState.makeUnixPath(target, &error) : NULL;
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
    ULONG parent = store_find(profile, location, NULL, NULL);
    PmwpRecord container = {0};
    if (!parent || (parent != DESKTOP_OBJECT && (!store_load(profile, parent, &container) ||
                                                 strcmp(container.class_name, "WPFolder"))))
    {
        free(container.data);
        goto done;
    }
    free(container.data);
    char identifier[OBJECT_ID_SIZE];
    BOOL has_identifier = store_property(setup, "OBJECTID", identifier, sizeof identifier);
    ULONG existing = has_identifier ? store_find(profile, identifier, NULL, NULL) : 0;
    if (!existing && !has_identifier)
        existing = store_find(profile, NULL, location, title);
    if (existing == DESKTOP_OBJECT || (existing && flags == CO_FAILIFEXISTS))
        goto done;
    PmwpRecord object = {NULL, class_name, title, location, setup}, previous = {0};
    char *merged = NULL;
    if (existing && flags == CO_UPDATEIFEXISTS)
    {
        if (!store_load(profile, existing, &previous) || strcmp(previous.class_name, class_name))
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
    char configured_title[SETUP_MAX + 1];
    if (store_property(object.setup, "TITLE", configured_title, sizeof configured_title))
    {
        if (!*configured_title || strlen(configured_title) > TITLE_MAX)
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
    if (store_save(profile, handle, &object))
        result = handle;
free_previous:
    free(merged);
    free(previous.data);
done:
    store_end(profile, lock);
    return result;
}

BOOL WinSetObjectData(ULONG handle, PSZ setup)
{
    TRACE_NATIVE("WinSetObjectData(%u, '%s')", (unsigned)handle, setup);
    if (!setup || strlen(setup) > SETUP_MAX || handle < FIRST_OBJECT)
        return FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL result = FALSE;
    PmwpRecord object = {0};
    if (profile && store_load(profile, handle, &object))
    {
        char id[OBJECT_ID_SIZE];
        ULONG conflict = store_property(setup, "OBJECTID", id, sizeof id)
                             ? store_find(profile, id, NULL, NULL)
                             : 0;
        size_t size = strlen(object.setup) + strlen(setup) + 2;
        char *merged = malloc(size);
        if (merged && (!conflict || conflict == handle))
        {
            snprintf(merged, size, "%s;%s", object.setup, setup);
            object.setup = merged;
            char title[SETUP_MAX + 1];
            BOOL titled = store_property(merged, "TITLE", title, sizeof title);
            if (!titled || (*title && strlen(title) <= TITLE_MAX))
            {
                if (titled)
                    object.title = title;
                result = store_save(profile, handle, &object);
            }
        }
        free(merged);
    }
    free(object.data);
    store_end(profile, lock);
    return result;
}

BOOL WinDestroyObject(ULONG handle)
{
    TRACE_NATIVE("WinDestroyObject(%u)", (unsigned)handle);
    if (handle < FIRST_OBJECT)
        return FALSE;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    PmwpRecord object = {0};
    BOOL result = FALSE;
    if (profile && store_load(profile, handle, &object))
    {
        /* A nonempty folder cannot be deleted without an explicit child operation. */
        ULONG size;
        char *list = store_keys(profile, &size);
        char id[OBJECT_ID_SIZE];
        BOOL empty = TRUE;
        if (store_property(object.setup, "OBJECTID", id, sizeof id))
            for (char *key = list; key && key < list + size && *key; key += strlen(key) + 1)
            {
                PmwpRecord child = {0};
                if (store_load(profile, (ULONG)strtoul(key, NULL, 16), &child))
                    empty &= strcasecmp(child.location, id) != 0;
                free(child.data);
            }
        free(list);
        char key[9];
        store_object_key(handle, key);
        if (empty)
            result = PrfWriteProfileData(profile, "objects", key, NULL, 0);
    }
    free(object.data);
    store_end(profile, lock);
    return result;
}

BOOL WinIsSomDDReady(void)
{
    return FALSE;
}

APIRET WinRestartSomDD(BOOL state)
{
    return state ? ERROR_NOT_SUPPORTED : 0; /* There is no DSOM daemon to start. */
}

BOOL WinStoreWindowPos(PSZ app, PSZ key, HWND window)
{
    if (!app || !*app || !key || !*key)
        return FALSE;
    SWP p;
    if (!WinQueryWindowPos(window, &p))
        return FALSE;
    uint8 data[WINDOW_POS_SIZE] = {0};
    ULONG style = WinQueryWindowULong(window, QWL_STYLE);
    ULONG flags =
        (style & WS_MINIMIZED ? SWP_MINIMIZE : 0) | (style & WS_MAXIMIZED ? SWP_MAXIMIZE : 0);
    lx_guest_store_u16(data, WINDOW_POS_SIGNATURE);
    lx_guest_store_u32(data + WINDOW_POS_FLAGS, flags);
    lx_guest_store_u16(data + WINDOW_POS_X, p.x);
    lx_guest_store_u16(data + WINDOW_POS_Y, p.y);
    lx_guest_store_u16(data + WINDOW_POS_CX, p.cx);
    lx_guest_store_u16(data + WINDOW_POS_CY, p.cy);
    lx_guest_store_u32(data + WINDOW_POS_HAS_RESTORE, 1);
    for (unsigned i = 0; i < WINDOW_POS_RESTORE_WORDS; i++)
        lx_guest_store_u16(data + WINDOW_POS_RESTORE + i * 2,
                           WinQueryWindowUShort(window, QWS_XRESTORE + i * 2));
    lx_guest_store_u32(data + WINDOW_POS_HAS_MINIMIZE, 1);
    lx_guest_store_u16(data + WINDOW_POS_X_MINIMIZE, WinQueryWindowUShort(window, QWS_XMINIMIZE));
    lx_guest_store_u16(data + WINDOW_POS_Y_MINIMIZE, WinQueryWindowUShort(window, QWS_YMINIMIZE));
    lx_guest_store_u32(data + WINDOW_POS_SCREEN_CX, WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN));
    lx_guest_store_u32(data + WINDOW_POS_SCREEN_CY, WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN));
    lx_guest_store_u32(data + WINDOW_POS_BORDER_CX,
                       WinQuerySysValue(HWND_DESKTOP, SV_CXSIZEBORDER));
    lx_guest_store_u32(data + WINDOW_POS_BORDER_CY,
                       WinQuerySysValue(HWND_DESKTOP, SV_CYSIZEBORDER));
    return PrfWriteProfileData(HINI_USERPROFILE, app, key, data, sizeof data);
}

BOOL WinRestoreWindowPos(PSZ app, PSZ key, HWND window)
{
    if (!app || !*app || !key || !*key)
        return FALSE;
    uint8 data[WINDOW_POS_SIZE];
    ULONG size = sizeof data;
    if (!PrfQueryProfileData(HINI_USERPROFILE, app, key, data, &size) || size != sizeof data ||
        lx_guest_load_u16(data) != WINDOW_POS_SIGNATURE)
        return FALSE;
    LONG x = (SHORT)lx_guest_load_u16(data + WINDOW_POS_X),
         y = (SHORT)lx_guest_load_u16(data + WINDOW_POS_Y),
         cx = lx_guest_load_u16(data + WINDOW_POS_CX), cy = lx_guest_load_u16(data + WINDOW_POS_CY),
         width = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
         height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    /* Positions scale with the screen; sizes are kept. */
    ULONG old_width = lx_guest_load_u32(data + WINDOW_POS_SCREEN_CX),
          old_height = lx_guest_load_u32(data + WINDOW_POS_SCREEN_CY);
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
    if (lx_guest_load_u32(data + WINDOW_POS_HAS_RESTORE))
        for (unsigned i = 0; i < WINDOW_POS_RESTORE_WORDS; i++)
            WinSetWindowUShort(window, QWS_XRESTORE + i * 2,
                               lx_guest_load_u16(data + WINDOW_POS_RESTORE + i * 2));
    if (lx_guest_load_u32(data + WINDOW_POS_HAS_MINIMIZE))
    {
        WinSetWindowUShort(window, QWS_XMINIMIZE, lx_guest_load_u16(data + WINDOW_POS_X_MINIMIZE));
        WinSetWindowUShort(window, QWS_YMINIMIZE, lx_guest_load_u16(data + WINDOW_POS_Y_MINIMIZE));
    }
    ULONG flags = SWP_MOVE | SWP_SIZE | SWP_SHOW;
    ULONG state = lx_guest_load_u32(data + WINDOW_POS_FLAGS) & (SWP_MINIMIZE | SWP_MAXIMIZE);
    if (state)
        flags |= state;
    else
        flags |= SWP_RESTORE;
    return WinSetWindowPos(window, HWND_TOP, x, y, cx, cy, flags);
}

BOOL pmwp_object_path(HINI profile, ULONG handle, char *output, size_t capacity, unsigned depth)
{
    if (depth > FOLDER_DEPTH_MAX || !capacity)
        return FALSE;
    if (handle == DESKTOP_OBJECT)
    {
        char desktop[CCHMAXPATH];
        if (!PrfQueryProfileString(profile, "metadata", "desktopPath", NULL, desktop,
                                   sizeof desktop))
            return FALSE;
        if (strlen(desktop) >= capacity)
            return FALSE;
        strcpy(output, desktop);
        return TRUE;
    }
    PmwpRecord object = {0};
    if (!store_load(profile, handle, &object))
        return FALSE;
    BOOL ok = FALSE;
    if (store_property(object.setup, "PATH", output, capacity))
        ok = TRUE;
    else if (!strcmp(object.class_name, "WPFolder") || !strcmp(object.class_name, "WPDataFile") ||
             !strcmp(object.class_name, "WPFileSystem"))
    {
        char parent[OBJECT_PATH_SIZE];
        ULONG container = store_find(profile, object.location, NULL, NULL);
        if (container && pmwp_object_path(profile, container, parent, sizeof parent, depth + 1))
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
    if (!output || !capacity || capacity > OBJECT_PATH_CAPACITY_MAX)
        return FALSE;
    output[0] = 0;
    char path_buffer[OBJECT_PATH_SIZE];
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    BOOL ok = profile && pmwp_object_path(profile, handle, path_buffer, sizeof path_buffer, 0);
    if (ok)
    {
        size_t n = strlen(path_buffer);
        if (n >= capacity)
            ok = FALSE;
        else
            memcpy(output, path_buffer, n + 1);
    }
    store_end(profile, lock);
    return ok;
}

BOOL WinQueryActiveDesktopPathname(PSZ output, ULONG capacity)
{
    return WinQueryObjectPath(DESKTOP_OBJECT, output, capacity);
}

BOOL WinSaveObject(ULONG handle, BOOL async)
{
    (void)async;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    PmwpRecord object = {0};
    BOOL ok =
        profile && store_load(profile, handle, &object) && store_save(profile, handle, &object);
    free(object.data);
    store_end(profile, lock);
    return ok;
}

static BOOL destination_folder(HINI profile, ULONG handle)
{
    if (handle == DESKTOP_OBJECT)
        return TRUE;
    PmwpRecord d = {0};
    BOOL ok = store_load(profile, handle, &d) && !strcmp(d.class_name, "WPFolder");
    free(d.data);
    return ok;
}

ULONG WinMoveObject(ULONG handle, ULONG destination, ULONG reserved)
{
    if (reserved || handle == DESKTOP_OBJECT || handle == destination)
        return 0;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    PmwpRecord object = {0};
    ULONG result = 0;
    char id[OBJECT_ID_SIZE];
    if (profile && destination_folder(profile, destination) &&
        store_load(profile, handle, &object) &&
        store_ensure_identifier(profile, destination, id, sizeof id))
    {
        BOOL cycle = FALSE;
        ULONG at = destination;
        for (unsigned depth = 0; at && at != DESKTOP_OBJECT && depth < FOLDER_DEPTH_MAX; depth++)
        {
            if (at == handle)
            {
                cycle = TRUE;
                break;
            }
            PmwpRecord parent = {0};
            if (!store_load(profile, at, &parent))
                break;
            at = store_find(profile, parent.location, NULL, NULL);
            free(parent.data);
            /* A chain this deep is treated as a cycle. */
            if (depth == FOLDER_DEPTH_MAX - 1)
                cycle = TRUE;
        }
        if (!cycle)
        {
            object.location = id;
            if (store_save(profile, handle, &object))
                result = handle;
        }
    }
    free(object.data);
    store_end(profile, lock);
    return result;
}

static ULONG copy_object(HINI profile, ULONG handle, ULONG destination, unsigned depth)
{
    if (depth > FOLDER_DEPTH_MAX)
        return 0;
    PmwpRecord source = {0};
    char id[OBJECT_ID_SIZE];
    if (!store_load(profile, handle, &source) ||
        !store_ensure_identifier(profile, destination, id, sizeof id))
    {
        free(source.data);
        return 0;
    }
    char *setup = store_remove_identifier(source.setup);
    ULONG result = 0;
    if (!setup)
        goto done;
    ULONG copy = store_allocate_object(profile);
    if (!copy)
        goto done;
    PmwpRecord object = {NULL, source.class_name, source.title, id, setup};
    if (!store_save(profile, copy, &object))
        goto done;
    result = copy;
    if (!strcmp(source.class_name, "WPFolder"))
    {
        char source_id[OBJECT_ID_SIZE];
        if (store_property(source.setup, "OBJECTID", source_id, sizeof source_id))
        {
            ULONG size;
            char *list = store_keys(profile, &size);
            for (char *p = list; p && p < list + size && *p; p += strlen(p) + 1)
            {
                ULONG child = (ULONG)strtoul(p, NULL, 16);
                PmwpRecord original = {0};
                BOOL match = child != copy && store_load(profile, child, &original) &&
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
    for (unsigned i = 0; at && at != DESKTOP_OBJECT && i < FOLDER_DEPTH_MAX; i++)
    {
        if (at == object)
            return TRUE;
        PmwpRecord folder = {0};
        if (!store_load(profile, at, &folder))
            return TRUE;
        at = store_find(profile, folder.location, NULL, NULL);
        free(folder.data);
        if (i == FOLDER_DEPTH_MAX - 1)
            return TRUE;
    }
    return FALSE;
}

ULONG WinCopyObject(ULONG handle, ULONG destination, ULONG reserved)
{
    if (reserved || handle == DESKTOP_OBJECT || handle == destination)
        return 0;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG result = profile && destination_folder(profile, destination) &&
                           !contains_folder(profile, handle, destination)
                       ? copy_object(profile, handle, destination, 0)
                       : 0;
    store_end(profile, lock);
    return result;
}

ULONG WinCreateShadow(ULONG handle, ULONG destination, ULONG reserved)
{
    if (reserved)
        return 0;
    LxHostFile *lock;
    HINI profile = store_begin(&lock);
    ULONG result = 0;
    PmwpRecord source = {0};
    char source_id[OBJECT_ID_SIZE], destination_id[OBJECT_ID_SIZE];
    if (profile && destination_folder(profile, destination) &&
        (handle == DESKTOP_OBJECT || store_load(profile, handle, &source)) &&
        store_ensure_identifier(profile, handle, source_id, sizeof source_id) &&
        store_ensure_identifier(profile, destination, destination_id, sizeof destination_id))
    {
        char setup[SHADOW_SETUP_SIZE];
        snprintf(setup, sizeof setup, "SHADOWID=%s;", source_id);
        PmwpRecord shadow = {NULL, "WPShadow", handle == DESKTOP_OBJECT ? "Desktop" : source.title,
                             destination_id, setup};
        ULONG copy = store_allocate_object(profile);
        if (copy && store_save(profile, copy, &shadow))
            result = copy;
    }
    free(source.data);
    store_end(profile, lock);
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
