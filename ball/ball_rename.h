/* Public domain. No rights reserved. Wholly AI-authored. */
/* Package moves stage manifest changes and never replace another host item. */
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
static int rename_same_bytes(Context *c, const char *path, const char *expected, size_t length)
{
    size_t count = 0;
    char *current = read_file(c, path, &count, 0);
    return current && count == length && !memcmp(current, expected, count);
}

static char *rename_leaf(Context *c, const char *name)
{
    if (!name || !*name || !strcmp(name, ".") || !strcmp(name, ".."))
    {
        fail(c, "Enter a ball name");
        return NULL;
    }
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || *p == 127 || strchr("/\\:<>\"|?*", *p))
        {
            fail(
                c,
                "Ball names cannot contain path separators, control characters or reserved filename characters");
            return NULL;
        }
    size_t length = strlen(name);
    if (name[length - 1] == '.' || name[length - 1] == ' ')
    {
        fail(c, "A ball name cannot end in a period or space");
        return NULL;
    }
    size_t stem = length >= 9 && !strcasecmp(name + length - 9, ".2ineball") ? length - 9 : length;
    if (!stem || stem + 9 > 255)
    {
        fail(c, "The ball name is empty or too long");
        return NULL;
    }
    size_t base = strcspn(name, ".");
    if ((base == 3 && (!strncasecmp(name, "CON", 3) || !strncasecmp(name, "PRN", 3) ||
                       !strncasecmp(name, "AUX", 3) || !strncasecmp(name, "NUL", 3))) ||
        (base == 4 && (!strncasecmp(name, "COM", 3) || !strncasecmp(name, "LPT", 3)) &&
         name[3] >= '1' && name[3] <= '9'))
    {
        fail(c, "Choose a name that is not reserved by the host filesystem");
        return NULL;
    }
    return format(c, "%.*s.2ineball", (int)stem, name);
}

static int rename_host_field(Context *c, cJSON *entry, const char *key, const char *old,
                             const char *destination, int *changed)
{
    cJSON *value = key ? get(entry, key) : entry;
    const char *path = str(value);
    if (!path || !host_absolute(path) || !native_host_path(path))
        return 1;
    char *absolute = resolve_path(c, path, 0);
    if (!absolute)
        return 0;
    if (!inside(absolute, old))
        return 1;
    char *replacement = format(c, "%s%s", destination, absolute + strlen(old));
    if (!replacement || !cJSON_SetValuestring(value, replacement))
        return fail(c, "Cannot update a ball-owned host path");
    *changed = 1;
    return 1;
}

static int rename_manifest_paths(Context *c, const char *old, const char *destination, int *changed)
{
    cJSON *entry;
    cJSON_ArrayForEach(entry, get(c->manifest, "drives"))
    {
        if (!rename_host_field(c, entry, cJSON_IsObject(entry) ? "image" : NULL, old, destination,
                               changed))
            return 0;
    }
    if (!rename_host_field(c, get(c->manifest, "runtime"), "build", old, destination, changed))
        return 0;
    /* Installed sound banks use package-relative names, including after a move. */
    cJSON_ArrayForEach(entry, get(c->manifest, "soundBanks"))
    {
        cJSON *value = get(entry, "file");
        const char *path = str(value);
        if (path && host_absolute(path) && native_host_path(path))
        {
            char *absolute = resolve_path(c, path, 0);
            if (!absolute)
                return 0;
            if (inside(absolute, old))
            {
                const char *relative = absolute + strlen(old);
                while (*relative == '/' || *relative == '\\')
                    relative++;
                if (strncmp(relative, "soundfonts/", 11) || !cJSON_SetValuestring(value, relative))
                    return fail(c, "Installed sound banks must remain inside soundfonts");
                *changed = 1;
            }
        }
    }
    return 1;
}

static int rename_profile_environment(Context *c, cJSON *environment, const char *old,
                                      const char *destination, int *changed)
{
    cJSON *entry;
    cJSON_ArrayForEach(entry, environment) if (entry->string &&
                                               (!strcasecmp(entry->string, "USER_INI") ||
                                                !strcasecmp(entry->string, "SYSTEM_INI") ||
                                                !strcasecmp(entry->string, "COMSPEC")) &&
                                               !rename_host_field(c, entry, NULL, old, destination,
                                                                  changed)) return 0;
    return 1;
}

static cJSON *rename_ball(Context *c, const cJSON *request)
{
    const char *source = field(request, "ball");
    char *leaf = rename_leaf(c, field(request, "text"));
    if (!source || !leaf)
        return NULL;
    size_t source_length = strlen(source);
    if (tb_is_link(source))
    {
        fail(c, "Choose a regular ball package, not a symbolic link");
        return NULL;
    }
    if (source_length < 9 || strcasecmp(source + source_length - 9, ".2ineball"))
    {
        fail(c, "Only .2ineball packages can be renamed");
        return NULL;
    }
    c->ball = resolve_path(c, source, 0);
    if (!c->ball || tb_is_link(c->ball) || !tb_is_dir(c->ball))
    {
        fail(c, "Choose a regular ball package, not a symbolic link");
        return NULL;
    }
    char *old = resolve_path(c, c->ball, 1);
    if (!old)
        return NULL;
    char *home = tb_home();
    if (home)
        home = keep(c, home);
    char *canonical_home = home ? tb_canonical(home) : NULL;
    if (canonical_home)
        canonical_home = keep(c, canonical_home);
    if (!strcmp(old, "/") || (canonical_home && !strcmp(old, canonical_home)))
    {
        fail(c, "The home or filesystem root cannot be renamed as a ball");
        return NULL;
    }
    c->ball = old;
    char *manifest_path = join(c, old, "ball.json");
    if (tb_is_link(manifest_path) || !tb_is_file(manifest_path))
    {
        fail(c, "The ball manifest must be a regular file, not a symbolic link");
        return NULL;
    }
    size_t original_length = 0;
    char *original = read_file(c, manifest_path, &original_length, 0);
    if (!original)
        return NULL;
    const char *expected = field(request, "expected");
    if (expected)
    {
        unsigned char digest[32];
        if (!digest_file(c, manifest_path, digest) || strcmp(expected, hex(c, digest)))
        {
            fail(c, "The ball manifest changed on disk; refresh before renaming");
            return NULL;
        }
    }
    c->manifest = cJSON_ParseWithOpts(original, NULL, 1);
    if (!cJSON_IsObject(c->manifest) || !cJSON_IsNumber(get(c->manifest, "version")) ||
        get(c->manifest, "version")->valuedouble != 1 || !roots(c))
    {
        fail(c, "Unsupported or invalid ball.json");
        return NULL;
    }
    if (!inside(c->roots[2], old))
    {
        fail(c, "The C: drive must be private to this ball");
        return NULL;
    }
    char *parent = keep(c, strdup(old)), *separator = parent ? strrchr(parent, '/') : NULL;
#ifdef _WIN32
    char *backslash = parent ? strrchr(parent, '\\') : NULL;
    if (backslash && (!separator || backslash > separator))
        separator = backslash;
#endif
    if (!separator)
    {
        fail(c, "Cannot determine the ball's parent folder");
        return NULL;
    }
    if (separator == parent)
        separator[1] = 0;
    else
        *separator = 0;
    char *destination = join(c, parent, leaf);
    if (!destination)
        return NULL;
    if (!strcmp(old, destination))
    {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "path", old);
        cJSON_AddItemToObject(result, "snapshot", snapshot(c));
        return result;
    }
    uint64_t size;
    int64_t modified;
    if (tb_is_link(destination) || tb_path_info(destination, &size, &modified))
    {
        fail(
            c,
            "That name already exists. Choose another name; case-only renames are not supported on case-insensitive filesystems");
        return NULL;
    }
    cJSON *preflight = snapshot(c);
    cJSON_Delete(preflight);
    if (c->error)
        return NULL;
    int changed = 0;
    if (!rename_manifest_paths(c, old, destination, &changed) ||
        !rename_profile_environment(c, get(c->manifest, "environment"), old, destination, &changed))
        return NULL;
    cJSON *application;
    cJSON_ArrayForEach(
        application,
        get(c->manifest,
            "applications")) if (!rename_profile_environment(c, get(application, "environment"),
                                                             old, destination,
                                                             &changed)) return NULL;
    char *stage = keep(c, tb_archive_workspace(old));
    if (!stage)
        return NULL;
    const char *stage_leaf = strrchr(stage, '/');
#ifdef _WIN32
    const char *stage_backslash = strrchr(stage, '\\');
    if (stage_backslash && (!stage_leaf || stage_backslash > stage_leaf))
        stage_leaf = stage_backslash;
#endif
    stage_leaf = stage_leaf ? stage_leaf + 1 : stage;
    char *backup = join(c, stage, "original.json"), *prepared = join(c, stage, "renamed.json");
    int staged = tb_atomic_write(backup, original, original_length) &&
                 (!changed || write_json(c, prepared, c->manifest));
    size_t prepared_length = 0;
    char *prepared_bytes = changed && staged ? read_file(c, prepared, &prepared_length, 0) : NULL;
    if (!staged || (changed && !prepared_bytes) || c->error ||
        !rename_same_bytes(c, manifest_path, original, original_length))
    {
        tb_remove_tree(stage);
        fail(c, "The ball manifest changed on disk or rename preparation failed");
        return NULL;
    }
    if (!tb_move_new(old, destination))
    {
        int error = errno;
        tb_remove_tree(stage);
        fail(c, "Cannot rename ball without replacing another item: %s", strerror(error));
        return NULL;
    }
    c->ball = destination;
    stage = join(c, destination, stage_leaf);
    manifest_path = join(c, destination, "ball.json");
    prepared = join(c, stage, "renamed.json");
    backup = join(c, stage, "original.json");
    int committed = 0;
    cJSON *value = NULL;
    if (!rename_same_bytes(c, manifest_path, original, original_length))
        fail(c, "The ball manifest changed during rename");
    else if (changed && !tb_replace(prepared, manifest_path))
        fail(c, "Cannot commit renamed ball manifest: %s", strerror(errno));
    else
    {
        committed = changed;
        value = snapshot(c);
    }
    if (!c->error && value)
    {
        tb_remove_tree(stage);
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "path", destination);
        cJSON_AddItemToObject(result, "snapshot", value);
        return result;
    }
    cJSON_Delete(value);
    if (committed && !rename_same_bytes(c, manifest_path, prepared_bytes, prepared_length))
    {
        char *cause = c->error;
        c->error = NULL;
        fail(
            c,
            "The package is now at %s; its manifest changed again, so rollback was not attempted. %s",
            destination, cause ? cause : "Rename could not finish");
        free(cause);
        return NULL;
    }
    /* A rollback never replaces an item that appeared at the former path. */
    if (tb_move_new(destination, old))
    {
        c->ball = old;
        stage = join(c, old, stage_leaf);
        if (!committed || tb_replace(join(c, stage, "original.json"), join(c, old, "ball.json")))
            tb_remove_tree(stage);
    }
    else
    {
        char *cause = c->error;
        c->error = NULL;
        fail(c, "The package is now at %s; rollback could not replace the former path. %s",
             destination, cause ? cause : "Rename could not finish");
        free(cause);
    }
    fail(c, "Cannot finish renaming the ball");
    return NULL;
}
