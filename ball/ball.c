/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#include "include/twoine_ball.h"
#include "platform.h"
#include "vendor/cjson/cJSON.h"
#include "vendor/sha256/sha256.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define strdup _strdup
#define PATH_SEPARATOR ";"
#else
#define PATH_SEPARATOR ":"
#endif

typedef struct Context
{
    char **allocations;
    size_t count;
    char *error;
    char *ball;
    char *roots[26];
    cJSON *manifest;
} Context;

static int fail(Context *c, const char *format, ...)
{
    if (!c->error)
    {
        va_list args;
        va_start(args, format);
        va_list copy;
        va_copy(copy, args);
        int n = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        if (n >= 0)
        {
            c->error = malloc((size_t)n + 1);
            if (c->error)
                vsnprintf(c->error, (size_t)n + 1, format, args);
        }
        va_end(args);
    }
    return 0;
}
static char *keep(Context *c, char *p)
{
    if (!p)
    {
        fail(c, "Out of memory or inaccessible path");
        return NULL;
    }
    char **next = realloc(c->allocations, (c->count + 1) * sizeof(*next));
    if (!next)
    {
        free(p);
        fail(c, "Out of memory");
        return NULL;
    }
    c->allocations = next;
    c->allocations[c->count++] = p;
    return p;
}
static char *format(Context *c, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    char *p = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (p)
        vsnprintf(p, (size_t)n + 1, fmt, args);
    va_end(args);
    return keep(c, p);
}
static char *join(Context *c, const char *a, const char *b)
{
    char *path = a && b ? format(c, "%s/%s", a, b) : NULL;
#ifdef _WIN32
    if (path)
        for (char *p = path; *p; p++)
            if (*p == '/')
                *p = '\\';
#endif
    return path;
}
static const char *str(const cJSON *j)
{
    return cJSON_IsString(j) ? j->valuestring : NULL;
}
static cJSON *get(const cJSON *j, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(j, key);
}
static const char *field(const cJSON *j, const char *key)
{
    return str(get(j, key));
}
static void put(cJSON *j, const char *key, cJSON *value)
{
    if (get(j, key))
        cJSON_ReplaceItemInObjectCaseSensitive(j, key, value);
    else
        cJSON_AddItemToObject(j, key, value);
}
static cJSON *object(cJSON *j, const char *key)
{
    cJSON *v = get(j, key);
    if (!v)
    {
        v = cJSON_CreateObject();
        put(j, key, v);
    }
    return cJSON_IsObject(v) ? v : NULL;
}
static int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int equal_case(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    }
    return *a == *b;
}
static char *read_file(Context *c, const char *path, size_t *length, int optional)
{
    FILE *f = tb_fopen(path, "rb");
    if (!f)
    {
        if (optional && !tb_is_file(path) && !tb_is_dir(path))
        {
            if (length)
                *length = 0;
            return keep(c, strdup(""));
        }
        fail(c, "Cannot read %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END))
    {
        fclose(f);
        fail(c, "Cannot seek %s", path);
        return NULL;
    }
    long size = ftell(f);
    rewind(f);
    if (size < 0 || size > 64 * 1024 * 1024)
    {
        fclose(f);
        fail(c, "File is too large: %s", path);
        return NULL;
    }
    char *p = malloc((size_t)size + 1);
    if (!p)
    {
        fclose(f);
        fail(c, "Out of memory");
        return NULL;
    }
    size_t got = fread(p, 1, (size_t)size, f);
    int ok = !ferror(f) && got == (size_t)size;
    fclose(f);
    if (!ok)
    {
        free(p);
        fail(c, "Cannot read %s", path);
        return NULL;
    }
    p[got] = 0;
    if (length)
        *length = got;
    return keep(c, p);
}
static int write_json(Context *c, const char *path, cJSON *value)
{
    char *text = cJSON_Print(value);
    if (!text)
        return fail(c, "Cannot serialize JSON");
    size_t n = strlen(text);
    char *line = malloc(n + 2);
    if (!line)
    {
        cJSON_free(text);
        return fail(c, "Out of memory");
    }
    memcpy(line, text, n);
    line[n++] = '\n';
    line[n] = 0;
    int ok = tb_atomic_write(path, line, n);
    free(line);
    cJSON_free(text);
    return ok ? 1 : fail(c, "Cannot save %s", path);
}
static int save_manifest(Context *c)
{
    return write_json(c, join(c, c->ball, "ball.json"), c->manifest);
}
static char *latin1_to_utf8(Context *c, const char *bytes, size_t n)
{
    char *text = malloc(n * 2 + 1);
    if (!text)
    {
        fail(c, "Out of memory");
        return NULL;
    }
    size_t at = 0;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char b = (unsigned char)bytes[i];
        if (b < 128)
            text[at++] = (char)b;
        else
        {
            text[at++] = (char)(0xc0 | (b >> 6));
            text[at++] = (char)(0x80 | (b & 63));
        }
    }
    text[at] = 0;
    return keep(c, text);
}
static char *utf8_to_latin1(Context *c, const char *text, size_t *length)
{
    char *bytes = malloc(strlen(text) + 1);
    if (!bytes)
    {
        fail(c, "Out of memory");
        return NULL;
    }
    size_t at = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    {
        if (*p < 128)
            bytes[at++] = (char)*p;
        else if ((*p == 0xc2 || *p == 0xc3) && p[1] >= 0x80 && p[1] <= 0xbf)
        {
            bytes[at++] = (char)(((*p & 3) << 6) | (p[1] & 63));
            p++;
        }
        else
        {
            free(bytes);
            fail(c, "CONFIG.SYS uses Latin-1; remove characters outside that encoding");
            return NULL;
        }
    }
    bytes[at] = 0;
    *length = at;
    return keep(c, bytes);
}
static char *case_path(Context *c, const char *path)
{
    char *absolute = keep(c, tb_absolute(path));
    if (!absolute)
        return NULL;
#ifdef _WIN32
    if (tb_is_file(absolute) || tb_is_dir(absolute))
        return keep(c, tb_canonical(absolute));
    /* Canonicalize an existing ancestor before appending a new file name, so
       a junction cannot redirect a profile outside the mapped drive. */
    char *copy = keep(c, strdup(absolute));
    if (!copy)
        return NULL;
    char *slash = strrchr(copy, '\\');
    if (slash && slash[1])
    {
        char *name = keep(c, strdup(slash + 1));
        *slash = 0;
        if (strlen(copy) == 2 && copy[1] == ':')
            copy[2] = '\\', copy[3] = 0;
        char *parent = case_path(c, copy);
        return parent && name ? join(c, parent, name) : NULL;
    }
    return absolute;
#else
    char *result = keep(c, strdup("/"));
    char *parts = keep(c, strdup(absolute));
    if (!result || !parts)
        return NULL;
    char *cursor = NULL;
    for (char *part = strtok_r(parts, "/", &cursor); part; part = strtok_r(NULL, "/", &cursor))
    {
        size_t count = 0;
        char **names = tb_names(result, &count);
        const char *chosen = part;
        for (size_t i = 0; i < count; i++)
            if (!strcmp(names[i], part))
            {
                chosen = names[i];
                break;
            }
        if (chosen == part)
            for (size_t i = 0; i < count; i++)
                if (equal_case(names[i], part))
                {
                    chosen = names[i];
                    break;
                }
        result = join(c, result, chosen);
        tb_names_free(names, count);
        if (!result)
            return NULL;
    }
    if (tb_is_file(result) || tb_is_dir(result))
        return keep(c, tb_canonical(result));
    return keep(c, tb_absolute(result));
#endif
}
static int roots(Context *c)
{
    cJSON *drives = get(c->manifest, "drives");
    if (!cJSON_IsObject(drives) || !get(drives, "C"))
        return fail(c, "Invalid ball.json: missing private C: drive");
    memset(c->roots, 0, sizeof(c->roots));
    cJSON *entry;
    cJSON_ArrayForEach(entry, drives)
    {
        if (!entry->string || strlen(entry->string) != 1 || entry->string[0] < 'A' ||
            entry->string[0] > 'Z' || !str(entry))
            return fail(c, "Invalid drive mapping");
        const char *value = str(entry);
        char *path;
#ifdef _WIN32
        int absolute =
            (strlen(value) > 2 && value[1] == ':') || value[0] == '\\' || value[0] == '/';
#else
        int absolute = value[0] == '/';
#endif
        path = keep(c, tb_canonical(absolute ? value : join(c, c->ball, value)));
        if (!path || !tb_is_dir(path) || strpbrk(path, "\r\n"))
            return fail(c, "Drive %s: is not an accessible directory", entry->string);
        c->roots[entry->string[0] - 'A'] = path;
    }
    return !c->error;
}
static int load(Context *c, const cJSON *request)
{
    const char *path = field(request, "ball");
    if (!path)
        return fail(c, "A ball path is required");
    c->ball = keep(c, tb_absolute(path));
    if (!c->ball)
        return 0;
    const cJSON *provided = get(request, "manifest");
    if (provided)
        c->manifest = cJSON_Duplicate(provided, 1);
    else
    {
        char *text = read_file(c, join(c, c->ball, "ball.json"), NULL, 0);
        if (!text)
            return 0;
        c->manifest = cJSON_ParseWithOpts(text, NULL, 1);
    }
    if (!cJSON_IsObject(c->manifest) || !cJSON_IsNumber(get(c->manifest, "version")) ||
        get(c->manifest, "version")->valuedouble != 1)
        return fail(c, "Unsupported or invalid ball.json");
    return roots(c);
}
static int inside(const char *path, const char *root)
{
    size_t n = strlen(root);
    if (!tb_path_equal(path, root, n))
        return 0;
    return !path[n] || path[n] == '/' || path[n] == '\\' || (n == 1 && root[0] == '/');
}
static char *host_path(Context *c, const char *name, int explicit_host)
{
    if (!name)
    {
        fail(c, "An executable or directory path is required");
        return NULL;
    }
    char *path;
    if (!explicit_host && isalpha((unsigned char)name[0]) && name[1] == ':' &&
        (name[2] == '\\' || name[2] == '/'))
    {
        int drive = toupper((unsigned char)name[0]) - 'A';
        if (!c->roots[drive])
        {
            fail(c, "Drive %c: is not mapped", drive + 'A');
            return NULL;
        }
        char *tail = keep(c, strdup(name + 3));
        if (!tail)
            return NULL;
        for (char *p = tail; *p; p++)
            if (*p == '\\')
                *p = '/';
        path = case_path(c, join(c, c->roots[drive], tail));
    }
    else
        path = case_path(c, name);
    if (!path)
        return NULL;
    for (int i = 0; i < 26; i++)
        if (c->roots[i] && inside(path, c->roots[i]))
            return path;
    fail(c, "Path is outside mapped drives: %s; map its directory first", name);
    return NULL;
}
static char *guest_path(Context *c, const char *host)
{
    int best = -1;
    size_t length = 0;
    for (int i = 0; i < 26; i++)
        if (c->roots[i] && inside(host, c->roots[i]) && strlen(c->roots[i]) >= length)
        {
            length = strlen(c->roots[i]);
            best = i;
        }
    if (best < 0)
    {
        fail(c, "Path is outside mapped drives: %s", host);
        return NULL;
    }
    const char *relative = host + length;
    while (*relative == '/' || *relative == '\\')
        relative++;
    char *result = format(c, "%c:\\%s", best + 'A', relative);
    if (result)
        for (char *p = result + 3; *p; p++)
            if (*p == '/')
                *p = '\\';
    return result;
}
static const char *setting_text(Context *c, const cJSON *v)
{
    if (cJSON_IsString(v))
        return str(v);
    if (cJSON_IsBool(v))
        return cJSON_IsTrue(v) ? "true" : "false";
    if (cJSON_IsNumber(v))
        return format(c, "%.17g", v->valuedouble);
    fail(c, "Invalid setting value");
    return NULL;
}
static int validate_system(Context *c)
{
    cJSON *settings = get(c->manifest, "system"), *entry;
    if (settings && !cJSON_IsObject(settings))
        return fail(c, "Invalid system settings");
    cJSON_ArrayForEach(entry, settings)
    {
        const char *v = setting_text(c, entry);
        if (!v)
            return 0;
        if (strpbrk(v, "\r\n"))
            return fail(c, "A setting must fit on one line");
        if (!strcmp(entry->string, "beep_volume"))
        {
            char *end = NULL;
            double number = strtod(v, &end);
            if (end == v || *end || !isfinite(number) || number < 0 || number > 1)
                return fail(c, "beep_volume must be between 0 and 1");
        }
        else if (!strcmp(entry->string, "trace_native") || !strcmp(entry->string, "trace_events"))
        {
            const char *valid[] = {"1",  "yes", "y",     "true", "t",   "on", "0",
                                   "no", "n",   "false", "f",    "off", NULL};
            int found = 0;
            for (int i = 0; valid[i]; i++)
                if (equal_case(v, valid[i]))
                    found = 1;
            if (!found)
                return fail(c, "Invalid boolean setting: %s", entry->string);
        }
        else
            return fail(c, "Unknown system setting: %s", entry->string);
    }
    return 1;
}
static int write_config(Context *c)
{
    if (!roots(c) || !validate_system(c))
        return 0;
    char *text = keep(c, strdup(""));
    for (int i = 0; i < 26 && text; i++)
        text = format(c, "%smountpoint.%c = %s\n", text, 'A' + i, c->roots[i] ? c->roots[i] : "");
    cJSON *entry;
    cJSON_ArrayForEach(entry, get(c->manifest, "system"))
    {
        text = format(c, "%ssystem.%s = %s\n", text, entry->string, setting_text(c, entry));
        if (!text)
            return 0;
    }
    return text && tb_atomic_write(join(c, c->ball, "2ine.cfg"), text, strlen(text))
               ? 1
               : fail(c, "Cannot write 2ine.cfg");
}
static cJSON *snapshot(Context *c)
{
    if (!roots(c))
        return NULL;
    char *path = join(c, c->roots[2], "CONFIG.SYS");
    size_t size = 0;
    char *bytes = read_file(c, path, &size, 1);
    if (!bytes)
        return NULL;
    cJSON *value = cJSON_CreateObject(), *mapping = cJSON_CreateObject();
    cJSON_AddItemToObject(value, "manifest", cJSON_Duplicate(c->manifest, 1));
    cJSON_AddItemToObject(value, "roots", mapping);
    for (int i = 0; i < 26; i++)
        if (c->roots[i])
        {
            char letter[2] = {(char)('A' + i), 0};
            cJSON_AddStringToObject(mapping, letter, c->roots[i]);
        }
    cJSON_AddStringToObject(value, "configSys", latin1_to_utf8(c, bytes, size));
    return value;
}
static const char *default_keys[] = {"LANG", "OS2PATH",  "LIBPATH",    "DPATH", "TEMP",
                                     "TMP",  "USER_INI", "SYSTEM_INI", NULL};
static const char *default_values[] = {"en_US",
                                       "C:\\OS2;C:\\OS2\\SYSTEM",
                                       "C:\\OS2\\DLL",
                                       "C:\\OS2;C:\\OS2\\SYSTEM",
                                       "C:\\TEMP",
                                       "C:\\TEMP",
                                       "C:\\OS2\\OS2.INI",
                                       "C:\\OS2\\OS2SYS.INI"};
static const char *unset_keys[] = {"IS_2INE",
                                   "TWOINE_CHILD_ARGUMENTS",
                                   "TWOINE_LIBPATH",
                                   "TWOINE_CONFIG",
                                   "TWOINE_BALL",
                                   "TWOINE_USER_PROFILE",
                                   "TWOINE_SYSTEM_PROFILE",
                                   "TWOINE_REGISTRY",
                                   "TWOINE_OBJECTS",
                                   "TWOINE_IPC_DIR",
                                   NULL};
static int init_ball(Context *c, const char *path)
{
    if (!path)
        return fail(c, "A ball path is required");
    c->ball = keep(c, tb_absolute(path));
    if (!c->ball || !tb_mkdirs(c->ball))
        return fail(c, "Cannot create ball directory");
    char *manifest = join(c, c->ball, "ball.json");
    if (tb_is_file(manifest))
    {
        char *s = read_file(c, manifest, NULL, 0);
        c->manifest = s ? cJSON_ParseWithOpts(s, NULL, 1) : NULL;
        if (!cJSON_IsObject(c->manifest) || !cJSON_IsNumber(get(c->manifest, "version")) ||
            get(c->manifest, "version")->valuedouble != 1)
            return fail(c, "Unsupported or invalid ball.json");
        return roots(c);
    }
    size_t n = 0;
    char **names = tb_names(c->ball, &n);
    tb_names_free(names, n);
    if (n)
        return fail(c, "A new ball must be an empty directory");
    const char *dirs[] = {
        "drive_c/OS2/DLL", "drive_c/OS2/SYSTEM", "drive_c/TEMP", "logs", "ipc", NULL};
    for (int i = 0; dirs[i]; i++)
        if (!tb_mkdirs(join(c, c->ball, dirs[i])))
            return fail(c, "Cannot create ball's private directories");
    c->manifest = cJSON_CreateObject();
    cJSON_AddNumberToObject(c->manifest, "version", 1);
    cJSON *drives = cJSON_AddObjectToObject(c->manifest, "drives");
    cJSON_AddStringToObject(drives, "C", "drive_c");
    const char *groups[] = {"environment", "system", "runtime", "applications", NULL};
    for (int i = 0; groups[i]; i++)
        cJSON_AddObjectToObject(c->manifest, groups[i]);
    char *config = keep(c, strdup(""));
    for (int i = 0; default_keys[i]; i++)
        config = format(
            c, "%s%s%s=%s\r\n", config, !strcmp(default_keys[i], "LIBPATH") ? "" : "SET ",
            !strcmp(default_keys[i], "OS2PATH") ? "PATH" : default_keys[i], default_values[i]);
    if (!config || !save_manifest(c) ||
        !tb_atomic_write(join(c, c->ball, "drive_c/CONFIG.SYS"), config, strlen(config)))
        return fail(c, "Cannot initialize ball");
    return write_config(c);
}
static int digest_file(Context *c, const char *path, unsigned char hash[32])
{
    FILE *f = tb_fopen(path, "rb");
    if (!f)
        return fail(c, "Cannot hash runtime file: %s", path);
    SHA256_CTX state;
    sha256_init(&state);
    unsigned char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)))
        sha256_update(&state, buffer, n);
    int ok = !ferror(f);
    fclose(f);
    if (!ok)
        return fail(c, "Cannot read runtime file: %s", path);
    sha256_final(&state, hash);
    return 1;
}
static char *hex(Context *c, const unsigned char hash[32])
{
    char *out = malloc(65);
    if (!out)
    {
        fail(c, "Out of memory");
        return NULL;
    }
    for (int i = 0; i < 32; i++)
        sprintf(out + i * 2, "%02x", hash[i]);
    return keep(c, out);
}
static int runtime_name(const char *s)
{
    size_t n = strlen(s);
    return !strcmp(s, "lx_loader") || !strcmp(s, "lx_loader.exe") ||
           (n > 4 && !strcmp(s + n - 4, ".dll")) ||
           (!strncmp(s, "lib", 3) &&
            ((n > 3 && !strcmp(s + n - 3, ".so")) || (n > 6 && !strcmp(s + n - 6, ".dylib"))));
}
static const char *loader_name(Context *c, const char *build)
{
    if (tb_is_file(join(c, build, "lx_loader")))
        return "lx_loader";
    if (tb_is_file(join(c, build, "lx_loader.exe")))
        return "lx_loader.exe";
    fail(c, "Choose a build directory containing lx_loader");
    return NULL;
}
static char **runtime_names(const char *build, size_t *count)
{
    size_t n = 0;
    char **names = tb_names(build, &n);
    size_t kept = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (runtime_name(names[i]))
            names[kept++] = names[i];
        else
            free(names[i]);
    }
    if (kept > 1)
        qsort(names, kept, sizeof(*names), compare_names);
    *count = kept;
    return names;
}
static char *runtime_id(Context *c, const char *build)
{
    size_t n = 0;
    char **names = runtime_names(build, &n);
    SHA256_CTX state;
    sha256_init(&state);
    int ok = 1;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char hash[32];
        if (!digest_file(c, join(c, build, names[i]), hash))
        {
            ok = 0;
            break;
        }
        sha256_update(&state, (const BYTE *)names[i], strlen(names[i]));
        sha256_update(&state, hash, 32);
    }
    tb_names_free(names, n);
    unsigned char out[32];
    sha256_final(&state, out);
    return ok ? hex(c, out) : NULL;
}
static int sync_runtime(Context *c, const char *build, const char *directory)
{
    if (!tb_mkdirs(directory))
        return fail(c, "Cannot create runtime snapshot");
    size_t n = 0;
    char **names = runtime_names(build, &n);
    int ok = 1;
    for (size_t i = 0; i < n; i++)
    {
        char *src = join(c, build, names[i]), *dst = join(c, directory, names[i]);
        unsigned char a[32], b[32];
        if (!digest_file(c, src, a))
        {
            ok = 0;
            break;
        }
        if (tb_is_file(dst))
        {
            if (!digest_file(c, dst, b))
            {
                ok = 0;
                break;
            }
            if (!memcmp(a, b, 32))
                continue;
        }
        if (!tb_copy(src, dst))
        {
            ok = fail(c, "Cannot snapshot runtime file: %s", names[i]);
            break;
        }
    }
    tb_names_free(names, n);
    return ok;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    return s;
}
static int valid_env_key(const char *key)
{
    if (!key || (!isalpha((unsigned char)*key) && *key != '_'))
        return 0;
    for (key++; *key; key++)
        if (!isalnum((unsigned char)*key) && *key != '_')
            return 0;
    return 1;
}
static int reserved_env(const char *key)
{
    const char *reserved[] = {"HOME",
                              "PATH",
                              "IS_2INE",
                              "TWOINE_CHILD_ARGUMENTS",
                              "TWOINE_CONFIG",
                              "TWOINE_BALL",
                              "TWOINE_IPC_DIR",
                              "TWOINE_USER_PROFILE",
                              "TWOINE_SYSTEM_PROFILE",
                              "TWOINE_REGISTRY",
                              "TWOINE_OBJECTS",
                              "TWOINE_LIBPATH",
                              "TWOINE_BACKEND",
                              NULL};
    if (!strncmp(key, "LD_", 3) || !strncmp(key, "DYLD_", 5))
        return 1;
    for (int i = 0; reserved[i]; i++)
        if (!strcmp(key, reserved[i]))
            return 1;
    return 0;
}
static cJSON *environment(Context *c, const char *build, const char *override)
{
    cJSON *env = cJSON_CreateObject();
    for (int i = 0; default_keys[i]; i++)
        cJSON_AddStringToObject(env, default_keys[i], default_values[i]);
    const char *assets[][2] = {{"TWOINE_FONT_DIRECTORY", "fonts"},
                               {"TWOINE_SOUNDFONT", "soundfonts/GeneralUser-GS.sf2"},
                               {"TWOINE_KOSMICKRISP_ICD", "vulkan/icd.d/kosmickrisp.json"}};
    for (int i = 0; i < 3; i++)
    {
        char *asset = join(c, build, assets[i][1]);
        if (tb_is_file(asset) || tb_is_dir(asset))
            put(env, assets[i][0], cJSON_CreateString(asset));
    }
    size_t n = 0;
    char *bytes = read_file(c, join(c, c->roots[2], "CONFIG.SYS"), &n, 1);
    if (!bytes)
        goto failed;
    char *text = latin1_to_utf8(c, bytes, n);
    if (!text)
        goto failed;
    char *line = text;
    while (line && *line)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *entry = trim(line);
        char *key = NULL;
        if (strlen(entry) >= 4 && toupper((unsigned char)entry[0]) == 'S' &&
            toupper((unsigned char)entry[1]) == 'E' && toupper((unsigned char)entry[2]) == 'T' &&
            entry[3] == ' ')
            key = entry + 4;
        else if (strlen(entry) >= 8)
        {
            char first[9];
            memcpy(first, entry, 8);
            first[8] = 0;
            if (equal_case(first, "LIBPATH="))
                key = entry;
        }
        if (key)
        {
            char *equals = strchr(key, '=');
            if (equals)
            {
                *equals = 0;
                key = trim(key);
                for (char *p = key; *p; p++)
                    *p = (char)toupper((unsigned char)*p);
                put(env, !strcmp(key, "PATH") ? "OS2PATH" : key,
                    cJSON_CreateString(trim(equals + 1)));
            }
        }
        line = next;
    }
    cJSON *entry;
    cJSON *settings = get(c->manifest, "environment");
    if (!cJSON_IsObject(settings))
    {
        fail(c, "Invalid environment object");
        goto failed;
    }
    cJSON_ArrayForEach(entry, settings) put(env, entry->string, cJSON_Duplicate(entry, 1));
    cJSON_ArrayForEach(entry, env)
    {
        if (!valid_env_key(entry->string) || !str(entry))
        {
            fail(c, "Invalid environment entry");
            goto failed;
        }
        if (reserved_env(entry->string))
        {
            fail(c, "Use OS2PATH/LIBPATH or launcher options for %s", entry->string);
            goto failed;
        }
    }
    const char *backend = override ? override : field(get(c->manifest, "runtime"), "backend");
    const char *inherited = getenv("TWOINE_BACKEND");
    const char *selected = backend ? backend : (inherited && *inherited ? inherited : "default");
    if (strcmp(selected, "native") && strcmp(selected, "unicorn") && strcmp(selected, "default"))
    {
        fail(c, "runtime.backend must be native or unicorn");
        goto failed;
    }
    char *identity = runtime_id(c, build);
    if (!identity)
        goto failed;
    char *runtime = format(c, "%s/OS2/DLL/2INE/%s/%.16s", c->roots[2], selected, identity);
    if (!runtime)
        goto failed;
    char *libpath = format(c, "%s%s%s", runtime, PATH_SEPARATOR, build);
    char *guest_lib = keep(c, strdup(field(env, "LIBPATH") ? field(env, "LIBPATH") : ""));
    if (!guest_lib)
        goto failed;
    for (char *part = guest_lib; part && *part;)
    {
        char *next = strchr(part, ';');
        if (next)
            *next++ = 0;
        if (*part)
        {
            char *path = !strcmp(part, ".") ? part : host_path(c, part, 0);
            if (!path)
                goto failed;
            libpath = format(c, "%s%s%s", libpath, PATH_SEPARATOR, path);
        }
        part = next;
    }
    const char *keys[] = {"TWOINE_IPC_DIR",        "TWOINE_BALL",
                          "TWOINE_CONFIG",         "TWOINE_USER_PROFILE",
                          "TWOINE_SYSTEM_PROFILE", "TWOINE_REGISTRY",
                          "TWOINE_OBJECTS",        "TWOINE_LIBPATH",
                          "TWOINE_RUNTIME_ID",     NULL};
    const char *values[] = {join(c, c->ball, "ipc"),
                            c->ball,
                            join(c, c->ball, "2ine.cfg"),
                            host_path(c, field(env, "USER_INI"), 0),
                            host_path(c, field(env, "SYSTEM_INI"), 0),
                            join(c, c->roots[2], "OS2/REGISTRY.INI"),
                            join(c, c->roots[2], "OS2/OBJECTS.INI"),
                            libpath,
                            identity};
    if (c->error)
        goto failed;
    for (int i = 0; keys[i]; i++)
        put(env, keys[i], cJSON_CreateString(values[i]));
    const char *ld = getenv("LD_LIBRARY_PATH");
    put(env, "LD_LIBRARY_PATH",
        cJSON_CreateString(format(c, "%s%s%s%s%s", runtime, PATH_SEPARATOR, build,
                                  ld && *ld ? PATH_SEPARATOR : "", ld ? ld : "")));
    if (backend)
        put(env, "TWOINE_BACKEND", cJSON_CreateString(backend));
    return env;
failed:
    cJSON_Delete(env);
    return NULL;
}
static char *now(Context *c)
{
    time_t value = time(NULL);
    struct tm parts;
#ifdef _WIN32
    gmtime_s(&parts, &value);
#else
    gmtime_r(&value, &parts);
#endif
    char date[40];
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S+00:00", &parts);
    return keep(c, strdup(date));
}
static int array_strings(Context *c, const cJSON *array)
{
    if (!cJSON_IsArray(array))
        return fail(c, "Arguments must be an array of strings");
    cJSON *a;
    cJSON_ArrayForEach(a, array) if (!str(a)) return fail(c, "Arguments must be strings");
    return 1;
}
static cJSON *prepare(Context *c, const cJSON *request)
{
    const char *build_path = field(request, "build");
    if (!build_path || !*build_path)
    {
        fail(c, "Choose a runtime build");
        return NULL;
    }
    char *build = keep(c, tb_canonical(build_path));
    if (!build)
        return NULL;
    const char *loader = loader_name(c, build);
    if (!loader)
        return NULL;
    cJSON *args = get(request, "arguments");
    if (!array_strings(c, args) || !cJSON_GetArraySize(args))
        return NULL;
    const char *action = str(cJSON_GetArrayItem(args, 0));
    int count = cJSON_GetArraySize(args), at = 1;
    const char *cwd = NULL, *program = NULL;
    cJSON *argv = cJSON_CreateArray();
    if (!strcmp(action, "launch"))
    {
        const char *name = str(cJSON_GetArrayItem(args, at++));
        cJSON *app = name ? get(get(c->manifest, "applications"), name) : NULL;
        if (!cJSON_IsObject(app))
        {
            fail(c, "Saved application does not exist: %s", name ? name : "");
            goto failed;
        }
        program = field(app, "program");
        cwd = field(app, "cwd");
        cJSON *a;
        cJSON_ArrayForEach(a, get(app, "arguments"))
            cJSON_AddItemToArray(argv, cJSON_Duplicate(a, 1));
    }
    else if (!strcmp(action, "run") || !strcmp(action, "install"))
    {
        if (at < count && !strcmp(str(cJSON_GetArrayItem(args, at)), "--cwd"))
        {
            cwd = str(cJSON_GetArrayItem(args, at + 1));
            at += 2;
        }
        program = str(cJSON_GetArrayItem(args, at++));
    }
    else
    {
        fail(c, "Use run, install or launch");
        goto failed;
    }
    for (; at < count; at++)
        cJSON_AddItemToArray(argv, cJSON_Duplicate(cJSON_GetArrayItem(args, at), 1));
    char *host = host_path(c, program, cJSON_IsTrue(get(request, "hostProgram")));
    if (!host)
        goto failed;
    if (!tb_is_file(host))
    {
        fail(c, "Program does not exist: %s", host);
        goto failed;
    }
    char *working = NULL;
    if (cwd && *cwd)
        working = host_path(c, cwd, 0);
    else
    {
        working = keep(c, strdup(host));
        if (working)
        {
            char *slash = strrchr(working, '/');
#ifdef _WIN32
            char *back = strrchr(working, '\\');
            if (back && (!slash || back > slash))
                slash = back;
#endif
            if (slash)
                *slash = 0;
        }
    }
    if (!working || !tb_is_dir(working))
    {
        fail(c, "Working directory must be a mapped directory");
        goto failed;
    }
    if (!write_config(c))
        goto failed;
    cJSON *env = environment(c, build, field(request, "backend"));
    if (!env)
        goto failed;
    const char *identity = field(env, "TWOINE_RUNTIME_ID");
    const char *selected = field(env, "TWOINE_BACKEND");
    if (!selected)
        selected = getenv("TWOINE_BACKEND");
    if (!selected || !*selected)
        selected = "default";
    char *runtime = format(c, "%s/OS2/DLL/2INE/%s/%.16s", c->roots[2], selected, identity);
    if (!runtime || !sync_runtime(c, build, runtime))
    {
        cJSON_Delete(env);
        goto failed;
    }
    cJSON *command = cJSON_CreateArray();
    cJSON_AddItemToArray(command, cJSON_CreateString(join(c, runtime, loader)));
    cJSON_AddItemToArray(command, cJSON_CreateString(guest_path(c, host)));
    cJSON *a;
    cJSON_ArrayForEach(a, argv) cJSON_AddItemToArray(command, cJSON_Duplicate(a, 1));
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "command", command);
    cJSON_AddStringToObject(result, "cwd", working);
    cJSON_AddStringToObject(result, "programHost", host);
    cJSON_AddItemToObject(result, "environment", env);
    cJSON *unset = cJSON_AddArrayToObject(result, "unsetEnvironment");
    for (int i = 0; unset_keys[i]; i++)
        cJSON_AddItemToArray(unset, cJSON_CreateString(unset_keys[i]));
    cJSON *receipt = cJSON_AddObjectToObject(result, "receipt");
    cJSON_AddItemToObject(receipt, "command", cJSON_Duplicate(command, 1));
    cJSON_AddStringToObject(receipt, "runtime_id", identity);
    cJSON_AddStringToObject(receipt, "cwd", working);
    cJSON_AddStringToObject(receipt, "started", now(c));
    cJSON_AddBoolToObject(result, "installer", !strcmp(action, "install"));
    cJSON_Delete(argv);
    return result;
failed:
    cJSON_Delete(argv);
    return NULL;
}
static int command(Context *c, const cJSON *request)
{
    cJSON *args = get(request, "arguments");
    if (!array_strings(c, args))
        return 0;
    int n = cJSON_GetArraySize(args);
    const char *action = str(cJSON_GetArrayItem(args, 0));
    if (!action)
        return fail(c, "A command is required");
    const char *name = str(cJSON_GetArrayItem(args, 1));
    if (!strcmp(action, "map") || !strcmp(action, "unmap"))
    {
        if (!name || !isalpha((unsigned char)name[0]) || (name[1] && strcmp(name + 1, ":")))
            return fail(c, "Choose a drive A-Z other than C:");
        char letter[2] = {(char)toupper((unsigned char)name[0]), 0};
        if (letter[0] == 'C')
            return fail(c, "C: is private to the ball");
        cJSON *drives = object(c->manifest, "drives");
        if (!drives)
            return fail(c, "Invalid drives object");
        if (!strcmp(action, "map"))
        {
            const char *dir = str(cJSON_GetArrayItem(args, 2));
            char *path = dir ? keep(c, tb_canonical(dir)) : NULL;
            if (!path || !tb_is_dir(path))
                return fail(c, "Choose an accessible host directory");
            put(drives, letter, cJSON_CreateString(path));
        }
        else
            cJSON_DeleteItemFromObjectCaseSensitive(drives, letter);
        return write_config(c) && save_manifest(c);
    }
    if (!strcmp(action, "config") || !strcmp(action, "unset"))
    {
        if (!name)
            return fail(c, "A setting key is required");
        const char *dot = strchr(name, '.');
        if (!dot || !dot[1])
            return fail(c, "Use env.NAME, system.NAME or runtime.backend");
        char *group = keep(c, strdup(name));
        group[dot - name] = 0;
        const char *key = dot + 1;
        if (strcmp(group, "env") && strcmp(group, "system") && strcmp(group, "runtime"))
            return fail(c, "Unknown setting group");
        if (!strcmp(group, "runtime") && strcmp(key, "backend"))
            return fail(c, "Unknown runtime setting");
        cJSON *settings = object(c->manifest, !strcmp(group, "env") ? "environment" : group);
        if (!settings)
            return fail(c, "Invalid settings object");
        if (!strcmp(action, "unset"))
            cJSON_DeleteItemFromObjectCaseSensitive(settings, key);
        else
        {
            const char *value = str(cJSON_GetArrayItem(args, 2));
            if (!value)
                return fail(c, "A setting value is required");
            put(settings, key, cJSON_CreateString(value));
        }
        const char *build = field(request, "build");
        char *absolute = keep(c, tb_absolute(build && *build ? build : "."));
        if (!absolute)
            return 0;
        cJSON *env = environment(c, absolute, field(request, "backend"));
        if (!env)
            return 0;
        cJSON_Delete(env);
        return write_config(c) && save_manifest(c);
    }
    if (!strcmp(action, "add-app"))
    {
        int at = 1;
        const char *cwd = NULL;
        if (name && !strcmp(name, "--cwd"))
        {
            cwd = str(cJSON_GetArrayItem(args, 2));
            at = 3;
        }
        name = str(cJSON_GetArrayItem(args, at++));
        const char *program = str(cJSON_GetArrayItem(args, at++));
        if (!name || !program)
            return fail(c, "Application name and executable are required");
        char *host = host_path(c, program, 0);
        if (!host || !tb_is_file(host))
            return fail(c, "Program does not exist: %s", program);
        cJSON *apps = object(c->manifest, "applications");
        if (!apps)
            return fail(c, "Invalid applications object");
        cJSON *app = cJSON_CreateObject();
        cJSON_AddStringToObject(app, "program", guest_path(c, host));
        cJSON *argv = cJSON_AddArrayToObject(app, "arguments");
        for (; at < n; at++)
            cJSON_AddItemToArray(argv, cJSON_Duplicate(cJSON_GetArrayItem(args, at), 1));
        if (cwd && *cwd)
        {
            char *directory = host_path(c, cwd, 0);
            if (!directory || !tb_is_dir(directory))
            {
                cJSON_Delete(app);
                return fail(c, "Working directory must be a mapped directory");
            }
            cJSON_AddStringToObject(app, "cwd", guest_path(c, directory));
        }
        put(apps, name, app);
        return save_manifest(c);
    }
    if (!strcmp(action, "remove-app"))
    {
        if (!name)
            return fail(c, "An application name is required");
        cJSON_DeleteItemFromObjectCaseSensitive(get(c->manifest, "applications"), name);
        return save_manifest(c);
    }
    return fail(c, "Unsupported ball command: %s", action);
}
static cJSON *dispatch(Context *c, const cJSON *request)
{
    const char *action = field(request, "action");
    if (!action)
    {
        fail(c, "An action is required");
        return NULL;
    }
    if (!strcmp(action, "defaults"))
    {
        cJSON *v = cJSON_CreateObject();
        for (int i = 0; default_keys[i]; i++)
            cJSON_AddStringToObject(v, default_keys[i], default_values[i]);
        return v;
    }
    if (!strcmp(action, "unset-environment"))
    {
        cJSON *v = cJSON_CreateArray();
        for (int i = 0; unset_keys[i]; i++)
            cJSON_AddItemToArray(v, cJSON_CreateString(unset_keys[i]));
        return v;
    }
    if (!strcmp(action, "runtime-files"))
    {
        const char *build = field(request, "build");
        if (!build)
        {
            fail(c, "A build directory is required");
            return NULL;
        }
        size_t n = 0;
        char **names = runtime_names(build, &n);
        cJSON *v = cJSON_CreateArray();
        for (size_t i = 0; i < n; i++)
            cJSON_AddItemToArray(v, cJSON_CreateString(join(c, build, names[i])));
        tb_names_free(names, n);
        return v;
    }
    if (!strcmp(action, "sync-runtime"))
    {
        const char *build = field(request, "build"), *destination = field(request, "directory");
        if (!build || !destination)
        {
            fail(c, "Build and destination are required");
            return NULL;
        }
        return sync_runtime(c, build, destination) ? cJSON_CreateBool(1) : NULL;
    }
    if (!strcmp(action, "discover"))
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        char *home = keep(c, tb_home());
        if (!home)
            return NULL;
        char *base = xdg && *xdg ? keep(c, strdup(xdg)) : join(c, home, ".local/share");
        char *directory = join(c, base, "2ine/balls");
        cJSON *list = cJSON_CreateArray();
        size_t n = 0;
        char **names = tb_names(directory, &n);
        if (n > 1)
            qsort(names, n, sizeof(*names), compare_names);
        for (size_t i = 0; i < n; i++)
        {
            char *path = join(c, directory, names[i]);
            if (tb_is_file(join(c, path, "ball.json")))
                cJSON_AddItemToArray(list, cJSON_CreateString(path));
        }
        tb_names_free(names, n);
        return list;
    }
    cJSON *args = get(request, "arguments");
    const char *first = str(cJSON_GetArrayItem(args, 0));
    if (!strcmp(action, "command") && first && !strcmp(first, "init"))
    {
        if (!init_ball(c, field(request, "ball")))
            return NULL;
        return snapshot(c);
    }
    if (!load(c, request))
        return NULL;
    if (!strcmp(action, "snapshot"))
        return snapshot(c);
    if (!strcmp(action, "command"))
    {
        if (!command(c, request))
            return NULL;
        return snapshot(c);
    }
    if (!strcmp(action, "runtime-build"))
    {
        const char *name = field(request, "build");
        char *build = name ? keep(c, tb_canonical(name)) : NULL;
        if (!build || !loader_name(c, build))
            return NULL;
        cJSON *runtime = object(c->manifest, "runtime");
        if (!runtime)
        {
            fail(c, "Invalid runtime object");
            return NULL;
        }
        put(runtime, "build", cJSON_CreateString(build));
        if (!save_manifest(c))
            return NULL;
        return snapshot(c);
    }
    if (!strcmp(action, "save-config-sys"))
    {
        const char *expected = field(request, "expected"), *text = field(request, "text");
        if (!expected || !text)
        {
            fail(c, "Expected and edited CONFIG.SYS text are required");
            return NULL;
        }
        char *path = join(c, c->roots[2], "CONFIG.SYS");
        size_t n = 0;
        char *bytes = read_file(c, path, &n, 1);
        if (!bytes)
            return NULL;
        char *current = latin1_to_utf8(c, bytes, n);
        if (!current || strcmp(expected, current))
        {
            fail(c, "CONFIG.SYS changed on disk. Reload it before saving your edits.");
            return NULL;
        }
        char *edited = utf8_to_latin1(c, text, &n);
        if (!edited || !tb_atomic_write(path, edited, n))
        {
            fail(c, "Cannot save CONFIG.SYS");
            return NULL;
        }
        return snapshot(c);
    }
    if (!strcmp(action, "prepare-launch"))
        return prepare(c, request);
    if (!strcmp(action, "record-install"))
    {
        cJSON *receipt = get(request, "receipt"), *exit = get(request, "exitCode");
        const char *program = field(request, "programHost");
        if (!cJSON_IsObject(receipt) || !cJSON_IsNumber(exit) || !program)
        {
            fail(c, "A launch receipt, program and actual exit code are required");
            return NULL;
        }
        cJSON *saved = cJSON_Duplicate(receipt, 1);
        unsigned char digest[32];
        if (!digest_file(c, program, digest))
        {
            cJSON_Delete(saved);
            return NULL;
        }
        put(saved, "program_sha256", cJSON_CreateString(hex(c, digest)));
        put(saved, "exit_code", cJSON_Duplicate(exit, 1));
        put(saved, "finished", cJSON_CreateString(now(c)));
        int ok = write_json(c, join(c, c->ball, "logs/last-install.json"), saved);
        cJSON_Delete(saved);
        return ok ? snapshot(c) : NULL;
    }
    /* Small operations exposed for CLI/native frontends and interoperability tests. */
    if (!strcmp(action, "host-path") || !strcmp(action, "guest-path"))
    {
        const char *name = field(request, "path");
        char *path = name ? host_path(c, name, !strcmp(action, "guest-path")) : NULL;
        return path ? cJSON_CreateString(!strcmp(action, "host-path") ? path : guest_path(c, path))
                    : NULL;
    }
    if (!strcmp(action, "environment"))
    {
        const char *b = field(request, "build");
        char *build = keep(c, tb_absolute(b && *b ? b : "."));
        return build ? environment(c, build, field(request, "backend")) : NULL;
    }
    if (!strcmp(action, "write-config"))
        return write_config(c) ? snapshot(c) : NULL;
    fail(c, "Unsupported ball action: %s", action);
    return NULL;
}
unsigned tb_abi_version(void)
{
    return 1;
}
void tb_free(void *p)
{
    free(p);
}
int tb_request(const char *text, char **result, char **error)
{
    if (!result || !error)
        return 2;
    *result = NULL;
    *error = NULL;
    Context c = {0};
    cJSON *request = text ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
    cJSON *response = NULL;
    if (!cJSON_IsObject(request))
        fail(&c, "Request must be a JSON object");
    else
        response = dispatch(&c, request);
    if (response && !c.error)
        *result = cJSON_PrintUnformatted(response);
    if (!*result && !c.error)
        fail(&c, "Ball operation failed");
    cJSON_Delete(response);
    cJSON_Delete(request);
    cJSON_Delete(c.manifest);
    for (size_t i = 0; i < c.count; i++)
        free(c.allocations[i]);
    free(c.allocations);
    *error = c.error;
    return c.error || !*result ? 2 : 0;
}
