/* Public domain. No rights reserved. Wholly AI-authored. */
/* Binary-safe profile inspection and revision-checked value replacement. */
#ifndef TB_CONFIGURATION_H
#define TB_CONFIGURATION_H
#define TB_PROFILE_LIMIT (32u * 1024u * 1024u)

static uint32_t configuration_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void configuration_store(unsigned char *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

static char *configuration_hex(Context *c, const unsigned char *p, size_t n)
{
    char *out = keep(c, malloc(n * 2 + 1));
    if (out)
    {
        for (size_t i = 0; i < n; i++)
            snprintf(out + i * 2, 3, "%02x", p[i]);
        out[n * 2] = 0;
    }
    return out;
}

static int configuration_nibble(char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

static unsigned char *configuration_unhex(Context *c, const char *text, size_t *n)
{
    if (!text || strlen(text) % 2 || strlen(text) > TB_PROFILE_LIMIT * 2)
    {
        fail(c, "Use pairs of hexadecimal digits");
        return NULL;
    }
    *n = strlen(text) / 2;
    unsigned char *out = (unsigned char *)keep(c, malloc(*n + 1));
    if (!out)
        return NULL;
    for (size_t i = 0; i < *n; i++)
    {
        int a = configuration_nibble(text[i * 2]), b = configuration_nibble(text[i * 2 + 1]);
        if (a < 0 || b < 0)
        {
            fail(c, "Use pairs of hexadecimal digits");
            return NULL;
        }
        out[i] = (unsigned char)(a * 16 + b);
    }
    return out;
}

static int configuration_entry(Context *c, cJSON *entries, const unsigned char *app, size_t a,
                               const unsigned char *key, size_t k, const unsigned char *data,
                               size_t n)
{
    if (!a || !k || app[a - 1] || key[k - 1] || memchr(app, 0, a - 1) || memchr(key, 0, k - 1))
        return fail(c, "Invalid profile name record");
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "application", latin1_to_utf8(c, (const char *)app, a - 1));
    cJSON_AddStringToObject(entry, "key", latin1_to_utf8(c, (const char *)key, k - 1));
    cJSON_AddStringToObject(entry, "data", configuration_hex(c, data, n));
    cJSON_AddItemToArray(entries, entry);
    return !c->error;
}

static int configuration_native_string(const unsigned char *p, size_t n, uint32_t at, uint32_t size)
{
    return size && at <= n && size <= n - at && !p[at + size - 1] && !memchr(p + at, 0, size - 1);
}

static int configuration_mark(unsigned char *seen, size_t n, uint32_t at, uint32_t bytes)
{
    if (at < 20 || at > n || bytes > n - at || (seen[at / 8] & (1u << (at % 8))))
        return 0;
    seen[at / 8] |= 1u << (at % 8);
    return 1;
}

static cJSON *configuration_parse(Context *c, const unsigned char *p, size_t n)
{
    cJSON *entries = cJSON_CreateArray();
    if (!n)
        return entries;
    if (n >= 20 && configuration_u32(p) == UINT32_MAX)
    {
        unsigned char *seen = (unsigned char *)keep(c, calloc((n + 7) / 8, 1));
        if (!seen || configuration_u32(p + 8) > n)
            goto invalid;
        uint32_t app = configuration_u32(p + 4);
        while (app)
        {
            if (!configuration_mark(seen, n, app, 20))
                goto invalid;
            uint32_t name = configuration_u32(p + app + 16),
                     a = p[app + 12] | (uint32_t)p[app + 13] << 8;
            if (!configuration_native_string(p, n, name, a))
                goto invalid;
            uint32_t key = configuration_u32(p + app + 4);
            while (key)
            {
                if (!configuration_mark(seen, n, key, 24))
                    goto invalid;
                uint32_t kn = configuration_u32(p + key + 12),
                         k = p[key + 8] | (uint32_t)p[key + 9] << 8;
                uint32_t data = configuration_u32(p + key + 20),
                         bytes = p[key + 16] | (uint32_t)p[key + 17] << 8;
                if (!configuration_native_string(p, n, kn, k) || data > n || bytes > n - data ||
                    !configuration_entry(c, entries, p + name, a, p + kn, k, p + data, bytes))
                    goto invalid;
                key = configuration_u32(p + key);
            }
            app = configuration_u32(p + app);
        }
        return entries;
    }
    if (n < 12 || memcmp(p, "2INEPRF1", 8))
        goto invalid;
    uint32_t count = configuration_u32(p + 8);
    size_t at = 12;
    while (count--)
    {
        if (n - at < 12)
            goto invalid;
        uint32_t a = configuration_u32(p + at), k = configuration_u32(p + at + 4),
                 bytes = configuration_u32(p + at + 8);
        at += 12;
        if (!a || !k || a > 65536 || k > 65536 || a > n - at || k > n - at - a ||
            bytes > n - at - a - k ||
            !configuration_entry(c, entries, p + at, a, p + at + a, k, p + at + a + k, bytes))
            goto invalid;
        at += a + k + bytes;
    }
    if (at == n)
        return entries;
invalid:
    cJSON_Delete(entries);
    fail(c, "Profile has an invalid or unsupported binary format; it was not modified");
    return NULL;
}

#define TB_CONFIGURATION_FILES_LIMIT 2048u
#define TB_CONFIGURATION_SCAN_LIMIT 100000u
#define TB_CONFIGURATION_DIRECTORY_LIMIT 4096u
#define TB_CONFIGURATION_GUEST_SEPARATOR 92

static int configuration_part_equal(const char *a, size_t n, const char *b)
{
    if (strlen(b) != n)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

static int configuration_excluded(const char *guest)
{
    const char *part = guest + 3;
    if (strlen(part) >= 12 && configuration_part_equal(part, 12, "OS2/DLL/2INE") &&
        (!part[12] || part[12] == '/'))
        return 1;
    while (*part)
    {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (configuration_part_equal(part, n, "TEMP") || configuration_part_equal(part, n, "TMP"))
            return 1;
        part = end ? end + 1 : part + n;
    }
    return 0;
}

static int configuration_links(Context *c, const char *root, const char *path)
{
    if (!inside(path, root))
        return fail(c, "Choose an existing INI file on the ball's private C: drive");
    char *copy = keep(c, strdup(path));
    if (!copy)
        return 0;
    for (size_t i = strlen(root);; i++)
        if (!copy[i] || copy[i] == '/' || copy[i] == TB_CONFIGURATION_GUEST_SEPARATOR)
        {
            char saved = copy[i];
            copy[i] = 0;
            int link = tb_is_link(copy);
            copy[i] = saved;
            if (link)
                return fail(c, "Symbolic links and redirected directories cannot be edited here");
            if (!saved)
                break;
        }
    return 1;
}

static char *configuration_private_root(Context *c)
{
    char *ball = keep(c, tb_canonical(c->ball));
    const char *mapping = str(get(get(c->manifest, "drives"), "C"));
    const char *raw = mapping ? host_absolute(mapping) ? mapping : join(c, ball, mapping) : NULL;
    if (!ball || !raw || tb_is_link(c->ball) || !c->roots[2] || !inside(c->roots[2], ball) ||
        !configuration_links(c, ball, raw))
    {
        fail(c, "C: must be a private directory inside this ball");
        return NULL;
    }
    return c->roots[2];
}

static int configuration_suffix(const char *name)
{
    size_t n = strlen(name);
    return n >= 4 && equal_case(name + n - 4, ".INI");
}

static char *configuration_private_path(Context *c, const char *guest)
{
    char *root = configuration_private_root(c);
    if (!root)
        return NULL;
    if (!guest || toupper((unsigned char)guest[0]) != 'C' || guest[1] != ':' ||
        (guest[2] != TB_CONFIGURATION_GUEST_SEPARATOR && guest[2] != '/') || strlen(guest) > 32768)
    {
        fail(c, "Choose an existing INI file on the ball's private C: drive");
        return NULL;
    }
    char *normalized = keep(c, strdup(guest));
    if (!normalized)
        return NULL;
    for (char *p = normalized; *p; p++)
        if (*p == TB_CONFIGURATION_GUEST_SEPARATOR)
            *p = '/';
    if ((!configuration_suffix(normalized) && !equal_case(normalized, "C:/CONFIG.SYS")) ||
        configuration_excluded(normalized))
    {
        fail(c, "Choose an INI file or CONFIG.SYS outside temporary and runtime-cache directories");
        return NULL;
    }
    char *path = root, *at = normalized + 3;
    while (*at)
    {
        char *end = strchr(at, '/');
        if (end)
            *end = 0;
        if (!*at || !strcmp(at, ".") || !strcmp(at, "..") || strpbrk(at, ":*?\r\n"))
        {
            fail(c, "Invalid private C: configuration path");
            return NULL;
        }
        char *candidate = join(c, path, at);
        if (!candidate)
            return NULL;
        if (tb_is_link(candidate))
        {
            fail(c, "Symbolic links cannot be edited here");
            return NULL;
        }
        if (!tb_is_file(candidate) && !tb_is_dir(candidate))
        {
            size_t count = 0;
            char **names = tb_names(path, &count);
            char *found = NULL;
            for (size_t i = 0; i < count; i++)
                if (equal_case(names[i], at))
                {
                    found = join(c, path, names[i]);
                    break;
                }
            tb_names_free(names, count);
            candidate = found;
        }
        if (!candidate || tb_is_link(candidate) ||
            (end ? !tb_is_dir(candidate) : !tb_is_file(candidate)))
        {
            fail(c, "Configuration file is missing, redirected or not a regular file");
            return NULL;
        }
        path = candidate;
        if (!end)
            break;
        at = end + 1;
        if (!*at)
        {
            fail(c, "An existing file is required");
            return NULL;
        }
    }
    char *canonical = keep(c, tb_canonical(path));
    if (!canonical || !inside(canonical, root) || !configuration_links(c, root, path))
    {
        fail(c, "Configuration path leaves the private C: drive");
        return NULL;
    }
    return canonical;
}

static int configuration_is_text(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((p[i] < 32 && p[i] != '\t' && p[i] != '\r' && p[i] != '\n') || p[i] == 127)
            return 0;
    return 1;
}

static unsigned char *configuration_file_bytes(Context *c, const char *path, size_t *length)
{
    uint64_t size = 0;
    int64_t modified = 0;
    if (!tb_path_info(path, &size, &modified) || size > TB_PROFILE_LIMIT)
    {
        fail(c, "Configuration file exceeds 32 MiB or cannot be read");
        return NULL;
    }
    return (unsigned char *)read_file(c, path, length, 0);
}

static char *configuration_path(Context *c, const char *area)
{
    if (!area)
        return NULL;
    if (!strcmp(area, "registry"))
        return join(c, c->roots[2], "OS2/REGISTRY.INI");
    if (strcmp(area, "user") && strcmp(area, "system"))
        return NULL;
    cJSON *env = guest_environment(c, NULL);
    if (!env)
        return NULL;
    const char *guest = field(env, !strcmp(area, "user") ? "USER_INI" : "SYSTEM_INI");
    char *path = guest ? host_path(c, guest, 0) : NULL;
    cJSON_Delete(env);
    return path;
}

static cJSON *configuration_read(Context *c, const cJSON *request)
{
    const char *area = field(request, "name");
    char *path = area && !strcmp(area, "file")
                     ? configuration_private_path(c, field(request, "file"))
                     : configuration_path(c, area);
    if (!path)
    {
        fail(c, "Choose a user profile, system profile or registry");
        return NULL;
    }
    size_t n = 0;
    unsigned char *bytes = (unsigned char *)read_file(c, path, &n, 1);
    if (!bytes || n > TB_PROFILE_LIMIT)
    {
        fail(c, "Profile exceeds 32 MiB");
        return NULL;
    }
    cJSON *entries = configuration_parse(c, bytes, n);
    if (!entries)
        return NULL;
    SHA256_CTX state;
    unsigned char digest[32];
    sha256_init(&state);
    sha256_update(&state, bytes, n);
    sha256_final(&state, digest);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "area", area);
    cJSON_AddStringToObject(out, "path", path);
    cJSON_AddStringToObject(out, "revision", configuration_hex(c, digest, sizeof digest));
    cJSON_AddStringToObject(out, "format",
                            !n                              ? "empty"
                            : !memcmp(bytes, "2INEPRF1", 8) ? "2INEPRF1"
                                                            : "OS/2 INI");
    cJSON_AddItemToObject(out, "entries", entries);
    return out;
}

static int configuration_serialize(Context *c, const char *path, cJSON *entries)
{
    size_t total = 12;
    cJSON *entry;
    cJSON_ArrayForEach(entry, entries)
    {
        size_t a = 0, k = 0;
        if (!utf8_to_latin1(c, field(entry, "application"), &a) ||
            !utf8_to_latin1(c, field(entry, "key"), &k))
            return 0;
        size_t bytes = strlen(field(entry, "data")) / 2;
        if (a >= 65536 || k >= 65536 || a + k + bytes + 14 > TB_PROFILE_LIMIT - total)
            return fail(c, "Profile exceeds format limits");
        total += 14 + a + k + bytes;
    }
    unsigned char *out = (unsigned char *)keep(c, malloc(total));
    if (!out)
        return 0;
    memcpy(out, "2INEPRF1", 8);
    configuration_store(out + 8, (uint32_t)cJSON_GetArraySize(entries));
    size_t at = 12;
    cJSON_ArrayForEach(entry, entries)
    {
        size_t a = 0, k = 0, n = 0;
        char *app = utf8_to_latin1(c, field(entry, "application"), &a),
             *key = utf8_to_latin1(c, field(entry, "key"), &k);
        unsigned char *bytes = configuration_unhex(c, field(entry, "data"), &n);
        if (!app || !key || !bytes)
            return 0;
        configuration_store(out + at, (uint32_t)a + 1);
        configuration_store(out + at + 4, (uint32_t)k + 1);
        configuration_store(out + at + 8, (uint32_t)n);
        at += 12;
        memcpy(out + at, app, a + 1);
        at += a + 1;
        memcpy(out + at, key, k + 1);
        at += k + 1;
        memcpy(out + at, bytes, n);
        at += n;
    }
    return tb_atomic_write(path, out, total) ? 1
                                             : fail(c, "Cannot save profile: %s", strerror(errno));
}

static cJSON *configuration_write(Context *c, const cJSON *request)
{
    cJSON *current = configuration_read(c, request);
    if (!current)
        return NULL;
    const char *expected = field(request, "expected"), *app = field(request, "path"),
               *key = field(request, "key"), *text = field(request, "text");
    int remove = cJSON_IsTrue(get(request, "remove"));
    if (!expected || strcmp(expected, field(current, "revision")))
    {
        cJSON_Delete(current);
        fail(c,
             "Configuration changed on disk. Reload before saving; your draft has been preserved.");
        return NULL;
    }
    if (!app || !*app || !key || !*key || (!remove && !text))
    {
        cJSON_Delete(current);
        fail(c, "Application/key names and value are required");
        return NULL;
    }
    size_t n = 0;
    unsigned char *data = remove ? NULL : configuration_unhex(c, text, &n);
    if (!remove && !data)
    {
        cJSON_Delete(current);
        return NULL;
    }
    int registry = !strcmp(field(request, "name"), "registry");
    if (registry)
    {
        char *normalized_app = keep(c, strdup(app)), *normalized_key = keep(c, strdup(key));
        if (!normalized_app || !normalized_key)
        {
            cJSON_Delete(current);
            return NULL;
        }
        for (char *p = normalized_app; *p; p++)
            if (*p >= 'A' && *p <= 'Z')
                *p += 'a' - 'A';
        for (char *p = normalized_key; *p; p++)
            if (*p >= 'A' && *p <= 'Z')
                *p += 'a' - 'A';
        app = normalized_app;
        key = normalized_key;
        size_t root = !strncmp(app, "hku", 3) && (app[3] == '\\' || !app[3]) ? 3 : 4;
        int valid_root = (root == 3 || !strncmp(app, "hkcr", 4) || !strncmp(app, "hkcu", 4) ||
                          !strncmp(app, "hklm", 4)) &&
                         (!app[root] || app[root] == '\\');
        if (strncmp(key, "v:", 2) || !valid_root || strlen(app) > 32767 || strlen(key) > 32769 ||
            strstr(app, "\\\\") || app[strlen(app) - 1] == '\\' ||
            (!remove && (n < 4 || (configuration_u32(data) == 4 && n != 8))))
        {
            cJSON_Delete(current);
            fail(
                c,
                "Registry edits require a root/key path and a typed v: value record; DWORD values require four data bytes");
            return NULL;
        }
    }
    cJSON *entries = get(current, "entries"), *entry;
    int index = 0, found = 0;
    cJSON_ArrayForEach(entry, entries)
    {
        if (!strcmp(field(entry, "application"), app) && !strcmp(field(entry, "key"), key))
        {
            found = 1;
            break;
        }
        index++;
    }
    if (found && remove)
        cJSON_DeleteItemFromArray(entries, index);
    else if (!remove)
    {
        if (!found)
        {
            entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "application", app);
            cJSON_AddStringToObject(entry, "key", key);
            cJSON_AddItemToArray(entries, entry);
        }
        put(entry, "data", cJSON_CreateString(text));
        if (registry)
        {
            char *prefix = keep(c, strdup(app));
            if (!prefix)
            {
                cJSON_Delete(current);
                return NULL;
            }
            for (size_t offset = 0;; offset++)
            {
                if (prefix[offset] && prefix[offset] != '\\')
                    continue;
                char saved = prefix[offset];
                prefix[offset] = 0;
                int has_key = 0;
                cJSON *record;
                cJSON_ArrayForEach(record,
                                   entries) if (!strcmp(field(record, "application"), prefix) &&
                                                !strcmp(field(record, "key"), "k:")) has_key = 1;
                if (!has_key)
                {
                    const char *names[] = {"k:", "c:", "t:"};
                    unsigned char timestamp[8];
                    uint64_t ticks =
                        ((uint64_t)time(NULL) + UINT64_C(11644473600)) * UINT64_C(10000000);
                    configuration_store(timestamp, (uint32_t)ticks);
                    configuration_store(timestamp + 4, (uint32_t)(ticks >> 32));
                    for (size_t i = 0; i < 3; i++)
                    {
                        cJSON *marker = cJSON_CreateObject();
                        cJSON_AddStringToObject(marker, "application", prefix);
                        cJSON_AddStringToObject(marker, "key", names[i]);
                        cJSON_AddStringToObject(marker, "data",
                                                i == 2 ? configuration_hex(c, timestamp, 8) : "00");
                        cJSON_AddItemToArray(entries, marker);
                    }
                }
                prefix[offset] = saved;
                if (!saved)
                    break;
            }
        }
    }

    if (registry)
    {
        unsigned char timestamp[8];
        uint64_t ticks = ((uint64_t)time(NULL) + UINT64_C(11644473600)) * UINT64_C(10000000);
        configuration_store(timestamp, (uint32_t)ticks);
        configuration_store(timestamp + 4, (uint32_t)(ticks >> 32));
        cJSON *stamp = NULL;
        cJSON_ArrayForEach(entry, entries) if (!strcmp(field(entry, "application"), app) &&
                                               !strcmp(field(entry, "key"), "t:")) stamp = entry;
        if (!stamp)
        {
            stamp = cJSON_CreateObject();
            cJSON_AddStringToObject(stamp, "application", app);
            cJSON_AddStringToObject(stamp, "key", "t:");
            cJSON_AddItemToArray(entries, stamp);
        }
        put(stamp, "data", cJSON_CreateString(configuration_hex(c, timestamp, 8)));
    }
    int ok = configuration_serialize(c, field(current, "path"), entries);
    cJSON_Delete(current);
    return ok ? configuration_read(c, request) : NULL;
}

static char *configuration_guest_file(Context *c, const char *host)
{
    const char *part = host + strlen(c->roots[2]);
    while (*part == '/' || *part == TB_CONFIGURATION_GUEST_SEPARATOR)
        part++;
    char *guest = format(c, "C:%c%s", TB_CONFIGURATION_GUEST_SEPARATOR, part);
    if (guest)
        for (char *p = guest + 3; *p; p++)
            if (*p == '/')
                *p = TB_CONFIGURATION_GUEST_SEPARATOR;
    return guest;
}

static cJSON *configuration_aliases(Context *c)
{
    cJSON *aliases = cJSON_CreateArray(), *env = guest_environment(c, NULL);
    if (!env)
    {
        cJSON_Delete(aliases);
        return NULL;
    }
    const char *roles[] = {"config", "user", "system", "registry"};
    const char *paths[] = {"C:/CONFIG.SYS", field(env, "USER_INI"), field(env, "SYSTEM_INI"),
                           "C:/OS2/REGISTRY.INI"};
    for (unsigned i = 0; i < 4; i++)
    {
        if (!paths[i])
            continue;
        char *host = host_path(c, paths[i], 0);
        if (c->error)
        {
            if (!strcmp(c->error, "Out of memory"))
            {
                cJSON_Delete(env);
                cJSON_Delete(aliases);
                return NULL;
            }
            free(c->error);
            c->error = NULL;
        }
        cJSON *alias = cJSON_CreateObject();
        cJSON_AddStringToObject(alias, "role", roles[i]);
        cJSON_AddStringToObject(alias, "host", host ? host : paths[i]);
        cJSON_AddStringToObject(
            alias, "path",
            host && inside(host, c->roots[2]) ? configuration_guest_file(c, host) : paths[i]);
        cJSON_AddBoolToObject(alias, "exists", host && tb_is_file(host));
        cJSON_AddItemToArray(aliases, alias);
    }
    cJSON_Delete(env);
    return aliases;
}

static cJSON *configuration_roles(const cJSON *aliases, const char *host)
{
    cJSON *roles = cJSON_CreateArray();
    const cJSON *alias;
    cJSON_ArrayForEach(alias, aliases) if (equal_case(field(alias, "host"), host))
        cJSON_AddItemToArray(roles, cJSON_CreateString(field(alias, "role")));
    return roles;
}

static int configuration_profile_magic(const unsigned char *p, size_t n)
{
    return (n >= 8 && !memcmp(p, "2INEPRF1", 8)) || (n >= 4 && configuration_u32(p) == UINT32_MAX);
}

static cJSON *configuration_file_read(Context *c, const cJSON *request)
{
    char *path = configuration_private_path(c, field(request, "path"));
    size_t n = 0;
    unsigned char *bytes = path ? configuration_file_bytes(c, path, &n) : NULL;
    if (!bytes)
        return NULL;
    cJSON *aliases = configuration_aliases(c);
    if (!aliases)
        return NULL;
    cJSON *roles = configuration_roles(aliases, path), *entries = NULL;
    cJSON_Delete(aliases);
    int profile = configuration_profile_magic(bytes, n) ||
                  (!n && cJSON_GetArraySize(roles) &&
                   !equal_case(str(cJSON_GetArrayItem(roles, 0)), "config"));
    const char *reason = "", *kind = "binary", *format = "Unsupported binary";
    if (profile)
    {
        entries = configuration_parse(c, bytes, n);
        if (entries)
        {
            kind = "profile";
            format = !n                                        ? "empty"
                     : n >= 8 && !memcmp(bytes, "2INEPRF1", 8) ? "2INEPRF1"
                                                               : "OS/2 INI";
        }
        else if (c->error)
        {
            if (!strcmp(c->error, "Out of memory"))
            {
                cJSON_Delete(roles);
                return NULL;
            }
            reason = keep(c, c->error);
            c->error = NULL;
        }
    }
    else if (configuration_is_text(bytes, n))
    {
        kind = "text";
        format = "Latin-1 text";
    }
    if (!strcmp(kind, "binary") && !*reason)
        reason = "Unsupported binary format; bytes are read-only";
    unsigned char digest[32];
    SHA256_CTX hash;
    sha256_init(&hash);
    sha256_update(&hash, bytes, n);
    sha256_final(&hash, digest);
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "area", "file");
    cJSON_AddStringToObject(out, "path", configuration_guest_file(c, path));
    cJSON_AddStringToObject(out, "revision", configuration_hex(c, digest, sizeof digest));
    cJSON_AddStringToObject(out, "kind", kind);
    cJSON_AddStringToObject(out, "format", format);
    cJSON_AddBoolToObject(out, "editable", strcmp(kind, "binary") != 0);
    cJSON_AddStringToObject(out, "reason", reason);
    cJSON_AddNumberToObject(out, "size", (double)n);
    cJSON_AddItemToObject(out, "roles", roles);
    cJSON_AddStringToObject(
        out, "role", cJSON_GetArraySize(roles) ? str(cJSON_GetArrayItem(roles, 0)) : "other");
    if (entries)
        cJSON_AddItemToObject(out, "entries", entries);
    else if (!strcmp(kind, "text"))
        cJSON_AddStringToObject(out, "text", latin1_to_utf8(c, (char *)bytes, n));
    else
        cJSON_AddStringToObject(out, "data", configuration_hex(c, bytes, n));
    return out;
}

static cJSON *configuration_file_write(Context *c, const cJSON *request)
{
    cJSON *current = configuration_file_read(c, request);
    if (!current)
        return NULL;
    const char *expected = field(request, "expected"), *guest = field(current, "path");
    if (!expected || strcmp(expected, field(current, "revision")))
    {
        cJSON_Delete(current);
        fail(c,
             "Configuration changed on disk. Reload before saving; your draft has been preserved.");
        return NULL;
    }
    if (!cJSON_IsTrue(get(current, "editable")))
    {
        cJSON_Delete(current);
        fail(c, "Unsupported binary files are read-only and were not modified");
        return NULL;
    }
    if (!strcmp(field(current, "kind"), "profile"))
    {
        cJSON *change = cJSON_CreateObject();
        cJSON_AddStringToObject(change, "name", "file");
        cJSON_AddStringToObject(change, "file", guest);
        const char *keys[] = {"expected", "key", "remove", NULL};
        for (unsigned i = 0; keys[i]; i++)
            if (get(request, keys[i]))
                cJSON_AddItemToObject(change, keys[i], cJSON_Duplicate(get(request, keys[i]), 1));
        if (get(request, "application"))
            cJSON_AddItemToObject(change, "path", cJSON_Duplicate(get(request, "application"), 1));
        const cJSON *data = get(request, "text") ? get(request, "text") : get(request, "data");
        if (data)
            cJSON_AddItemToObject(change, "text", cJSON_Duplicate(data, 1));
        cJSON *saved = configuration_write(c, change);
        cJSON_Delete(change);
        cJSON_Delete(current);
        if (!saved)
            return NULL;
        cJSON_Delete(saved);
        return configuration_file_read(c, request);
    }
    const char *text = field(request, "text");
    size_t size = 0;
    char *bytes = text ? utf8_to_latin1(c, text, &size) : NULL;
    char *host = configuration_private_path(c, guest);
    if (!text)
        fail(c, "Text is required");
    if (bytes && (size > TB_PROFILE_LIMIT || !configuration_is_text((unsigned char *)bytes, size)))
        fail(c, "Use plain Latin-1 text without binary control bytes");
    int ok = bytes && host && !c->error && tb_atomic_write(host, bytes, size);
    cJSON_Delete(current);
    if (!ok)
    {
        fail(c, "Cannot save configuration file");
        return NULL;
    }
    return configuration_file_read(c, request);
}

static int configuration_same_path(const char *a, const char *b)
{
    while (*a && *b)
    {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x == TB_CONFIGURATION_GUEST_SEPARATOR)
            x = '/';
        if (y == TB_CONFIGURATION_GUEST_SEPARATOR)
            y = '/';
        if (tolower(x) != tolower(y))
            return 0;
    }
    return !*a && !*b;
}

static int configuration_file_catalogue(Context *c, cJSON *files, const cJSON *aliases,
                                        const char *host)
{
    char *guest = configuration_guest_file(c, host);
    const cJSON *entry;
    cJSON_ArrayForEach(entry,
                       files) if (configuration_same_path(field(entry, "path"), guest)) return 1;
    uint64_t size = 0;
    int64_t modified = 0;
    if (!tb_path_info(host, &size, &modified))
        return 0;
    cJSON *roles = configuration_roles(aliases, host);
    const char *kind = "binary", *reason = "", *format = "Unsupported binary";
    int editable = 0;
    FILE *file = size <= TB_PROFILE_LIMIT ? tb_fopen(host, "rb") : NULL;
    unsigned char *bytes = file ? malloc((size_t)size + 1) : NULL;
    int valid = bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size && !ferror(file);
    if (file)
        fclose(file);
    if (valid)
    {
        int profile = configuration_profile_magic(bytes, (size_t)size) ||
                      (!size && cJSON_GetArraySize(roles) &&
                       !equal_case(str(cJSON_GetArrayItem(roles, 0)), "config"));
        if (profile)
        {
            size_t allocations = c->count;
            cJSON *parsed = configuration_parse(c, bytes, (size_t)size);
            if (parsed)
            {
                kind = "profile";
                editable = 1;
                format = !size                                        ? "empty"
                         : size >= 8 && !memcmp(bytes, "2INEPRF1", 8) ? "2INEPRF1"
                                                                      : "OS/2 INI";
            }
            cJSON_Delete(parsed);
            while (c->count > allocations)
                free(c->allocations[--c->count]);
            if (c->error)
            {
                free(c->error);
                c->error = NULL;
            }
        }
        else if (configuration_is_text(bytes, (size_t)size))
        {
            kind = "text";
            editable = 1;
            format = "Latin-1 text";
        }
    }
    free(bytes);
    if (!editable)
        reason = size > TB_PROFILE_LIMIT
                     ? "File exceeds the 32 MiB editor limit"
                     : "Unsupported or unreadable binary; open for read-only inspection";
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "path", guest);
    const char *name = guest + 3;
    for (const char *p = name; *p; p++)
        if (*p == TB_CONFIGURATION_GUEST_SEPARATOR)
            name = p + 1;
    cJSON_AddStringToObject(out, "label", name);
    cJSON_AddStringToObject(out, "kind", kind);
    cJSON_AddStringToObject(out, "format", format);
    cJSON_AddStringToObject(
        out, "role", cJSON_GetArraySize(roles) ? str(cJSON_GetArrayItem(roles, 0)) : "other");
    cJSON_AddItemToObject(out, "roles", roles);
    cJSON_AddNumberToObject(out, "size", (double)size);
    cJSON_AddBoolToObject(out, "exists", 1);
    cJSON_AddBoolToObject(out, "editable", editable);
    cJSON_AddStringToObject(out, "reason", reason);
    cJSON_AddItemToArray(files, out);
    return 1;
}

static cJSON *configuration_files(Context *c, const cJSON *request)
{
    char *root = configuration_private_root(c);
    if (!root)
        return NULL;
    unsigned limit = TB_CONFIGURATION_FILES_LIMIT;
    const cJSON *chosen = get(request, "limit");
    if (chosen)
    {
        if (!cJSON_IsNumber(chosen) || chosen->valuedouble < 1 || chosen->valuedouble > limit ||
            chosen->valuedouble != (unsigned)chosen->valuedouble)
        {
            fail(c, "Configuration file limit must be between 1 and 2048");
            return NULL;
        }
        limit = (unsigned)chosen->valuedouble;
    }
    cJSON *aliases = configuration_aliases(c);
    if (!aliases)
        return NULL;
    cJSON *out = cJSON_CreateObject(), *files = cJSON_AddArrayToObject(out, "files"),
          *queue = cJSON_CreateArray();
    const cJSON *alias;
    unsigned skipped = 0, visited = 0;
    int truncated = 0;
    cJSON_ArrayForEach(alias, aliases)
    {
        const char *host = field(alias, "host"), *guest = field(alias, "path");
        int present = 0;
        const cJSON *prior;
        cJSON_ArrayForEach(prior, files) if (configuration_same_path(field(prior, "path"), guest))
            present = 1;
        if (present)
            continue;
        if ((unsigned)cJSON_GetArraySize(files) >= limit)
        {
            truncated = 1;
            break;
        }
        if (cJSON_IsTrue(get(alias, "exists")) && inside(host, root))
        {
            char *safe = configuration_private_path(c, guest);
            if (safe && configuration_file_catalogue(c, files, aliases, safe))
                continue;
            if (c->error)
            {
                free(c->error);
                c->error = NULL;
            }
            skipped++;
            continue;
        }
        if (!strcmp(field(alias, "role"), "config"))
            continue;
        cJSON *entry = cJSON_CreateObject(), *roles = configuration_roles(aliases, host);
        cJSON_AddStringToObject(entry, "path", guest);
        const char *name = guest;
        for (const char *p = guest; *p; p++)
            if (*p == '/' || *p == TB_CONFIGURATION_GUEST_SEPARATOR)
                name = p + 1;
        cJSON_AddStringToObject(entry, "label", name);
        cJSON_AddStringToObject(entry, "role", str(cJSON_GetArrayItem(roles, 0)));
        cJSON_AddItemToObject(entry, "roles", roles);
        cJSON_AddStringToObject(entry, "kind", "profile");
        cJSON_AddStringToObject(entry, "format",
                                cJSON_IsTrue(get(alias, "exists")) ? "Profile" : "empty");
        cJSON_AddNumberToObject(entry, "size", 0);
        cJSON_AddBoolToObject(entry, "exists", cJSON_IsTrue(get(alias, "exists")));
        cJSON_AddBoolToObject(entry, "editable", 1);
        cJSON_AddStringToObject(entry, "reason",
                                cJSON_IsTrue(get(alias, "exists"))
                                    ? "Configured profile; use its named profile editor"
                                    : "Created on first save");
        cJSON_AddItemToArray(files, entry);
    }
    cJSON *first = cJSON_CreateObject();
    cJSON_AddStringToObject(first, "host", root);
    cJSON_AddNumberToObject(first, "depth", 0);
    cJSON_AddItemToArray(queue, first);
    for (int at = 0; at < cJSON_GetArraySize(queue) && !truncated; at++)
    {
        const cJSON *directory = cJSON_GetArrayItem(queue, at);
        const char *host = field(directory, "host");
        unsigned depth = (unsigned)get(directory, "depth")->valuedouble;
        size_t count = 0;
        char **names = tb_names(host, &count);
        if (!names)
        {
            skipped++;
            continue;
        }
        if (count > 1)
            qsort(names, count, sizeof *names, compare_names);
        for (size_t i = 0; i < count; i++)
        {
            if (++visited > TB_CONFIGURATION_SCAN_LIMIT)
            {
                truncated = 1;
                break;
            }
            char *child = join(c, host, names[i]),
                 *guest = child ? configuration_guest_file(c, child) : NULL;
            if (!child || !guest)
            {
                truncated = 1;
                break;
            }
            for (char *p = guest; *p; p++)
                if (*p == TB_CONFIGURATION_GUEST_SEPARATOR)
                    *p = '/';
            if (tb_is_link(child) || configuration_excluded(guest))
            {
                skipped++;
                continue;
            }
            if (tb_is_dir(child))
            {
                if (depth >= 64 || cJSON_GetArraySize(queue) >= TB_CONFIGURATION_DIRECTORY_LIMIT)
                {
                    truncated = 1;
                    skipped++;
                    continue;
                }
                cJSON *next = cJSON_CreateObject();
                cJSON_AddStringToObject(next, "host", child);
                cJSON_AddNumberToObject(next, "depth", depth + 1);
                cJSON_AddItemToArray(queue, next);
            }
            else if (configuration_suffix(names[i]) || equal_case(guest, "C:/CONFIG.SYS"))
            {
                int present = 0;
                const cJSON *prior;
                cJSON_ArrayForEach(prior, files) if (configuration_same_path(field(prior, "path"),
                                                                             guest)) present = 1;
                if (present)
                    continue;
                if ((unsigned)cJSON_GetArraySize(files) >= limit)
                {
                    truncated = 1;
                    break;
                }
                if (!configuration_file_catalogue(c, files, aliases, child))
                    skipped++;
            }
        }
        tb_names_free(names, count);
    }
    cJSON_AddBoolToObject(out, "truncated", truncated);
    cJSON_AddNumberToObject(out, "limit", limit);
    cJSON_AddNumberToObject(out, "skipped", skipped);
    cJSON_AddStringToObject(out, "reason",
                            truncated ? "Discovery limit reached; this list is incomplete" : "");
    cJSON_Delete(queue);
    cJSON_Delete(aliases);
    return out;
}
#endif
