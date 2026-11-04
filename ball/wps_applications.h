/* Public domain. No rights reserved. Wholly AI-authored. */
/* Project persistent WPProgram registrations into independently managed launchers. */
#ifndef TB_WPS_APPLICATIONS_H
#define TB_WPS_APPLICATIONS_H
#define TB_WPS_PROFILE_LIMIT (8u * 1024u * 1024u)
#define TB_WPS_OBJECT_LIMIT 8192u
static void wps_digest(const unsigned char *bytes, size_t size, unsigned char digest[32])
{
    SHA256_CTX hash;
    sha256_init(&hash);
    sha256_update(&hash, bytes, size);
    sha256_final(&hash, digest);
}
static char *wps_property(Context *c, const char *setup, const char *wanted)
{
    char *result = NULL;
    while (setup && *setup)
    {
        char key[256], value[32768];
        size_t k = 0, n = 0;
        while (*setup && *setup != '=' && *setup != ';')
        {
            char ch = *setup++;
            if (ch == '^' && *setup)
                ch = *setup++;
            if (k + 1 < sizeof key)
                key[k++] = ch;
        }
        key[k] = 0;
        int match = *setup == '=' && equal_case(trim(key), wanted);
        if (*setup == '=')
            setup++;
        while (*setup && *setup != ';')
        {
            char ch = *setup++;
            if (ch == '^' && *setup)
                ch = *setup++;
            if (n + 1 < sizeof value)
                value[n++] = ch;
        }
        value[n] = 0;
        if (match)
            result = latin1_to_utf8(c, value, n);
        if (*setup)
            setup++;
    }
    return result;
}
static char *wps_title(Context *c, const char *raw)
{
    char *value = keep(c, strdup(raw ? raw : ""));
    if (!value)
        return NULL;
    char *out = value;
    int space = 0;
    for (const char *p = value; *p; p++)
    {
        if (*p == '^' || isspace((unsigned char)*p))
            space = out != value;
        else
        {
            if (space)
                *out++ = ' ';
            *out++ = *p;
            space = 0;
        }
    }
    *out = 0;
    return value;
}
static cJSON *wps_arguments(Context *c, const char *text)
{
    cJSON *args = cJSON_CreateArray();
    size_t length = strlen(text ? text : "");
    char *word = keep(c, malloc(length + 1));
    if (!word)
    {
        cJSON_Delete(args);
        return NULL;
    }
    size_t n = 0;
    int quote = 0, started = 0;
    for (const char *p = text ? text : "";; p++)
    {
        if (*p == '"')
        {
            started = 1;
            if (quote && p[1] == '"')
                word[n++] = *++p;
            else
                quote = !quote;
        }
        else if (!*p || (!quote && isspace((unsigned char)*p)))
        {
            if (started)
            {
                word[n] = 0;
                cJSON_AddItemToArray(args, cJSON_CreateString(word));
                n = 0;
                started = 0;
            }
            if (!*p)
                break;
        }
        else
        {
            word[n++] = *p;
            started = 1;
        }
    }
    return args;
}
static char *wps_resolve(Context *c, const char *name, const char *cwd, const char *paths)
{
    if (!name || !*name)
        return NULL;
    char *plain = keep(c, strdup(name));
    if (!plain)
        return NULL;
    plain = trim(plain);
    size_t length = strlen(plain);
    if (length > 1 && plain[0] == '"' && plain[length - 1] == '"')
    {
        plain[length - 1] = 0;
        plain++;
    }
    if (isalpha((unsigned char)plain[0]) && plain[1] == ':')
        return plain;
    if (plain[0] == '\\' || plain[0] == '/')
        return format(c, "C:%s", plain);
    char *search = format(c, "%s;%s", cwd ? cwd : "C:\\", paths ? paths : "");
    for (char *p = search; p && *p;)
    {
        char *next = strchr(p, ';');
        if (next)
            *next++ = 0;
        p = trim(p);
        if (*p)
        {
            char *candidate = format(c, "%s\\%s", p, plain);
            char *host = candidate ? host_path(c, candidate, 0) : NULL;
            if (host && tb_is_file(host))
                return candidate;
            free(c->error);
            c->error = NULL;
        }
        p = next;
    }
    return plain;
}
static void wps_hide(Context *c, const cJSON *app)
{
    const char *handle = field(get(app, "wpsRegistration"), "handle");
    if (handle)
        put(object(c->manifest, "wpsHidden"), handle, cJSON_CreateBool(1));
}
static void wps_detach(Context *c, cJSON *app)
{
    wps_hide(c, app);
    cJSON_DeleteItemFromObjectCaseSensitive(app, "wpsRegistration");
}
static void wps_icon_beside_program(Context *c, cJSON *icon, const char *program)
{
    const char *declared = field(icon, "path"), *base = declared, *directory = NULL;
    if (!declared || !program || get(icon, "resource"))
        return;
    char *host = host_path(c, declared, 0);
    if (host && tb_is_file(host))
        return;
    free(c->error);
    c->error = NULL;
    for (const char *p = declared; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    for (const char *p = program; *p; p++)
        if (*p == '\\' || *p == '/')
            directory = p;
    if (!directory || !*base)
        return;
    char *candidate = format(c, "%.*s\\%s", (int)(directory - program), program, base);
    host = candidate ? host_path(c, candidate, 0) : NULL;
    uint64_t length = 0;
    int64_t modified = 0;
    size_t size = 0;
    char *bytes = host && tb_path_info(host, &length, &modified) && length <= TB_WPS_PROFILE_LIMIT
                      ? read_file(c, host, &size, 1)
                      : NULL;
    TbIconImage *image = bytes ? tb_icon_bitmap((unsigned char *)bytes, size) : NULL;
    if (image)
    {
        cJSON_AddItemToObject(icon, "declaredPath", cJSON_Duplicate(get(icon, "path"), 1));
        put(icon, "path", cJSON_CreateString(candidate));
        cJSON_AddStringToObject(
            icon, "fallbackReason",
            "Registered icon path is absent; the same icon filename is installed beside the executable.");
        tb_icon_free(image);
    }
    free(c->error);
    c->error = NULL;
}
static const cJSON *wps_previous(const cJSON *apps, const char *handle)
{
    const cJSON *app;
    cJSON_ArrayForEach(
        app, apps) if (field(get(app, "wpsRegistration"), "handle") &&
                       !strcmp(field(get(app, "wpsRegistration"), "handle"), handle)) return app;
    return NULL;
}
static cJSON *wps_objects(Context *c, const unsigned char *bytes, size_t size)
{
    if (size > TB_WPS_PROFILE_LIMIT || (size >= 12 && !memcmp(bytes, "2INEPRF1", 8) &&
                                        configuration_u32(bytes + 8) > TB_WPS_OBJECT_LIMIT))
    {
        fail(c, "Workplace Shell profile exceeds the registration limit");
        return NULL;
    }
    cJSON *entries = configuration_parse(c, bytes, size), *objects = cJSON_CreateObject();
    const cJSON *entry;
    unsigned count = 0;
    cJSON_ArrayForEach(entry, entries)
    {
        if (!equal_case(field(entry, "application"), "objects"))
            continue;
        const char *handle = field(entry, "key");
        char *end = NULL;
        unsigned long id = strtoul(handle, &end, 16);
        if (!*handle || strlen(handle) != 8 || *end || id < 0x20000 || id > INT32_MAX ||
            ++count > TB_WPS_OBJECT_LIMIT)
        {
            fail(c, "Invalid Workplace Shell object handle");
            break;
        }
        size_t n = 0;
        unsigned char *data = configuration_unhex(c, field(entry, "data"), &n);
        if (!data)
            break;
        const char *names[] = {"class", "title", "location", "setup"};
        cJSON *record = cJSON_CreateObject();
        size_t at = 0;
        for (unsigned i = 0; i < 4; i++)
        {
            unsigned char *nul = at < n ? memchr(data + at, 0, n - at) : NULL;
            if (!nul)
            {
                fail(c, "Invalid Workplace Shell object record");
                break;
            }
            cJSON_AddStringToObject(
                record, names[i], latin1_to_utf8(c, (char *)data + at, (size_t)(nul - data - at)));
            at = (size_t)(nul - data) + 1;
        }
        if (at != n)
            fail(c, "Invalid Workplace Shell object extent");
        /* Setup parsing consumes original Latin-1, while other fields are UTF-8. */
        if (!c->error)
        {
            const char *raw_setup = (char *)data;
            for (unsigned i = 0; i < 3; i++)
                raw_setup += strlen(raw_setup) + 1;
            cJSON_AddStringToObject(record, "id", wps_property(c, raw_setup, "OBJECTID"));
            const char *properties[] = {"TITLE",    "EXENAME",      "STARTUPDIR", "PARAMETERS",
                                        "ICONFILE", "ICONRESOURCE", "PROGTYPE",   NULL};
            for (unsigned i = 0; properties[i]; i++)
            {
                char *value = wps_property(c, raw_setup, properties[i]);
                if (value)
                    cJSON_AddStringToObject(record, properties[i], value);
            }
            unsigned char digest[32];
            wps_digest(data, n, digest);
            cJSON_AddStringToObject(record, "hash", hex(c, digest));
            put(objects, handle, record);
        }
        else
            cJSON_Delete(record);
    }
    cJSON_Delete(entries);
    if (c->error)
    {
        cJSON_Delete(objects);
        return NULL;
    }
    return objects;
}
static cJSON *wps_folders(Context *c, const cJSON *objects, const char *location)
{
    cJSON *folders = cJSON_CreateArray();
    for (unsigned depth = 0; location && depth < 256; depth++)
    {
        if (equal_case(location, "<WP_DESKTOP>"))
            return folders;
        const cJSON *folder, *found = NULL;
        cJSON_ArrayForEach(folder, objects) if (equal_case(field(folder, "class"), "WPFolder") &&
                                                field(folder, "id") &&
                                                equal_case(field(folder, "id"), location))
        {
            found = folder;
            break;
        }
        if (!found)
            break;
        const char *title = field(found, "TITLE");
        cJSON_InsertItemInArray(
            folders, 0, cJSON_CreateString(wps_title(c, title ? title : field(found, "title"))));
        location = field(found, "location");
    }
    cJSON_Delete(folders);
    return NULL;
}
static int wps_sync(Context *c)
{
    if (!c->roots[2])
        return 1;
    char *path = case_path(c, join(c, c->roots[2], "OS2/OBJECTS.INI"));
    int managed = 0;
    const cJSON *existing;
    cJSON_ArrayForEach(existing, get(c->manifest, "applications")) managed |=
        get(existing, "wpsRegistration") != NULL;
    if (path && !tb_is_file(path) && !managed)
        return 1;
    uint64_t file_size = 0;
    int64_t modified = 0;
    if (path && tb_path_info(path, &file_size, &modified) && file_size > TB_WPS_PROFILE_LIMIT)
        fail(c, "Workplace Shell profile exceeds the registration limit");
    size_t size = 0;
    char *bytes = path && !c->error ? read_file(c, path, &size, 1) : NULL;
    cJSON *objects = bytes ? wps_objects(c, (unsigned char *)bytes, size) : NULL;
    if (!objects)
    {
        c->registration_warning =
            keep(c, strdup(c->error ? c->error : "Cannot read Workplace Shell registrations"));
        free(c->error);
        c->error = NULL;
        return 1;
    }
    cJSON *old = object(c->manifest, "applications"), *apps = cJSON_CreateObject(),
          *report = cJSON_CreateObject(), *events = cJSON_AddArrayToObject(report, "objects");
    const cJSON *app, *record;
    cJSON_ArrayForEach(app, old) if (!get(app, "wpsRegistration"))
        cJSON_AddItemToObject(apps, app->string, cJSON_Duplicate(app, 1));
    unsigned added = 0, updated = 0, removed = 0, skipped = 0;
    cJSON *environment = guest_environment(c, NULL);
    cJSON_ArrayForEach(record, objects)
    {
        if (!equal_case(field(record, "class"), "WPProgram"))
            continue;
        const char *handle = record->string, *program = field(record, "EXENAME");
        cJSON *event = cJSON_CreateObject();
        cJSON_AddStringToObject(event, "handle", handle);
        cJSON_AddItemToArray(events, event);
        cJSON *folders = wps_folders(c, objects, field(record, "location"));
        if (!program || !*program || !folders ||
            cJSON_IsTrue(get(get(c->manifest, "wpsHidden"), handle)))
        {
            cJSON_Delete(folders);
            skipped++;
            cJSON_AddStringToObject(event, "status", "skipped");
            cJSON_AddStringToObject(event, "reason",
                                    !program || !*program ? "No executable registered"
                                    : !folders ? "Object is outside the desktop folder tree"
                                               : "Launcher was removed or edited by the user");
            continue;
        }
        const char *title = field(record, "TITLE");
        char *name = wps_title(c, title ? title : field(record, "title"));
        if (!name || !*name)
            name = format(c, "Program %s", handle);
        if (get(apps, name))
            name = format(c, "%s [WPS %s]", name, handle);
        while (get(apps, name))
            name = format(c, "%s+", name);
        const char *cwd = field(record, "STARTUPDIR");
        char *executable = wps_resolve(c, !strcmp(program, "*") ? "CMD.EXE" : program, cwd,
                                       field(environment, "OS2PATH"));
        cJSON *next = cJSON_CreateObject();
        cJSON_AddStringToObject(next, "program", executable);
        cJSON_AddItemToObject(next, "arguments", wps_arguments(c, field(record, "PARAMETERS")));
        if (cwd && *cwd)
            cJSON_AddStringToObject(
                next, "cwd",
                cwd[0] == '\\'
                    ? format(c, "%c:%s", executable && executable[1] == ':' ? executable[0] : 'C',
                             cwd)
                    : cwd);
        cJSON *identity = cJSON_AddObjectToObject(next, "wpsRegistration");
        cJSON_AddStringToObject(identity, "handle", handle);
        cJSON_AddStringToObject(identity, "objectID",
                                field(record, "id") ? field(record, "id") : "");
        cJSON_AddStringToObject(identity, "profile", "C:\\OS2\\OBJECTS.INI");
        cJSON_AddStringToObject(identity, "hash", field(record, "hash"));
        cJSON_AddItemToObject(identity, "folders", folders);
        const char *icon_file = field(record, "ICONFILE"),
                   *icon_resource = field(record, "ICONRESOURCE");
        if ((icon_file && *icon_file) || (icon_resource && *icon_resource))
        {
            cJSON *icon = cJSON_AddObjectToObject(next, "wpsIcon");
            if (icon_file && *icon_file)
            {
                cJSON_AddStringToObject(
                    icon, "path", wps_resolve(c, icon_file, cwd, field(environment, "OS2PATH")));
                cJSON_AddStringToObject(identity, "iconOrigin", "ICONFILE");
            }
            else
            {
                char *end;
                unsigned long resource = strtoul(icon_resource, &end, 0);
                while (*end == ',' || isspace((unsigned char)*end))
                    end++;
                if (resource <= 65535 && *end)
                {
                    char *module =
                        strchr(end, '.') ? keep(c, strdup(end)) : format(c, "%s.DLL", end);
                    cJSON_AddStringToObject(
                        icon, "path", wps_resolve(c, module, cwd, field(environment, "LIBPATH")));
                    cJSON_AddNumberToObject(icon, "resource", (double)resource);
                }
                cJSON_AddStringToObject(identity, "iconOrigin", "ICONRESOURCE");
            }
            wps_icon_beside_program(c, icon, executable);
            if (field(icon, "path"))
                cJSON_AddStringToObject(identity, "iconPath", field(icon, "path"));
            if (field(icon, "declaredPath"))
            {
                cJSON_AddStringToObject(identity, "declaredIconPath", field(icon, "declaredPath"));
                cJSON_AddStringToObject(identity, "iconFallbackReason",
                                        field(icon, "fallbackReason"));
            }
        }
        else
            cJSON_AddStringToObject(identity, "iconOrigin", "executable");
        const cJSON *previous = wps_previous(old, handle);
        int changed =
            previous && (strcmp(previous->string, name) || !cJSON_Compare(previous, next, 1));
        added += previous == NULL;
        updated += changed;
        cJSON_AddStringToObject(event, "status",
                                !previous ? "added"
                                : changed ? "updated"
                                          : "unchanged");
        cJSON_AddStringToObject(event, "name", name);
        cJSON_AddStringToObject(event, "program", executable);
        cJSON_AddItemToObject(event, "registration", cJSON_Duplicate(identity, 1));
        put(apps, name, next);
    }
    cJSON_ArrayForEach(app, old)
    {
        const char *handle = field(get(app, "wpsRegistration"), "handle");
        if (handle && !wps_previous(apps, handle))
            removed++;
    }
    int changed = !cJSON_Compare(old, apps, 1);
    if (!c->error && changed)
    {
        put(c->manifest, "applications", apps);
        apps = NULL;
        save_manifest(c);
    }
    unsigned char digest[32];
    wps_digest((unsigned char *)bytes, size, digest);
    cJSON_AddStringToObject(report, "profile", "C:\\OS2\\OBJECTS.INI");
    cJSON_AddStringToObject(report, "profileSha256", hex(c, digest));
    char *hidden = cJSON_PrintUnformatted(get(c->manifest, "wpsHidden"));
    wps_digest((unsigned char *)(hidden ? hidden : ""), hidden ? strlen(hidden) : 0, digest);
    cJSON_AddStringToObject(report, "policySha256", hex(c, digest));
    cJSON_free(hidden);
    cJSON_AddNumberToObject(report, "added", added);
    cJSON_AddNumberToObject(report, "updated", updated);
    cJSON_AddNumberToObject(report, "removed", removed);
    cJSON_AddNumberToObject(report, "skipped", skipped);
    char *report_path = join(c, c->ball, "logs/wps-registration-latest.json");
    char *previous_text = read_file(c, report_path, NULL, 1);
    cJSON *previous_report = previous_text ? cJSON_Parse(previous_text) : NULL;
    const char *previous_hash = field(previous_report, "profileSha256");
    const char *previous_policy = field(previous_report, "policySha256");
    if (!c->error && (changed || !previous_hash || !previous_policy ||
                      strcmp(previous_hash, field(report, "profileSha256")) ||
                      strcmp(previous_policy, field(report, "policySha256"))))
    {
        cJSON_AddStringToObject(report, "recorded", now(c));
        cJSON_AddStringToObject(report, "logPath", report_path);
        cJSON_AddStringToObject(
            report, "summary",
            format(c,
                   "Workplace Shell registrations: %u added, %u updated, %u removed, %u skipped.",
                   added, updated, removed, skipped));
        char *logs = join(c, c->ball, "logs");
        if (!logs || !tb_mkdirs(logs))
            fail(c, "Cannot create application registration logs directory");
        else
            write_json(c, report_path, report);
    }
    cJSON_Delete(previous_report);
    cJSON_Delete(environment);
    cJSON_Delete(report);
    cJSON_Delete(apps);
    cJSON_Delete(objects);
    return !c->error;
}
static cJSON *wps_latest(Context *c)
{
    char *path = join(c, c->ball, "logs/wps-registration-latest.json");
    char *bytes = path ? read_file(c, path, NULL, 1) : NULL;
    cJSON *report = bytes ? cJSON_Parse(bytes) : NULL;
    if (!cJSON_IsObject(report))
    {
        cJSON_Delete(report);
        return NULL;
    }
    const char *fields[] = {"summary", "logPath", "added", "updated", "removed", "skipped", NULL};
    cJSON *compact = cJSON_CreateObject();
    for (unsigned i = 0; fields[i]; i++)
        if (get(report, fields[i]))
            cJSON_AddItemToObject(compact, fields[i], cJSON_Duplicate(get(report, fields[i]), 1));
    cJSON_Delete(report);
    return compact;
}
#endif
