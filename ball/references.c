/* Public domain. No rights reserved. Wholly AI-authored. */
/* References: the files and folders outside the ball that a ball uses. Every
   drive image, drive folder and remembered installation medium has one
   record in the manifest's "references" object, beside the version 1 drive
   fields it mirrors. A record holds the path relative to its base, identity
   hints captured when it was attached, opaque per-host access data and the
   read-only flag. Resolution reports a state and never moves a reference;
   only relink, bundle and unbundle change where one points. */
#include "ball/ball_internal.h"
#include "storage/image_path.h"
#include "storage/volume.h"
#include "ball/vendor/sha256/sha256.h"

#define REFERENCE_DRIVE_PREFIX "drive-"
#define REFERENCE_MEDIA_PREFIX "media-"
#define REFERENCE_BUNDLE_DIRECTORY "bundled-media"
/* Relocation search looks at most this deep below each search root. */
#define REFERENCE_SEARCH_DEPTH 4
#define REFERENCE_SEARCH_ENTRIES 20000u

static const char *leaf_name(const char *path)
{
    const char *leaf = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    return leaf;
}

/* The host path a stored drive or media path names. */
static char *stored_host_path(TbBall *c, const char *path)
{
    return host_absolute(path) ? tb_ball_keep(c, strdup(path)) : tb_ball_join(c, c->ball, path);
}

/* Paths inside the ball are kept relative to it, so they move with the ball. */
static void reference_location(TbBall *c, cJSON *record, const char *path)
{
    const char *relative = NULL;
    if (!host_absolute(path))
        relative = path;
    else if (native_host_path(path) && tb_ball_inside(path, c->ball))
    {
        relative = path + strlen(c->ball);
        while (*relative == '/' || *relative == '\\')
            relative++;
    }
    put(record, "path", cJSON_CreateString(relative ? relative : path));
    put(record, "base", cJSON_CreateString(relative ? "ball" : "absolute"));
}

static char *reference_host_path(TbBall *c, const cJSON *record)
{
    const char *path = field(record, "path"), *base = field(record, "base");
    if (!path)
        return NULL;
    return base && !strcmp(base, "ball") ? tb_ball_join(c, c->ball, path)
                                         : tb_ball_keep(c, strdup(path));
}

static int digest(const char *path, unsigned char hash[32])
{
    FILE *file = tb_fopen(path, "rb");
    if (!file)
        return 0;
    SHA256_CTX state;
    sha256_init(&state);
    unsigned char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, file)))
        sha256_update(&state, buffer, n);
    int ok = !ferror(file);
    fclose(file);
    if (ok)
        sha256_final(&state, hash);
    return ok;
}

static char *volume_label(TbBall *c, const char *image)
{
    char *root = tb_image_root(image);
    TbImageInfo info;
    int known = root && tb_image_info(root, &info);
    free(root);
    if (!known)
        return NULL;
    size_t n = strnlen(info.label, sizeof info.label);
    while (n && info.label[n - 1] == ' ')
        n--;
    return n ? tb_ball_format(c, "%.*s", (int)n, info.label) : NULL;
}

/* Identity hints: enough to notice a different file at the same path. */
static cJSON *reference_hint(TbBall *c, const char *kind, const char *host, int hash)
{
    cJSON *hint = cJSON_CreateObject();
    cJSON_AddStringToObject(hint, "name", leaf_name(host));
    uint64_t size = 0;
    int64_t modified = INT64_MIN;
    if (native_host_path(host) && tb_path_info(host, &size, &modified) && !tb_is_dir(host))
    {
        cJSON_AddNumberToObject(hint, "size", (double)size);
        if (modified != INT64_MIN)
            cJSON_AddNumberToObject(hint, "mtime", (double)modified);
        if (!strcmp(kind, "image"))
        {
            char *label = volume_label(c, host);
            if (label)
                cJSON_AddStringToObject(hint, "volumeLabel", label);
        }
        unsigned char sum[32];
        if (hash && digest(host, sum))
            cJSON_AddStringToObject(hint, "sha256", tb_ball_hex(c, sum));
    }
    return hint;
}

static cJSON *reference_record(TbBall *c, const char *kind, const char *path, int readonly)
{
    cJSON *record = cJSON_CreateObject();
    cJSON_AddStringToObject(record, "kind", kind);
    reference_location(c, record, path);
    char *host = stored_host_path(c, path);
    cJSON_AddItemToObject(record, "hint", reference_hint(c, kind, host ? host : path, 0));
    cJSON_AddBoolToObject(record, "readonly", readonly);
    return record;
}

/* Keeps a record while it still names the same place, so its hints and host
   data survive; a record for a new place starts again. */
static void reference_put(TbBall *c, cJSON *references, const char *id, const char *kind,
                          const char *path, int readonly)
{
    cJSON *wanted = cJSON_CreateObject();
    reference_location(c, wanted, path);
    cJSON *existing = get(references, id);
    if (cJSON_IsObject(existing) &&
        !strcmp(field(existing, "kind") ? field(existing, "kind") : "", kind) &&
        cJSON_Compare(get(existing, "path"), get(wanted, "path"), 1) &&
        cJSON_Compare(get(existing, "base"), get(wanted, "base"), 1))
        put(existing, "readonly", cJSON_CreateBool(readonly));
    else
        put(references, id, reference_record(c, kind, path, readonly));
    cJSON_Delete(wanted);
}

int tb_ball_sync_references(TbBall *c)
{
    cJSON *references = get(c->manifest, "references");
    if (references && !cJSON_IsObject(references))
        return tb_ball_fail(c, "Invalid references object");
    cJSON *next = cJSON_CreateObject(), *entry;
    cJSON_ArrayForEach(entry, references) if (entry->string &&
                                              strncmp(entry->string, REFERENCE_DRIVE_PREFIX,
                                                      strlen(REFERENCE_DRIVE_PREFIX)))
        cJSON_AddItemToObject(next, entry->string, cJSON_Duplicate(entry, 1));
    cJSON_ArrayForEach(entry, get(c->manifest, "drives"))
    {
        if (!entry->string || !strcmp(entry->string, "C"))
            continue;
        char *id = tb_ball_format(c, REFERENCE_DRIVE_PREFIX "%s", entry->string);
        if (!id)
            break;
        const char *folder = str(entry), *image = field(entry, "image");
        cJSON *old = cJSON_Duplicate(get(references, id), 1);
        if (old)
            cJSON_AddItemToObject(next, id, old);
        if (folder && *folder)
            reference_put(c, next, id, "folder", folder, 0);
        else if (image && *image)
            reference_put(c, next, id, "image", image,
                          !strcmp(field(entry, "device") ? field(entry, "device") : "", "cdrom"));
        else
            cJSON_DeleteItemFromObjectCaseSensitive(next, id);
    }
    if (c->error)
    {
        cJSON_Delete(next);
        return 0;
    }
    if (cJSON_GetArraySize(next))
        put(c->manifest, "references", next);
    else
    {
        cJSON_Delete(next);
        cJSON_DeleteItemFromObjectCaseSensitive(c->manifest, "references");
    }
    return 1;
}

int tb_ball_set_media_references(TbBall *c, const cJSON *sources)
{
    cJSON *references = object(c->manifest, "references");
    if (!references)
        return tb_ball_fail(c, "Invalid references object");
    for (cJSON *entry = references->child, *following; entry; entry = following)
    {
        following = entry->next;
        if (entry->string &&
            !strncmp(entry->string, REFERENCE_MEDIA_PREFIX, strlen(REFERENCE_MEDIA_PREFIX)))
            cJSON_Delete(cJSON_DetachItemViaPointer(references, entry));
    }
    cJSON *media = cJSON_CreateArray(), *source;
    int index = 0;
    cJSON_ArrayForEach(source, sources)
    {
        if (!str(source) || !*str(source))
            continue;
        char *id = tb_ball_format(c, REFERENCE_MEDIA_PREFIX "%d", ++index);
        if (!id)
            break;
        put(references, id, reference_record(c, "media", str(source), 1));
        cJSON_AddItemToArray(media, cJSON_CreateString(id));
    }
    put(c->manifest, "media", media);
    if (!cJSON_GetArraySize(references))
        cJSON_DeleteItemFromObjectCaseSensitive(c->manifest, "references");
    return !c->error;
}

/* $XDG_CONFIG_HOME/2ine on Linux, Application Support on macOS, %APPDATA% on
   Windows: per-host settings that never travel with a ball. */
static char *host_settings_directory(TbBall *c)
{
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata)
        return tb_ball_join(c, appdata, "2ine");
#endif
    char *home = tb_home();
    if (!home)
    {
        tb_ball_fail(c, "Home directory is unavailable");
        return NULL;
    }
    home = tb_ball_keep(c, home);
#ifdef __APPLE__
    return home ? tb_ball_join(c, home, "Library/Application Support/2ine") : NULL;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char *base = xdg && *xdg ? tb_ball_keep(c, strdup(xdg)) : tb_ball_join(c, home, ".config");
    return base ? tb_ball_join(c, base, "2ine") : NULL;
#endif
}

static char *remap_path(TbBall *c)
{
    char *directory = host_settings_directory(c);
    return directory ? tb_ball_join(c, directory, "remap.json") : NULL;
}

static cJSON *remap_table(TbBall *c)
{
    char *path = remap_path(c);
    char *text = path ? tb_ball_read_file(c, path, NULL, 1) : NULL;
    cJSON *table = text && *text ? cJSON_Parse(text) : NULL;
    if (!cJSON_IsArray(get(table, "remaps")))
    {
        cJSON_Delete(table);
        table = cJSON_CreateObject();
        cJSON_AddArrayToObject(table, "remaps");
    }
    return table;
}

cJSON *tb_ball_reference_remaps(TbBall *c, const cJSON *request)
{
    const char *from = field(request, "from"), *to = field(request, "to");
    int remove = cJSON_IsTrue(get(request, "remove"));
    char *path = remap_path(c);
    if (!path)
        return NULL;
    cJSON *table = remap_table(c), *remaps = get(table, "remaps");
    if (from)
    {
        if (!*from || !host_absolute(from) || (!remove && (!to || !host_absolute(to))))
        {
            cJSON_Delete(table);
            tb_ball_fail(c, "A remap needs absolute old and new folder paths");
            return NULL;
        }
        for (int i = cJSON_GetArraySize(remaps) - 1; i >= 0; i--)
            if (field(cJSON_GetArrayItem(remaps, i), "from") &&
                tb_path_equal(field(cJSON_GetArrayItem(remaps, i), "from"), from, strlen(from) + 1))
                cJSON_DeleteItemFromArray(remaps, i);
        if (!remove)
        {
            cJSON *remap = cJSON_CreateObject();
            cJSON_AddStringToObject(remap, "from", from);
            cJSON_AddStringToObject(remap, "to", to);
            cJSON_AddItemToArray(remaps, remap);
        }
        char *directory = host_settings_directory(c);
        if (!directory || !tb_mkdirs(directory) || !tb_ball_write_json(c, path, table))
        {
            cJSON_Delete(table);
            if (!c->error)
                tb_ball_fail(c, "Cannot save the reference remap table");
            return NULL;
        }
    }
    cJSON_AddStringToObject(table, "path", path);
    return table;
}

static int hint_matches(TbBall *c, const cJSON *record, const char *host, int verify)
{
    const cJSON *hint = get(record, "hint");
    uint64_t size = 0;
    int64_t modified = 0;
    if (cJSON_IsNumber(get(hint, "size")) && tb_path_info(host, &size, &modified) &&
        (double)size != get(hint, "size")->valuedouble)
        return 0;
    const char *label = field(hint, "volumeLabel");
    if (label && !strcmp(field(record, "kind") ? field(record, "kind") : "", "image"))
    {
        char *current = volume_label(c, host);
        if (!current || strcmp(current, label))
            return 0;
    }
    const char *sum = field(hint, "sha256");
    unsigned char hash[32];
    if (verify && sum && (!digest(host, hash) || strcmp(sum, tb_ball_hex(c, hash))))
        return 0;
    return 1;
}

static char *search_leaf(TbBall *c, const char *directory, const char *leaf, unsigned depth,
                         unsigned *visited)
{
    if (depth > REFERENCE_SEARCH_DEPTH || !tb_is_dir(directory))
        return NULL;
    size_t count = 0;
    char **names = tb_names(directory, &count);
    if (count == (size_t)-1)
        return NULL;
    char *found = NULL;
    for (size_t i = 0; !found && i < count && *visited < REFERENCE_SEARCH_ENTRIES; i++, ++*visited)
        if (equal_case(names[i], leaf))
        {
            char *candidate = tb_ball_join(c, directory, names[i]);
            if (candidate && tb_is_file(candidate))
                found = candidate;
        }
    for (size_t i = 0; !found && i < count && *visited < REFERENCE_SEARCH_ENTRIES; i++)
    {
        char *child = tb_ball_join(c, directory, names[i]);
        if (child && !tb_is_link(child) && tb_is_dir(child))
            found = search_leaf(c, child, leaf, depth + 1, visited);
    }
    tb_names_free(names, count);
    return found;
}

/* A moved file is only a candidate: the user relinks it explicitly. */
static char *relocation_candidate(TbBall *c, const cJSON *record, const char *host,
                                  const cJSON *search_roots, const cJSON *remaps)
{
    const cJSON *remap;
    cJSON_ArrayForEach(remap, remaps)
    {
        const char *from = field(remap, "from"), *to = field(remap, "to");
        size_t n = from ? strlen(from) : 0;
        if (!n || !to || !tb_path_equal(host, from, n) ||
            (host[n] && host[n] != '/' && host[n] != '\\'))
            continue;
        char *candidate = tb_ball_format(c, "%s%s", to, host + n);
        if (candidate && tb_is_file(candidate) && hint_matches(c, record, candidate, 0))
            return candidate;
    }
    const char *leaf = leaf_name(host);
    char *parent = tb_ball_keep(c, strdup(c->ball));
    char *slash = parent ? (char *)leaf_name(parent) : NULL;
    if (slash && slash > parent)
        slash[-1] = 0;
    const char *places[] = {c->ball, tb_ball_join(c, c->ball, REFERENCE_BUNDLE_DIRECTORY), parent};
    unsigned visited = 0;
    for (unsigned i = 0; i < sizeof places / sizeof *places; i++)
    {
        char *candidate = places[i] ? tb_ball_join(c, places[i], leaf) : NULL;
        if (candidate && strcmp(candidate, host) && tb_is_file(candidate) &&
            hint_matches(c, record, candidate, 0))
            return candidate;
    }
    const cJSON *root;
    cJSON_ArrayForEach(root, search_roots) if (str(root))
    {
        char *candidate = search_leaf(c, str(root), leaf, 0, &visited);
        if (candidate && hint_matches(c, record, candidate, 0))
            return candidate;
    }
    return NULL;
}

static cJSON *reference_state(TbBall *c, const cJSON *record, const cJSON *search_roots,
                              const cJSON *remaps, int verify)
{
    cJSON *status = cJSON_Duplicate(record, 1);
    cJSON_DeleteItemFromObjectCaseSensitive(status, "host");
    char *host = reference_host_path(c, record);
    const char *state = "available";
    if (!host)
        state = "missing";
    else if (!native_host_path(host))
        state = "foreign";
    else if (!tb_is_file(host) && !tb_is_dir(host))
        state = "missing";
    else
    {
        FILE *file = tb_is_dir(host) ? NULL : tb_fopen(host, "rb");
        if (tb_is_dir(host) ? !tb_directory_readable(host) : !file)
            state = "unreadable";
        else if (!tb_is_dir(host) && !hint_matches(c, record, host, verify))
            state = "mismatch";
        if (file)
            fclose(file);
    }
    if (host)
        cJSON_AddStringToObject(status, "resolved", host);
    if (host && (!strcmp(state, "missing") || !strcmp(state, "foreign")))
    {
        char *candidate = relocation_candidate(c, record, host, search_roots, remaps);
        if (candidate)
        {
            state = "relocation-candidate";
            cJSON_AddStringToObject(status, "candidate", candidate);
        }
    }
    cJSON_AddStringToObject(status, "state", state);
    return status;
}

cJSON *tb_ball_reference_status(TbBall *c, const cJSON *request)
{
    const char *only = field(request, "id");
    const cJSON *search_roots = get(request, "searchRoots");
    if (search_roots && !tb_ball_array_strings(c, search_roots))
        return NULL;
    int verify = cJSON_IsTrue(get(request, "verify"));
    if (!tb_ball_sync_references(c))
        return NULL;
    cJSON *table = remap_table(c);
    cJSON *result = cJSON_CreateObject(), *states = cJSON_AddObjectToObject(result, "references");
    const cJSON *record;
    cJSON_ArrayForEach(record, get(c->manifest, "references"))
    {
        if (only && strcmp(only, record->string))
            continue;
        cJSON_AddItemToObject(
            states, record->string,
            reference_state(c, record, search_roots, get(table, "remaps"), verify));
    }
    /* A device slot without media has no reference; report it as ejected. */
    const cJSON *drive;
    cJSON_ArrayForEach(drive, get(c->manifest, "drives"))
    {
        char *id = tb_ball_format(c, REFERENCE_DRIVE_PREFIX "%s", drive->string);
        if (!id || (only && strcmp(only, id)) || !field(drive, "device") || field(drive, "image"))
            continue;
        cJSON *ejected = cJSON_AddObjectToObject(states, id);
        cJSON_AddStringToObject(ejected, "kind", "image");
        cJSON_AddStringToObject(ejected, "state", "ejected");
    }
    cJSON_AddItemToObject(
        result, "media",
        cJSON_Duplicate(get(c->manifest, "media") ? get(c->manifest, "media") : cJSON_CreateArray(),
                        1));
    cJSON_Delete(table);
    if (only && !get(states, only))
    {
        cJSON_Delete(result);
        tb_ball_fail(c, "Unknown reference: %s", only);
        return NULL;
    }
    return result;
}

static int drive_letter(TbBall *c, const char *id, char letter[2])
{
    size_t n = strlen(REFERENCE_DRIVE_PREFIX);
    if (!id || strncmp(id, REFERENCE_DRIVE_PREFIX, n) || !id[n] || id[n + 1] || id[n] < 'A' ||
        id[n] > 'Z' || id[n] == 'C')
        return tb_ball_fail(c, "Choose a drive reference such as drive-D");
    letter[0] = id[n];
    letter[1] = 0;
    return 1;
}

/* Runs a ball command on behalf of a reference action. */
static int reference_command(TbBall *c, const char *a, const char *b, const char *d)
{
    cJSON *request = cJSON_CreateObject(),
          *arguments = cJSON_AddArrayToObject(request, "arguments");
    cJSON_AddItemToArray(arguments, cJSON_CreateString(a));
    cJSON_AddItemToArray(arguments, cJSON_CreateString(b));
    cJSON_AddItemToArray(arguments, cJSON_CreateString(d));
    int ok = tb_ball_command(c, request);
    cJSON_Delete(request);
    return ok;
}

static int capture_hash(TbBall *c, const char *id)
{
    cJSON *record = get(get(c->manifest, "references"), id);
    char *host = reference_host_path(c, record);
    unsigned char sum[32];
    if (!host || !digest(host, sum))
        return tb_ball_fail(c, "Cannot hash reference %s", id);
    put(object(record, "hint"), "sha256", cJSON_CreateString(tb_ball_hex(c, sum)));
    return tb_ball_save_manifest(c);
}

int tb_ball_relink_reference(TbBall *c, const cJSON *request)
{
    const char *id = field(request, "id"), *path = field(request, "path");
    if (!id || !path || !*path)
        return tb_ball_fail(c, "A reference and its new path are required");
    if (!strncmp(id, REFERENCE_MEDIA_PREFIX, strlen(REFERENCE_MEDIA_PREFIX)))
    {
        cJSON *record = get(get(c->manifest, "references"), id);
        char *resolved = tb_ball_resolve_path(c, path, 1);
        if (!cJSON_IsObject(record))
            return tb_ball_fail(c, "Unknown reference: %s", id);
        if (!resolved)
            return 0;
        put(get(c->manifest, "references"), id, reference_record(c, "media", resolved, 1));
    }
    else
    {
        char letter[2];
        if (!drive_letter(c, id, letter))
            return 0;
        const cJSON *drive = get(get(c->manifest, "drives"), letter);
        if (!drive)
            return tb_ball_fail(c, "Unknown reference: %s", id);
        if (!reference_command(c, cJSON_IsObject(drive) ? "map-image" : "map", letter, path))
            return 0;
    }
    if (!tb_ball_sync_references(c) || !tb_ball_save_manifest(c))
        return 0;
    return !cJSON_IsTrue(get(request, "hash")) || capture_hash(c, id);
}

int tb_ball_bundle_reference(TbBall *c, const cJSON *request)
{
    char letter[2];
    const char *id = field(request, "id");
    if (!drive_letter(c, id, letter))
        return 0;
    cJSON *drive = get(get(c->manifest, "drives"), letter);
    const char *image = field(drive, "image");
    if (!image)
        return tb_ball_fail(c, "Only an attached drive image can be bundled");
    if (cJSON_IsTrue(get(drive, "locked")))
        return tb_ball_fail(c, "Drive %s: is locked to its image. Unlock it before bundling.",
                            letter);
    char *source = tb_ball_resolve_path(c, stored_host_path(c, image), 1);
    if (!source)
        return 0;
    if (tb_ball_inside(source, c->ball))
        return tb_ball_fail(c, "This image is already inside the ball");
    const char *extension = strrchr(leaf_name(source), '.');
    char *relative = tb_ball_format(c, REFERENCE_BUNDLE_DIRECTORY "/%s%s", letter,
                                    extension ? extension : ".img");
    char *directory = tb_ball_join(c, c->ball, REFERENCE_BUNDLE_DIRECTORY);
    char *target = relative ? tb_ball_join(c, c->ball, relative) : NULL;
    if (!directory || !target || !tb_mkdirs(directory) || !tb_copy_new(source, target))
        return tb_ball_fail(c, "Cannot copy the image into the ball; nothing was replaced");
    put(drive, "image", cJSON_CreateString(relative));
    return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
}

int tb_ball_unbundle_reference(TbBall *c, const cJSON *request)
{
    char letter[2];
    const char *id = field(request, "id"), *path = field(request, "path");
    if (!drive_letter(c, id, letter))
        return 0;
    cJSON *drive = get(get(c->manifest, "drives"), letter);
    const char *image = field(drive, "image");
    if (!image || !path || !*path)
        return tb_ball_fail(c, "A bundled drive image and a destination are required");
    if (cJSON_IsTrue(get(drive, "locked")))
        return tb_ball_fail(c, "Drive %s: is locked to its image. Unlock it before moving it.",
                            letter);
    char *source = tb_ball_resolve_path(c, stored_host_path(c, image), 1);
    char *target = tb_ball_resolve_path(c, path, 0);
    if (!source || !target)
        return 0;
    if (!tb_ball_inside(source, c->ball))
        return tb_ball_fail(c, "This image is not inside the ball");
    if (tb_ball_inside(target, c->ball))
        return tb_ball_fail(c, "Choose a destination outside the ball");
    if (!tb_move_new(source, target))
    {
        if (!tb_copy_new(source, target))
            return tb_ball_fail(c, "Cannot move the image out of the ball; nothing was replaced");
        remove(source);
    }
    put(drive, "image", cJSON_CreateString(target));
    return tb_ball_regenerate(c) && tb_ball_save_manifest(c);
}

int tb_ball_set_reference_host_data(TbBall *c, const cJSON *request)
{
    const char *id = field(request, "id"), *host = field(request, "host");
    cJSON *record = get(get(c->manifest, "references"), id ? id : "");
    if (!cJSON_IsObject(record) || !host || !*host)
        return tb_ball_fail(c, "A reference and a host identifier are required");
    const cJSON *data = get(request, "data");
    if (!data || cJSON_IsNull(data))
    {
        cJSON_DeleteItemFromObjectCaseSensitive(get(record, "host"), host);
        if (!cJSON_GetArraySize(get(record, "host")))
            cJSON_DeleteItemFromObjectCaseSensitive(record, "host");
    }
    else
    {
        cJSON *hosts = object(record, "host");
        if (!hosts)
            return tb_ball_fail(c, "Invalid reference host data");
        put(hosts, host, cJSON_Duplicate(data, 1));
    }
    return tb_ball_save_manifest(c);
}

void tb_ball_strip_reference_host_data(cJSON *manifest)
{
    cJSON *record;
    cJSON_ArrayForEach(record, get(manifest, "references"))
        cJSON_DeleteItemFromObjectCaseSensitive(record, "host");
}
