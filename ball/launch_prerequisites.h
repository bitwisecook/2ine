/* Public domain. No rights reserved. Wholly AI-authored. */
/* Read-only bootstrap requirements and existing-ball navigation facts. */
static int installer_support_required(Context *c, const char *host)
{
    char *folder = keep(c, strdup(host)), *slash = folder ? strrchr(folder, '/') : NULL;
    if (!slash)
        return 0;
    *slash = 0;
    size_t count = 0;
    char **names = tb_names(folder, &count);
    int suite = 0;
    if (count != (size_t)-1)
        for (size_t i = 0; i < count; i++)
            suite |= equal_case(names[i], "SUITE.RSP");
    tb_names_free(names, count == (size_t)-1 ? 0 : count);
    return suite;
}

static int launch_command_file(const char *program)
{
    const char *suffix = program ? strrchr(program, '.') : NULL;
    return suffix && (equal_case(suffix, ".CMD") || equal_case(suffix, ".BAT"));
}

static cJSON *launch_prerequisites(Context *c, const cJSON *request)
{
    const cJSON *arguments = get(request, "arguments");
    if (!array_strings(c, arguments) || !cJSON_GetArraySize(arguments))
    {
        fail(c, "Choose an application or installer first");
        return NULL;
    }
    const char *action = str(cJSON_GetArrayItem(arguments, 0)), *program = NULL;
    c->application = request;
    if (!strcmp(action, "launch"))
    {
        const char *name = str(cJSON_GetArrayItem(arguments, 1));
        c->application = name ? get(get(c->manifest, "applications"), name) : NULL;
        program = field(c->application, "program");
    }
    else if (!strcmp(action, "run") || !strcmp(action, "install"))
    {
        int at = 1;
        const char *option = str(cJSON_GetArrayItem(arguments, at));
        if (option && !strcmp(option, "--cwd"))
            at += 2;
        program = str(cJSON_GetArrayItem(arguments, at));
    }
    if (!program || !*program)
    {
        fail(c, "Use run, install or launch with an existing program");
        return NULL;
    }
    char *host = host_path(c, program, cJSON_IsTrue(get(request, "hostProgram")));
    if (!host || !tb_is_file(host))
    {
        fail(c, "Program does not exist: %s", program);
        return NULL;
    }
    int command = launch_command_file(program), rexx = 0;
    if (command)
    {
        FILE *file = tb_fopen(host, "rb");
        if (!file)
        {
            fail(c, "Cannot read command file: %s", program);
            return NULL;
        }
        rexx = fgetc(file) == '/' && fgetc(file) == '*';
        fclose(file);
    }
    int suite = !strcmp(action, "install") && installer_support_required(c, host);
    cJSON *result = cJSON_CreateObject(),
          *required = cJSON_AddArrayToObject(result, "requiredCategories"),
          *requirements = cJSON_AddArrayToObject(result, "requirements"),
          *issues = cJSON_AddArrayToObject(result, "issues");
    cJSON_AddStringToObject(result, "program", program);
    cJSON_AddBoolToObject(result, "commandFile", command);
    cJSON_AddBoolToObject(result, "rexx", rexx);
    int missing_interpreter = 0;
    if (command)
    {
        cJSON *environment = guest_environment(c, get(c->application, "environment"));
        int environment_failed = environment == NULL;
        const char *configured = field(environment, "COMSPEC");
        char *comspec = configured ? keep(c, strdup(configured)) : NULL;
        cJSON_Delete(environment);
        char *interpreter = !c->error ? find_command_interpreter(c) : NULL;
        if (interpreter)
            cJSON_AddStringToObject(
                result, "interpreter",
                guest_for_input(c, interpreter, comspec ? comspec : "C:\\OS2\\CMD.EXE"));
        else if (c->error && (environment_failed || !comspec ||
                              !equal_case(component_key(c, comspec), "C:\\OS2\\CMD.EXE")))
        {
            cJSON *issue = cJSON_CreateObject();
            cJSON_AddStringToObject(issue, "kind", "configuration");
            cJSON_AddStringToObject(issue, "path", comspec ? comspec : "environment");
            cJSON_AddStringToObject(issue, "message", c->error);
            cJSON_AddItemToArray(issues, issue);
            free(c->error);
            c->error = NULL;
        }
        else
        {
            missing_interpreter = 1;
            free(c->error);
            c->error = NULL;
        }
    }
    cJSON *support = os2_support(c), *missing = cJSON_CreateArray();
    if (!support)
    {
        cJSON_Delete(result);
        cJSON_Delete(missing);
        return NULL;
    }
    const cJSON *file;
    cJSON_ArrayForEach(file,
                       get(support, "files")) if (!cJSON_IsTrue(get(file, "present")) &&
                                                  ((cJSON_IsTrue(get(file, "required")) &&
                                                    (suite || missing_interpreter)) ||
                                                   (!cJSON_IsTrue(get(file, "required")) && rexx)))
        cJSON_AddItemToArray(missing, cJSON_CreateString(field(file, "path")));
    if (cJSON_GetArraySize(missing))
    {
        cJSON *group = extract_group(EXTRACT_CMD);
        cJSON_AddItemToObject(group, "missingPaths", missing);
        cJSON_AddStringToObject(
            group, "reason",
            "Install CMD and REXX Support before starting this command file or installer.");
        cJSON_AddItemToArray(requirements, group);
        cJSON_AddItemToArray(required, cJSON_CreateString("cmd"));
    }
    else
        cJSON_Delete(missing);
    cJSON_AddBoolToObject(result, "ready",
                          !cJSON_GetArraySize(requirements) && !cJSON_GetArraySize(issues));
    cJSON_AddStringToObject(
        result, "reason",
        cJSON_GetArraySize(issues)
            ? "The configured command interpreter is unavailable; check COMSPEC before retrying."
        : cJSON_GetArraySize(requirements)
            ? "Select CMD and REXX Support in OS/2 Components and wait for it to finish installing."
            : "Ready to launch.");
    cJSON_Delete(support);
    return result;
}

static void launch_discard_error(Context *c)
{
    free(c->error);
    c->error = NULL;
}

static int navigation_program_format(const char *host)
{
    FILE *file = tb_fopen(host, "rb");
    if (!file)
        return 0;
    unsigned char header[64], signature[2];
    int valid = fread(header, 1, sizeof header, file) == sizeof header && header[0] == 'M' &&
                header[1] == 'Z';
    uint32_t at = valid ? (uint32_t)header[60] | (uint32_t)header[61] << 8 |
                              (uint32_t)header[62] << 16 | (uint32_t)header[63] << 24
                        : 0;
    valid = valid && !fseek(file, at, SEEK_SET) && fread(signature, 1, 2, file) == 2 &&
            ((!memcmp(signature, "LX", 2)) || !memcmp(signature, "NE", 2) ||
             !memcmp(signature, "LE", 2));
    if (valid)
    {
        unsigned offset = signature[0] == 'N' ? 12 : 16;
        unsigned char flags[2];
        valid = at <= UINT32_MAX - offset && !fseek(file, at + offset, SEEK_SET) &&
                fread(flags, 1, sizeof flags, file) == sizeof flags && !(flags[1] & 0x80);
    }
    fclose(file);
    return valid;
}

static int navigation_payload(const char *name)
{
    const char *suffix = strrchr(name, '.');
    static const char *types[] = {".EXE", ".COM", ".CMD", ".BAT", ".DLL", ".FON", ".TTF", ".OTF",
                                  ".PFB", ".PFA", ".AFM", ".PFM", ".MSG", ".HLP", ".INF", ".BMP",
                                  ".ICO", ".WAV", ".MID", ".AVI", ".DCP", ".TBL", ".NLS", ".DAT",
                                  ".IR",  ".TOC", ".DRV", ".PDR", ".DEV", ".SYS", ".OS2", NULL};
    for (unsigned i = 0; suffix && types[i]; i++)
        if (equal_case(suffix, types[i]))
            return 1;
    return 0;
}

static cJSON *ball_navigation_state(Context *c)
{
    cJSON *result = cJSON_CreateObject(),
          *valid = cJSON_AddArrayToObject(result, "validApplications");
    const cJSON *application;
    int installed = 0, truncated = 0;
    unsigned applications_visited = 0;
    cJSON_ArrayForEach(application, get(c->manifest, "applications"))
    {
        if (++applications_visited > 10000)
        {
            truncated = 1;
            break;
        }
        if (!application->string || !cJSON_IsObject(application))
            continue;
        cJSON *request = cJSON_CreateObject(),
              *arguments = cJSON_AddArrayToObject(request, "arguments");
        cJSON_AddItemToArray(arguments, cJSON_CreateString("launch"));
        cJSON_AddItemToArray(arguments, cJSON_CreateString(application->string));
        cJSON *status = launch_prerequisites(c, request);
        if (status && cJSON_IsTrue(get(status, "ready")))
        {
            const char *program = field(application, "program");
            char *host = program ? host_path(c, program, 0) : NULL;
            if (host && (launch_command_file(program) || navigation_program_format(host)))
                cJSON_AddItemToArray(valid, cJSON_CreateString(application->string));
        }
        cJSON_Delete(status);
        cJSON_Delete(request);
        launch_discard_error(c);
    }
    cJSON *ledger = component_ledger(c);
    const cJSON *record;
    cJSON_ArrayForEach(record, ledger)
    {
        const char *path = field(record, "path");
        char *host = path ? component_private_path(c, path) : NULL;
        if (host && tb_is_file(host))
            installed = 1;
        launch_discard_error(c);
        if (installed)
            break;
    }
    cJSON_Delete(ledger);
    launch_discard_error(c);
    const char *roots[] = {"OS2", "MMOS2", "PSFONTS", "MPTN", "IBMCOM", "IBMLAN", "TCPIP", NULL};
    cJSON *queue = cJSON_CreateArray();
    for (unsigned i = 0; roots[i]; i++)
        cJSON_AddItemToArray(queue, cJSON_CreateString(format(c, "C:\\%s", roots[i])));
    unsigned visited = 0;
    const cJSON *directory;
    cJSON_ArrayForEach(directory, queue)
    {
        if (installed)
            break;
        if (++visited > 10000)
        {
            truncated = 1;
            break;
        }
        char *host = host_path(c, str(directory), 0);
        if (!host || !c->roots[2] || !inside(c->roots[2], c->ball) || !inside(host, c->roots[2]) ||
            tb_is_link(host))
        {
            launch_discard_error(c);
            continue;
        }
        char *key = component_key(c, str(directory));
        if (equal_case(key, "C:\\OS2\\DLL\\2INE") || !strncmp(key, "C:\\OS2\\DLL\\2INE\\", 16) ||
            strstr(key, "\\TEMP") || strstr(key, "\\CACHE"))
            continue;
        size_t count = 0;
        char **names = tb_names(host, &count);
        if (count != (size_t)-1)
            for (size_t i = 0; i < count; i++)
            {
                if (++visited > 10000)
                {
                    truncated = 1;
                    break;
                }
                char *child = join(c, host, names[i]);
                if (tb_is_link(child))
                    continue;
                if (tb_is_dir(child))
                    cJSON_AddItemToArray(
                        queue, cJSON_CreateString(format(c, "%s\\%s", str(directory), names[i])));
                else if (tb_is_file(child) && navigation_payload(names[i]))
                {
                    installed = 1;
                    break;
                }
            }
        tb_names_free(names, count == (size_t)-1 ? 0 : count);
    }
    cJSON_Delete(queue);
    c->application = NULL;
    cJSON_AddBoolToObject(result, "installedComponents", installed);
    cJSON_AddBoolToObject(result, "truncated", truncated);
    cJSON_AddStringToObject(result, "page",
                            cJSON_GetArraySize(valid) ? "applications"
                            : installed               ? "application-installation"
                                                      : "system-components");
    return result;
}
