/* Public domain. No rights reserved. Wholly AI-authored. */
/* Add installed component directories to OS/2's configuration search lists. */
static int os2_path_equal(const char *first, size_t length, const char *second)
{
    while (length && isspace((unsigned char)*first))
        first++, length--;
    while (length && isspace((unsigned char)first[length - 1]))
        length--;
    if (length > 1 && *first == '"' && first[length - 1] == '"')
        first++, length -= 2;
    while (length > 3 && (first[length - 1] == '\\' || first[length - 1] == '/'))
        length--;
    if (strlen(second) != length)
        return 0;
    for (size_t i = 0; i < length; i++)
    {
        char a = first[i] == '/' ? '\\' : (char)toupper((unsigned char)first[i]);
        char b = second[i] == '/' ? '\\' : (char)toupper((unsigned char)second[i]);
        if (a != b)
            return 0;
    }
    return 1;
}
static char *os2_path_append(Context *c, const char *value, const char *directory)
{
    if (!value)
        value = "";
    for (const char *part = value; *part;)
    {
        const char *end = strchr(part, ';');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (os2_path_equal(part, n, directory))
            return (char *)value;
        part += n;
        if (*part)
            part++;
    }
    size_t n = strlen(value);
    return format(c, "%s%s%s", value, n && value[n - 1] != ';' ? ";" : "", directory);
}
static void os2_path_want(Context *c, cJSON *wanted, const char *key, const char *directory)
{
    char *value = os2_path_append(c, field(wanted, key), directory);
    if (value)
        put(wanted, key, cJSON_CreateString(value));
}
static int os2_component_directory(const char *directory)
{
    static const char *normal[] = {"C:\\OS2",
                                   "C:\\OS2\\SYSTEM",
                                   "C:\\OS2\\INSTALL",
                                   "C:\\OS2\\APPS",
                                   "C:\\OS2\\APPS\\DLL",
                                   "C:\\OS2\\DLL",
                                   "C:\\OS2\\HELP",
                                   "C:\\OS2\\BOOK",
                                   "C:\\OS2\\BITMAP",
                                   "C:\\OS2\\MDOS",
                                   "C:\\OS2\\MDOS\\WINOS2",
                                   "C:\\OS2\\MDOS\\WINOS2\\SYSTEM",
                                   "C:\\MMOS2",
                                   "C:\\MMOS2\\DLL",
                                   "C:\\MMOS2\\HELP",
                                   "C:\\MMOS2\\INSTALL",
                                   "C:\\TCPIP\\BIN",
                                   "C:\\TCPIP\\DLL",
                                   "C:\\TCPIP\\HELP",
                                   "C:\\TCPIP\\BOOK",
                                   "C:\\MPTN\\BIN",
                                   "C:\\MPTN\\DLL",
                                   "C:\\MPTN\\HELP",
                                   "C:\\MPTN\\BOOK",
                                   "C:\\IBMLAN\\NETPROG",
                                   "C:\\IBMLAN\\NETLIB",
                                   "C:\\IBMLAN\\HELP",
                                   "C:\\IBMLAN\\BOOK",
                                   "C:\\IBMCOM",
                                   "C:\\IBMCOM\\DLL",
                                   NULL};
    for (int i = 0; normal[i]; i++)
        if (equal_case(directory, normal[i]))
            return 1;
    return 0;
}
static cJSON *os2_component_paths(Context *c, const cJSON *files)
{
    cJSON *wanted = cJSON_CreateObject();
    const cJSON *file;
    cJSON_ArrayForEach(file, files)
    {
        const char *path = field(file, "path");
        if (!path || field(file, "originalPath"))
            continue;
        char *host = host_path(c, path, 0);
        if (!host || !tb_is_file(host))
            continue;
        char *directory = keep(c, strdup(path));
        if (!directory)
            break;
        for (char *at = directory; *at; at++)
            *at = *at == '/' ? '\\' : (char)toupper((unsigned char)*at);
        char *base = strrchr(directory, '\\');
        if (!base)
            continue;
        *base++ = 0;
        if (!os2_component_directory(directory))
            continue;
        const char *extension = strrchr(base, '.');
        if (!extension)
            continue;
        const int windows = strstr(directory, "\\WINOS2") != NULL;
        if (!strcmp(extension, ".EXE") || !strcmp(extension, ".CMD") ||
            (windows && !strcmp(extension, ".COM")))
        {
            os2_path_want(c, wanted, "PATH", directory);
            os2_path_want(c, wanted, "DPATH", directory);
        }
        else if (!strcmp(extension, ".DLL") && !windows)
            os2_path_want(c, wanted, "LIBPATH", directory);
        else if (!strcmp(extension, ".HLP") && !windows)
            os2_path_want(c, wanted, "HELP", directory);
        else if (!strcmp(extension, ".INF") && !windows)
            os2_path_want(c, wanted, "BOOKSHELF", directory);
        else if (!strcmp(extension, ".MSG") || !strcmp(extension, ".DCP") ||
                 !strcmp(extension, ".BMP") || !strcmp(extension, ".MET") ||
                 !strcmp(extension, ".ICO"))
            os2_path_want(c, wanted, "DPATH", directory);
    }
    return wanted;
}
/* Preserve every original byte and append only changed effective assignments.
   CONFIG.SYS permits later assignments to replace earlier search lists. */
static int os2_configure_paths(Context *c, const cJSON *files)
{
    cJSON *wanted = os2_component_paths(c, files);
    if (!cJSON_GetArraySize(wanted))
    {
        cJSON_Delete(wanted);
        return !c->error;
    }
    char *path = join(c, c->roots[2], "CONFIG.SYS");
    size_t n = 0;
    char *bytes = read_file(c, path, &n, 1);
    char *copy = bytes ? keep(c, strdup(bytes)) : NULL;
    cJSON *configured = cJSON_CreateObject(), *defaults = guest_environment(c, NULL);
    if (!copy || !defaults)
        goto done;
    for (char *line = copy; line && *line;)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *entry = trim(line), *assignment = entry;
        if (strlen(entry) >= 4 && toupper((unsigned char)entry[0]) == 'S' &&
            toupper((unsigned char)entry[1]) == 'E' && toupper((unsigned char)entry[2]) == 'T' &&
            isspace((unsigned char)entry[3]))
            assignment = trim(entry + 4);
        char *equals = strchr(assignment, '=');
        if (equals)
        {
            *equals = 0;
            char *key = trim(assignment);
            for (char *at = key; *at; at++)
                *at = (char)toupper((unsigned char)*at);
            if (get(wanted, key) && (assignment != entry || !strcmp(key, "LIBPATH")))
                put(configured, key, cJSON_CreateString(trim(equals + 1)));
        }
        line = next;
    }
    char *output = bytes;
    cJSON *setting;
    cJSON *overrides = get(c->manifest, "environment");
    int manifest_changed = 0;
    cJSON_ArrayForEach(setting, wanted)
    {
        const char *key = setting->string;
        const char *previous = field(configured, key);
        const char *fallback = field(defaults, !strcmp(key, "PATH") ? "OS2PATH" : key);
        char *value = (char *)(previous ? previous : fallback ? fallback : "");
        for (const char *part = str(setting); *part;)
        {
            const char *end = strchr(part, ';');
            size_t length = end ? (size_t)(end - part) : strlen(part);
            char *directory = format(c, "%.*s", (int)length, part);
            value = os2_path_append(c, value, directory);
            for (cJSON *override = overrides ? overrides->child : NULL, *next; override;
                 override = next)
            {
                next = override->next;
                if (!equal_case(override->string, key) &&
                    (strcmp(key, "PATH") || !equal_case(override->string, "OS2PATH")))
                    continue;
                const char *old = str(override);
                char *updated = old ? os2_path_append(c, old, directory) : NULL;
                if (updated && strcmp(updated, old))
                {
                    char *override_key = keep(c, strdup(override->string));
                    put(overrides, override_key, cJSON_CreateString(updated));
                    manifest_changed = 1;
                }
            }
            part += length;
            if (*part)
                part++;
        }
        if (!previous || strcmp(previous, value))
            output = format(c, "%s%s%s%s=%s\r\n", output,
                            *output && output[strlen(output) - 1] != '\n' ? "\r\n" : "",
                            !strcmp(key, "LIBPATH") ? "" : "SET ", key, value);
    }
    if (!c->error && output != bytes)
    {
        size_t current_size = 0;
        char *current = read_file(c, path, &current_size, 1);
        if (!current || current_size != n || memcmp(current, bytes, n))
            fail(c, "CONFIG.SYS changed during extraction; its search paths were not updated");
        else if (!tb_atomic_write(path, output, strlen(output)))
            fail(c, "Cannot update OS/2 component search paths in CONFIG.SYS");
    }
    if (!c->error && manifest_changed)
        save_manifest(c);
done:
    cJSON_Delete(defaults);
    cJSON_Delete(configured);
    cJSON_Delete(wanted);
    return !c->error;
}
