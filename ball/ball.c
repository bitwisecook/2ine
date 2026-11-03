/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#include "include/twoine_ball.h"
#include "platform.h"
#include "disk_image.h"
#include "icon.h"
#include "vendor/cjson/cJSON.h"
#include "project_sources.h"
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
    int unavailable[26];
    cJSON *manifest;
    const cJSON *application;
    const char *opengl_policy;
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
        fail(c, "Out of memory");
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
static char *resolve_path(Context *c, const char *path, int existing)
{
    if (!path)
        return NULL;
    errno = 0;
    char *result = existing ? tb_canonical(path) : tb_absolute(path);
    if (!result)
    {
        int error = errno;
        if (tb_image_device(path) && error == ENXIO)
        {
            fail(c, "No media is inserted in this drive. Attach an image and try again.");
            return NULL;
        }
        if (tb_image_path(path) && error != ENOMEM)
        {
            char *source = tb_image_source(path);
            if (source)
            {
                if (error == ENOENT && tb_is_file(source))
                    fail(c, "File or folder not found in image %s: %s", source,
                         tb_image_member(path));
                else
                    fail(c, "Cannot read image %s: %s", source, strerror(error ? error : EIO));
                free(source);
                return NULL;
            }
        }
        if (error == ENOMEM)
            fail(c, "Out of memory");
        else if (error == ENOENT)
            fail(c, "Path does not exist: %s", path);
        else if (error == EACCES || error == EPERM)
            fail(c, "Permission denied accessing path: %s", path);
        else if (error == ENOTDIR)
            fail(c, "A path component is not a directory: %s", path);
        else
            fail(c, "Cannot resolve path %s: %s", path, strerror(error ? error : EIO));
        return NULL;
    }
    return keep(c, result);
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
/* Recognize host paths from either platform without treating DOS paths as relative. */
static int host_absolute(const char *path)
{
    return path && (path[0] == '/' || path[0] == '\\' ||
                    (isalpha((unsigned char)path[0]) && path[1] == ':'));
}
static int native_host_path(const char *path)
{
#ifdef _WIN32
    return !(path[0] == '/' && path[1] != '/');
#else
    return path[0] != '\\' && !(path[0] == '/' && path[1] == '/') &&
           !(isalpha((unsigned char)path[0]) && path[1] == ':');
#endif
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
    if (tb_image_path(path))
    {
        TbImageInfo info;
        if (tb_image_info(path, &info))
            return resolve_path(c, path, 1);
        return resolve_path(c, path, errno == ENOENT ? 0 : 1);
    }
    char *absolute = resolve_path(c, path, 0);
    if (!absolute)
        return NULL;
#ifdef _WIN32
    if (tb_is_file(absolute) || tb_is_dir(absolute))
        return resolve_path(c, absolute, 1);
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
        return resolve_path(c, result, 1);
    return resolve_path(c, result, 0);
#endif
}
static char *device_root(Context *c, const char *letter, const char *kind, const char *image,
                         int readonly)
{
    char *directory = join(c, c->ball, "devices");
    char *control = join(c, directory, format(c, "%s.media", letter));
    if (!directory || !control || !tb_mkdirs(directory))
    {
        fail(c, "Cannot create media device directory: %s", strerror(errno));
        return NULL;
    }
    size_t n = image ? strlen(image) : 0;
    if (n > 32768)
    {
        fail(c, "Image path is too long");
        return NULL;
    }
    unsigned char *bytes = (unsigned char *)keep(c, calloc(1, n + 16));
    if (!bytes)
        return NULL;
    memcpy(bytes, "2INEMED1", 8);
    bytes[8] = !strcmp(kind, "floppy") ? 2 : !strcmp(kind, "disk") ? 3 : 1;
    bytes[9] = readonly || bytes[8] == 1;
    for (unsigned i = 0; i < 4; i++)
        bytes[12 + i] = (unsigned char)(n >> (8 * i));
    if (n)
        memcpy(bytes + 16, image, n);
    unsigned char *old = (unsigned char *)keep(c, malloc(n + 17));
    if (!old)
        return NULL;
    FILE *file = tb_fopen(control, "rb");
    int same = file && fread(old, 1, n + 17, file) == n + 16 && !memcmp(old, bytes, n + 16);
    if (file)
        fclose(file);
    if (!same && !tb_atomic_write(control, bytes, n + 16))
    {
        fail(c, "Cannot update %s: media: %s", letter, strerror(errno));
        return NULL;
    }
    return keep(c, tb_image_device_root(control));
}
static int roots(Context *c)
{
    cJSON *drives = get(c->manifest, "drives");
    if (!cJSON_IsObject(drives) || !get(drives, "C"))
        return fail(c, "Invalid ball.json: missing private C: drive");
    memset(c->roots, 0, sizeof(c->roots));
    memset(c->unavailable, 0, sizeof(c->unavailable));
    cJSON *entry;
    cJSON_ArrayForEach(entry, drives)
    {
        if (!entry->string || strlen(entry->string) != 1 || entry->string[0] < 'A' ||
            entry->string[0] > 'Z' ||
            (!str(entry) && !field(entry, "image") && !field(entry, "device")))
            return fail(c, "Invalid drive mapping");
        const char *image = field(entry, "image"), *kind = field(entry, "device");
        if (kind && strcmp(kind, "cdrom") && strcmp(kind, "floppy") && strcmp(kind, "disk"))
            return fail(c, "Unknown removable device type");
        if (image || kind)
        {
            if (image && !host_absolute(image))
            {
                image = join(c, c->ball, image);
                if (!image)
                    return 0;
            }
            if (entry->string[0] == 'C')
                return fail(c, "C: must remain the ball's private directory");
            if (!kind && image)
            {
                char *raw = keep(c, tb_image_root(image));
                TbImageInfo info;
                kind = raw && tb_image_info(raw, &info)
                           ? (!strcmp(info.filesystem, "CDFS")  ? "cdrom"
                              : !strcmp(info.filesystem, "FAT") ? "floppy"
                                                                : "disk")
                           : "cdrom";
                cJSON_AddStringToObject(entry, "device", kind);
            }
            FILE *media = image && native_host_path(image) && tb_is_file(image)
                              ? tb_fopen(image, "rb")
                              : NULL;
            if (image && !media)
            {
                c->unavailable[entry->string[0] - 'A'] = 1;
                image = NULL;
            }
            if (media)
                fclose(media);
            c->roots[entry->string[0] - 'A'] =
                device_root(c, entry->string, kind, image, cJSON_IsTrue(get(entry, "readonly")));
            continue;
        }
        const char *value = str(entry);
        char *path;
        if (!*value)
        {
            if (entry->string[0] == 'C')
                return fail(c, "Invalid private C: directory");
            c->unavailable[entry->string[0] - 'A'] = 1;
            continue;
        }
        if (strpbrk(value, "\r\n"))
            return fail(c, "Invalid drive mapping");
        const char *source = host_absolute(value) ? value : join(c, c->ball, value);
        if (!source)
            return 0;
        if (entry->string[0] != 'C')
        {
            errno = 0;
            char *resolved = native_host_path(source) ? tb_canonical(source) : NULL;
            if (!resolved && errno == ENOMEM)
                return fail(c, "Out of memory");
            if (!resolved || !tb_is_dir(resolved) || !tb_directory_readable(resolved))
            {
                free(resolved);
                c->unavailable[entry->string[0] - 'A'] = 1;
                continue;
            }
            path = keep(c, resolved);
        }
        else
            path = resolve_path(c, source, 1);
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
    c->ball = resolve_path(c, path, 0);
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
static int current_drive(Context *c)
{
    const char *letter = field(c->manifest, "currentDrive");
    return letter && strlen(letter) == 1 && letter[0] >= 'A' && letter[0] <= 'Z' &&
                   c->roots[letter[0] - 'A']
               ? letter[0] - 'A'
               : 2;
}
static const char *drive_directory(Context *c, int drive)
{
    char key[2] = {(char)('A' + drive), 0};
    const char *path = field(get(c->application, "workingDirectories"), key);
    if (!path)
        path = field(get(c->manifest, "workingDirectories"), key);
    return path ? path : format(c, "%c:\\", 'A' + drive);
}
static char *host_path(Context *c, const char *name, int explicit_host)
{
    if (!name || !*name)
    {
        fail(c, "An executable or directory path is required");
        return NULL;
    }
    int named_drive = isalpha((unsigned char)name[0]) && name[1] == ':';
    /* Absolute host paths remain available to CLI adapters. Windows callers
       disambiguate host drive letters with explicit_host. */
    int guest = !explicit_host && (named_drive || name[0] == '\\' || name[0] != '/');
    if (guest)
    {
        int drive = named_drive ? toupper((unsigned char)name[0]) - 'A' : current_drive(c);
        if (c->unavailable[drive])
        {
            char key[2] = {(char)('A' + drive), 0};
            if (field(get(get(c->manifest, "drives"), key), "image"))
                fail(
                    c,
                    "No media is inserted in drive %c: because its image is unavailable. Attach an image to reconnect it.",
                    drive + 'A');
            else
                fail(
                    c,
                    "Drive %c: is ejected because its folder is unavailable. Choose a mountpoint in Drives.",
                    drive + 'A');
            return NULL;
        }
        if (!c->roots[drive])
        {
            fail(c, "Drive %c: is not mapped", drive + 'A');
            return NULL;
        }
        const char *tail = name + (named_drive ? 2 : 0);
        int rooted = *tail == '\\' || *tail == '/';
        while (*tail == '\\' || *tail == '/')
            tail++;
        const char *base = rooted ? "" : drive_directory(c, drive);
        if (!base)
            return NULL;
        if (*base)
        {
            if (strlen(base) < 3 || toupper((unsigned char)base[0]) != drive + 'A' ||
                base[1] != ':' || (base[2] != '\\' && base[2] != '/'))
            {
                fail(c, "Invalid working directory for drive %c:", drive + 'A');
                return NULL;
            }
            base += 3;
        }
        char *relative = format(c, "%s/%s", base, tail);
        if (!relative)
            return NULL;
        for (char *p = relative; *p; p++)
            if (*p == '\\')
                *p = '/';
        char *path = case_path(c, join(c, c->roots[drive], relative));
        /* A path must stay in its own drive, even when another drive contains it. */
        if (path && inside(path, c->roots[drive]))
            return path;
        fail(c, "Path is outside mapped drive %c: %s", drive + 'A', name);
        return NULL;
    }
    char *path = case_path(c, name);
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
static char *guest_for_input(Context *c, const char *host, const char *input)
{
    if (input && isalpha((unsigned char)input[0]) && input[1] == ':')
    {
        int drive = toupper((unsigned char)input[0]) - 'A';
        if (c->roots[drive] && inside(host, c->roots[drive]))
        {
            const char *relative = host + strlen(c->roots[drive]);
            while (*relative == '/' || *relative == '\\')
                relative++;
            char *path = format(c, "%c:\\%s", 'A' + drive, relative);
            if (path)
                for (char *p = path; *p; p++)
                    if (*p == '/')
                        *p = '\\';
            return path;
        }
    }
    return guest_path(c, host);
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
        else if (!strcmp(entry->string, "display_dpi"))
        {
            char *end = NULL;
            long number = strtol(v, &end, 10);
            if (end == v || *end || number < 72 || number > 384)
                return fail(c, "display_dpi must be a whole number between 72 and 384");
        }
        else if (!strcmp(entry->string, "opengl"))
        {
            if (strcmp(v, "mesa") && strcmp(v, "original") && strcmp(v, "off"))
                return fail(c, "opengl must be mesa, original or off");
        }
        else if (!strcmp(entry->string, "window_chrome"))
        {
            if (strcmp(v, "native") && strcmp(v, "os2") && strcmp(v, "both"))
                return fail(c, "window_chrome must be native, os2 or both");
        }
        else if (!strcmp(entry->string, "trace_native") || !strcmp(entry->string, "trace_events") ||
                 !strcmp(entry->string, "video") || !strcmp(entry->string, "pcm") ||
                 !strcmp(entry->string, "midi") || !strcmp(entry->string, "dive"))
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
static cJSON *guest_environment(Context *c, const cJSON *overrides);
static cJSON *os2_support(Context *c);
static cJSON *dependency_audit_latest(Context *c);
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
    cJSON_AddItemToObject(value, "os2Support", os2_support(c));
    cJSON *dependency_audit = dependency_audit_latest(c);
    if (dependency_audit)
        cJSON_AddItemToObject(value, "dependencyAudit", dependency_audit);
    cJSON *records = cJSON_AddObjectToObject(value, "applicationRecords"), *application;
    cJSON_ArrayForEach(application, get(c->manifest, "applications"))
    {
        char *encoded = cJSON_PrintUnformatted(application);
        if (encoded)
        {
            cJSON_AddStringToObject(records, application->string, encoded);
            cJSON_free(encoded);
        }
    }
    cJSON *drive_records = cJSON_AddObjectToObject(value, "driveRecords"), *drive_entry;
    cJSON_ArrayForEach(drive_entry, get(c->manifest, "drives"))
    {
        char *encoded = cJSON_PrintUnformatted(drive_entry);
        if (encoded)
        {
            cJSON_AddStringToObject(drive_records, drive_entry->string, encoded);
            cJSON_free(encoded);
        }
    }
    cJSON *image_sources = cJSON_AddObjectToObject(value, "imageSources");
    for (int i = 0; i < 26; i++)
        if (c->roots[i] && tb_image_path(c->roots[i]))
        {
            char *source = tb_image_source(c->roots[i]);
            if (source)
            {
                char letter[2] = {(char)('A' + i), 0};
                cJSON_AddStringToObject(image_sources, letter, source);
                free(source);
            }
        }
    cJSON_AddItemToObject(value, "roots", mapping);
    cJSON *unavailable = cJSON_AddObjectToObject(value, "unavailableDrives");
    cJSON_ArrayForEach(drive_entry,
                       get(c->manifest, "drives")) if (c->unavailable[drive_entry->string[0] - 'A'])
        cJSON_AddStringToObject(unavailable, drive_entry->string,
                                str(drive_entry) ? str(drive_entry) : field(drive_entry, "image"));
    for (int i = 0; i < 26; i++)
        if (c->roots[i])
        {
            char letter[2] = {(char)('A' + i), 0};
            cJSON_AddStringToObject(mapping, letter, c->roots[i]);
        }
    cJSON *directories = cJSON_AddObjectToObject(value, "workingDirectories");
    for (int i = 0; i < 26; i++)
        if (c->roots[i] || c->unavailable[i])
        {
            char key[2] = {(char)('A' + i), 0};
            cJSON_AddStringToObject(directories, key, drive_directory(c, i));
        }
    cJSON_AddStringToObject(value, "currentDrive", format(c, "%c", 'A' + current_drive(c)));
    cJSON *env = guest_environment(c, NULL);
    if (!env)
    {
        cJSON_Delete(value);
        return NULL;
    }
    if (field(env, "OS2PATH"))
    {
        put(env, "PATH", cJSON_CreateString(field(env, "OS2PATH")));
        cJSON_DeleteItemFromObjectCaseSensitive(env, "OS2PATH");
    }
    cJSON_AddItemToObject(value, "environmentDefaults", env);
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
                                   "TWOINE_DISPLAY_DPI",
                                   "TWOINE_IPC_DIR",
                                   "DYLD_LIBRARY_PATH",
                                   NULL};
static int init_ball(Context *c, const char *path)
{
    if (!path)
        return fail(c, "A ball path is required");
    c->ball = resolve_path(c, path, 0);
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
#include "audio.h"

static int runtime_name(const char *s)
{
    size_t n = strlen(s);
    return !strcmp(s, "runtime-capabilities.json") || !strcmp(s, "lx_loader") ||
           !strcmp(s, "lx_loader.exe") || (n > 4 && !strcmp(s + n - 4, ".dll")) ||
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
/* Overlay expressions resolve against the complete inherited layer. A
   self-reference appends to the inherited value; other overrides resolve
   recursively so JSON key ordering cannot affect results. */
static char *expand_env(Context *c, const cJSON *base, const cJSON *overlay, const char *key,
                        const char **stack, unsigned depth)
{
    if (depth >= 64)
    {
        fail(c, "Environment expansion is too deep");
        return NULL;
    }
    for (unsigned i = 0; i < depth; i++)
        if (!strcmp(stack[i], key))
        {
            fail(c, "Environment expansion cycle at %s", key);
            return NULL;
        }
    const char *raw = field(overlay, key);
    if (!raw)
        return keep(c, strdup(field(base, key) ? field(base, key) : ""));
    stack[depth] = key;
    char *out = keep(c, strdup(""));
    for (const char *at = raw; *at && out;)
    {
        const char *begin = strchr(at, '%');
        if (!begin)
            return format(c, "%s%s", out, at);
        const char *end = strchr(begin + 1, '%');
        if (!end)
            return format(c, "%s%s", out, at);
        char *ref = keep(c, strdup(begin + 1));
        if (!ref)
            return NULL;
        ref[end - begin - 1] = 0;
        for (char *p = ref; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        if (!strcmp(ref, "PATH"))
            ref = "OS2PATH";
        char *value;
        if (!strcmp(ref, key))
            value = keep(c, strdup(field(base, key) ? field(base, key) : ""));
        else if (field(overlay, ref))
            value = expand_env(c, base, overlay, ref, stack, depth + 1);
        else
            value = keep(c, strdup(field(base, ref) ? field(base, ref) : ""));
        if (!value)
            return NULL;
        out = format(c, "%s%.*s%s", out, (int)(begin - at), at, value);
        if (out && strlen(out) > 1024 * 1024)
        {
            fail(c, "Expanded environment value is too large");
            return NULL;
        }
        at = end + 1;
    }
    return out;
}
static int overlay_environment(Context *c, cJSON *env, const cJSON *settings)
{
    if (!settings)
        return 1;
    if (!cJSON_IsObject(settings))
        return fail(c, "Invalid environment object");
    cJSON *normalized = cJSON_CreateObject(), *entry;
    cJSON_ArrayForEach(entry, settings)
    {
        if (!entry->string || !valid_env_key(entry->string) || !str(entry))
        {
            fail(c, "Invalid environment entry");
            break;
        }
        char *key = keep(c, strdup(entry->string));
        if (!key)
            break;
        for (char *p = key; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        if (!strcmp(key, "PATH"))
            key = "OS2PATH";
        if (reserved_env(key) || !strncmp(key, "TWOINE_DRIVE_CWD_", 17) ||
            !strcmp(key, "TWOINE_CURRENT_DRIVE"))
        {
            fail(c, "Reserved host environment variable: %s", key);
            break;
        }
        if (get(normalized, key))
        {
            fail(c, "Duplicate environment variable: %s", key);
            break;
        }
        put(normalized, key, cJSON_CreateString(str(entry)));
    }
    cJSON *inherited = cJSON_Duplicate(env, 1);
    if (!c->error)
        cJSON_ArrayForEach(entry, normalized)
        {
            const char *stack[64];
            char *value = expand_env(c, inherited, normalized, entry->string, stack, 0);
            if (!value)
                break;
            put(env, entry->string, cJSON_CreateString(value));
        }
    cJSON_Delete(inherited);
    cJSON_Delete(normalized);
    return !c->error;
}
static cJSON *guest_environment(Context *c, const cJSON *overrides)
{
    cJSON *env = cJSON_CreateObject();
    for (int i = 0; default_keys[i]; i++)
        cJSON_AddStringToObject(env, default_keys[i], default_values[i]);
    char *interpreter = host_path(c, "C:\\OS2\\CMD.EXE", 0);
    if (!interpreter)
        goto failed;
    if (tb_is_file(interpreter))
        cJSON_AddStringToObject(env, "COMSPEC", "C:\\OS2\\CMD.EXE");
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
    if (!overlay_environment(c, env, get(c->manifest, "environment")) ||
        !overlay_environment(c, env, overrides))
        goto failed;
    cJSON *entry;
    cJSON_ArrayForEach(entry, env)
    {
        if (!valid_env_key(entry->string) || !str(entry) || reserved_env(entry->string))
        {
            fail(c, "Invalid or reserved environment entry: %s", entry->string);
            goto failed;
        }
    }
    return env;
failed:
    cJSON_Delete(env);
    return NULL;
}
static cJSON *environment(Context *c, const char *build, const char *override, const cJSON *app_env)
{
    cJSON *env = guest_environment(c, app_env);
    if (!env)
        return NULL;
    const char *assets[][2] = {{"TWOINE_FONT_DIRECTORY", "fonts"},
                               {"TWOINE_SOUNDFONT", "soundfonts/FluidR3_GM.sf2"},
                               {"TWOINE_KOSMICKRISP_ICD", "vulkan/icd.d/kosmickrisp.json"}};
    for (int i = 0; i < 3; i++)
    {
        char *asset = join(c, build, assets[i][1]);
        if (tb_is_file(asset) || tb_is_dir(asset))
            put(env, assets[i][0], cJSON_CreateString(asset));
    }
    if (!audio_config(c, build, env))
        goto failed;
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
            if (isalpha((unsigned char)part[0]) && part[1] == ':' &&
                c->unavailable[toupper((unsigned char)part[0]) - 'A'])
            {
                part = next;
                continue;
            }
            char *path = !strcmp(part, ".") ? part : host_path(c, part, 0);
            if (!path)
                goto failed;
            libpath = format(c, "%s%s%s", libpath, PATH_SEPARATOR, path);
        }
        part = next;
    }
    const char *keys[] = {"TWOINE_IPC_DIR",
                          "TWOINE_BALL",
                          "TWOINE_CONFIG",
                          "TWOINE_USER_PROFILE",
                          "TWOINE_SYSTEM_PROFILE",
                          "TWOINE_REGISTRY",
                          "TWOINE_OBJECTS",
                          "TWOINE_LIBPATH",
                          "TWOINE_RUNTIME_ID",
                          "TWOINE_DISPLAY_DPI",
                          NULL};
    const char *values[] = {join(c, c->ball, "ipc"),
                            c->ball,
                            join(c, c->ball, "2ine.cfg"),
                            host_path(c, field(env, "USER_INI"), 0),
                            host_path(c, field(env, "SYSTEM_INI"), 0),
                            join(c, c->roots[2], "OS2/REGISTRY.INI"),
                            join(c, c->roots[2], "OS2/OBJECTS.INI"),
                            libpath,
                            identity,
                            get(get(c->manifest, "system"), "display_dpi")
                                ? setting_text(c, get(get(c->manifest, "system"), "display_dpi"))
                                : "96"};
    if (c->error)
        goto failed;
    for (int i = 0; keys[i]; i++)
        put(env, keys[i], cJSON_CreateString(values[i]));
    const char *ld = getenv("LD_LIBRARY_PATH");
    put(env, "LD_LIBRARY_PATH",
        cJSON_CreateString(format(c, "%s%s%s%s%s", runtime, PATH_SEPARATOR, build,
                                  ld && *ld ? PATH_SEPARATOR : "", ld ? ld : "")));
    if (tb_host_uses_dyld())
    {
        const char *dyld = getenv("DYLD_LIBRARY_PATH");
        put(env, "DYLD_LIBRARY_PATH",
            cJSON_CreateString(format(c, "%s%s%s%s%s", runtime, PATH_SEPARATOR, build,
                                      dyld && *dyld ? PATH_SEPARATOR : "", dyld ? dyld : "")));
    }
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
static int validate_directories(Context *c, const cJSON *directories);
static char *command_interpreter(Context *c)
{
    cJSON *env = guest_environment(c, get(c->application, "environment"));
    if (!env)
        return NULL;
    const char *comspec = field(env, "COMSPEC");
    if (comspec && *comspec)
    {
        char *host = host_path(c, comspec, 0);
        cJSON_Delete(env);
        if (host && tb_is_file(host))
            return host;
        fail(c, "COMSPEC must name an accessible original OS/2 command interpreter");
        return NULL;
    }
    cJSON_Delete(env);
    for (int i = 0; i < 26; i++)
    {
        int drive = i == 0 ? 2 : i <= 2 ? i - 1 : i;
        if (!c->roots[drive])
            continue;
        const char *suffixes[] = {"OS2/CMD.EXE", "CMD.EXE", "OS2IMAGE/DISK_2/CMD.EXE",
                                  "OS2IMAGE/DISK_1/CMD.EXE", NULL};
        for (int j = 0; suffixes[j]; j++)
        {
            char *host = case_path(c, join(c, c->roots[drive], suffixes[j]));
            if (host && tb_is_file(host))
                return host;
            if (errno == ENOMEM)
                return NULL;
            free(c->error);
            c->error = NULL;
        }
    }
    fail(
        c,
        "This command file needs the original OS/2 CMD.EXE. Choose Install OS/2 Support Files to extract it from your OS/2 CD or floppy images, or use install-os2-support in the CLI. Then run the command file again.");
    return NULL;
}
#include "os2_support.h"
#include "os2_extract.h"
#include "os2_dependencies.h"
#include "configuration.h"
#include "runtime_selection.h"
#include "runtime_features.h"

static cJSON *prepare(Context *c, const cJSON *request)
{
    const char *build_path = field(request, "build");
    if (!build_path || !*build_path)
    {
        fail(c, "Choose a runtime build");
        return NULL;
    }
    char *build = resolve_path(c, build_path, 1);
    if (!build)
        return NULL;
    const char *loader = loader_name(c, build);
    if (!loader)
        return NULL;
    cJSON *args = get(request, "arguments");
    if (!array_strings(c, args) || !cJSON_GetArraySize(args))
        return NULL;
    const char *action = str(cJSON_GetArrayItem(args, 0));
    c->application = request;
    if (!validate_directories(c, get(request, "workingDirectories")))
        return NULL;
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
        c->application = app;
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
        fail(c, "Program does not exist: %s", program);
        goto failed;
    }
    /* SmartSuite's executable installer invokes CMD.EXE for child commands. */
    if (!strcmp(action, "install"))
    {
        char *folder = keep(c, strdup(host));
        char *slash = folder ? strrchr(folder, '/') : NULL;
        if (slash)
        {
            *slash = 0;
            size_t n = 0;
            char **names = tb_names(folder, &n);
            int suite = 0;
            if (n != (size_t)-1)
                for (size_t i = 0; i < n; i++)
                    if (equal_case(names[i], "SUITE.RSP"))
                        suite = 1;
            tb_names_free(names, n == (size_t)-1 ? 0 : n);
            if (suite)
            {
                cJSON *support = os2_support(c);
                int complete = support && cJSON_IsTrue(get(support, "complete"));
                cJSON_Delete(support);
                if (!complete)
                {
                    fail(
                        c,
                        "SmartSuite needs original OS/2 support files, including CMD.EXE. Choose Install OS/2 Support Files and select your OS/2 CD or floppy images, then retry the installer.");
                    goto failed;
                }
            }
        }
    }
    /* Installers usually open companion files relative to their own directory. */
    if (!strcmp(action, "install") && !cwd)
    {
        const char *guest = guest_for_input(c, host, program);
        if (guest && !field(get(c->application, "workingDirectories"), format(c, "%c", guest[0])))
        {
            const char *slash = strrchr(guest, '\\');
            if (slash)
                cwd = format(c, "%.*s", (int)(slash == guest + 2 ? 3 : slash - guest), guest);
        }
    }
    char *original_host = host;
    const char *extension = strrchr(program, '.');
    if (extension && (equal_case(extension, ".CMD") || equal_case(extension, ".BAT")))
    {
        const char *script = guest_for_input(c, host, program);
        host = command_interpreter(c);
        if (!host || !script)
            goto failed;
        FILE *command = tb_fopen(original_host, "rb");
        int rexx = command && fgetc(command) == '/' && fgetc(command) == '*';
        if (command)
            fclose(command);
        if (rexx)
        {
            cJSON *support = os2_support(c);
            int ready = support && cJSON_IsTrue(get(support, "rexxComplete"));
            cJSON_Delete(support);
            if (!ready)
            {
                fail(
                    c,
                    "This REXX command file needs the original OS/2 REXX interpreter and DLLs. Choose Install OS/2 Support Files and select your OS/2 CD or REXX installation floppy, then retry.");
                goto failed;
            }
        }
        cJSON_InsertItemInArray(argv, 0, cJSON_CreateString(script));
        cJSON_InsertItemInArray(argv, 0, cJSON_CreateString("CALL"));
        cJSON_InsertItemInArray(argv, 0, cJSON_CreateString("/C"));
    }
    char *working = NULL;
    if (cwd && *cwd)
        working = host_path(c, cwd, 0);
    else
    {
        const char *guest = guest_for_input(c, original_host, program);
        if (guest)
            working = host_path(c, drive_directory(c, guest[0] - 'A'), 0);
    }
    if (!working || !tb_is_dir(working))
    {
        fail(c, "Working directory must be a mapped directory");
        goto failed;
    }
    if (!runtime_features_validate(c, build) || !write_config(c))
        goto failed;
    const char *override = field(request, "backend");
    const char *saved_backend = field(get(c->manifest, "runtime"), "backend");
    const char *wanted = override        ? override
                         : saved_backend ? saved_backend
                                         : getenv("TWOINE_BACKEND");
    const char *backend = effective_backend(c, build, wanted, !override && saved_backend != NULL);
    if (!backend)
        goto failed;
    cJSON *env = environment(c, build, !strcmp(backend, "default") ? NULL : backend,
                             get(c->application, "environment"));
    if (!env)
        goto failed;
    for (int i = 0; i < 26; i++)
        if (c->roots[i])
        {
            const char *directory = drive_directory(c, i);
            char *mapped = directory ? host_path(c, directory, 0) : NULL;
            if (!mapped || !tb_is_dir(mapped))
            {
                cJSON_Delete(env);
                fail(c, "Working directory for %c: is not accessible", 'A' + i);
                goto failed;
            }
            put(env, format(c, "TWOINE_DRIVE_CWD_%c", 'A' + i), cJSON_CreateString(directory + 3));
        }
    const char *guest_working = guest_for_input(c, working, cwd && *cwd ? cwd : program);
    if (!guest_working)
    {
        cJSON_Delete(env);
        goto failed;
    }
    put(env, "TWOINE_CURRENT_DRIVE", cJSON_CreateString(format(c, "%c", guest_working[0])));
    put(env, format(c, "TWOINE_DRIVE_CWD_%c", guest_working[0]),
        cJSON_CreateString(guest_working + 3));
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
    cJSON_AddItemToArray(command, cJSON_CreateString(guest_for_input(c, host, program)));
    cJSON *a;
    cJSON_ArrayForEach(a, argv) cJSON_AddItemToArray(command, cJSON_Duplicate(a, 1));
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "command", command);
    cJSON_AddStringToObject(result, "cwd", tb_image_path(working) ? c->roots[2] : working);
    cJSON_AddStringToObject(result, "programHost", original_host);
    cJSON_AddItemToObject(result, "environment", env);
    cJSON *unset = cJSON_AddArrayToObject(result, "unsetEnvironment");
    for (int i = 0; unset_keys[i]; i++)
        cJSON_AddItemToArray(unset, cJSON_CreateString(unset_keys[i]));
    cJSON *receipt = cJSON_AddObjectToObject(result, "receipt");
    cJSON_AddItemToObject(receipt, "command", cJSON_Duplicate(command, 1));
    cJSON_AddStringToObject(receipt, "runtime_id", identity);
    cJSON_AddStringToObject(receipt, "cwd", working);
    cJSON_AddStringToObject(receipt, "started", now(c));
    if (!strcmp(action, "install"))
    {
        unsigned char digest[32];
        if (!digest_file(c, original_host, digest))
        {
            cJSON_Delete(result);
            goto failed;
        }
        cJSON_AddStringToObject(receipt, "program_sha256", hex(c, digest));
        cJSON_AddStringToObject(receipt, "program", guest_for_input(c, original_host, program));
        if (tb_image_path(original_host))
        {
            char *source = keep(c, tb_image_source(original_host));
            if (!source)
            {
                cJSON_Delete(result);
                goto failed;
            }
            cJSON_AddStringToObject(receipt, "media_image", source);
        }
    }
    cJSON_AddBoolToObject(result, "installer", !strcmp(action, "install"));
    cJSON *startup = cJSON_AddArrayToObject(result, "startup");
    cJSON *services = get(c->manifest, "startupPrograms");
    if (!cJSON_IsFalse(get(request, "includeStartup")) && services)
    {
        if (!cJSON_IsArray(services) || cJSON_GetArraySize(services) > 16)
        {
            fail(c, "startupPrograms must contain at most 16 application records");
            cJSON_Delete(result);
            goto failed;
        }
        cJSON *service;
        cJSON_ArrayForEach(service, services)
        {
            const char *path = field(service, "program");
            if (!path || !*path || !cJSON_IsObject(service))
            {
                fail(c, "A startup program needs an executable path");
                cJSON_Delete(result);
                goto failed;
            }
            if (equal_case(path, program))
                continue;
            cJSON *launch = cJSON_Duplicate(service, 1);
            cJSON *arguments = cJSON_CreateArray();
            cJSON_AddItemToArray(arguments, cJSON_CreateString("run"));
            const char *directory = field(service, "cwd");
            if (directory && *directory)
            {
                cJSON_AddItemToArray(arguments, cJSON_CreateString("--cwd"));
                cJSON_AddItemToArray(arguments, cJSON_CreateString(directory));
            }
            cJSON_AddItemToArray(arguments, cJSON_CreateString(path));
            cJSON *extra = get(service, "arguments");
            if (extra && !array_strings(c, extra))
            {
                cJSON_Delete(arguments);
                cJSON_Delete(launch);
                cJSON_Delete(result);
                goto failed;
            }
            cJSON_ArrayForEach(a, extra) cJSON_AddItemToArray(arguments, cJSON_Duplicate(a, 1));
            put(launch, "arguments", arguments);
            put(launch, "includeStartup", cJSON_CreateFalse());
            put(launch, "build", cJSON_CreateString(build));
            put(launch, "backend", cJSON_CreateString(backend));
            const cJSON *application = c->application;
            cJSON *prepared = prepare(c, launch);
            c->application = application;
            cJSON_Delete(launch);
            if (!prepared)
            {
                cJSON_Delete(result);
                goto failed;
            }
            cJSON_AddItemToArray(startup, prepared);
        }
    }
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
    if (!strcmp(action, "map") || !strcmp(action, "map-image") || !strcmp(action, "unmap") ||
        !strcmp(action, "device") || !strcmp(action, "eject") || !strcmp(action, "restore-drive") ||
        !strcmp(action, "create-floppy") || !strcmp(action, "create-disk") ||
        !strcmp(action, "device-lock") || !strcmp(action, "media-readonly"))
    {
        if (!name || !isalpha((unsigned char)name[0]) || (name[1] && strcmp(name + 1, ":")))
            return fail(c, "Choose a drive A-Z other than C:");
        char letter[2] = {(char)toupper((unsigned char)name[0]), 0};
        if (letter[0] == 'C')
            return fail(c, "C: is private to the ball");
        cJSON *drives = object(c->manifest, "drives");
        if (!drives)
            return fail(c, "Invalid drives object");
        cJSON *existing = get(drives, letter);
        if (cJSON_IsTrue(get(existing, "locked")) && strcmp(action, "device-lock"))
            return fail(
                c,
                "Drive %s: is locked to its image. Unlock it in Settings before changing its media or access mode.",
                letter);
        if (!strcmp(action, "device-lock") || !strcmp(action, "media-readonly"))
        {
            const char *value = str(cJSON_GetArrayItem(args, 2));
            if (!value || (strcmp(value, "on") && strcmp(value, "off")))
                return fail(c, "Choose on or off");
            if (!field(existing, "device"))
                return fail(c, "Choose a removable device");
            int enabled = !strcmp(value, "on");
            if (!strcmp(action, "device-lock"))
            {
                if (enabled && !field(existing, "image"))
                    return fail(c, "Attach an image before locking this device");
                put(existing, "locked", cJSON_CreateBool(enabled));
            }
            else
            {
                if (!strcmp(field(existing, "device"), "cdrom") && !enabled)
                    return fail(c, "CD images are read-only");
                put(existing, "readonly", cJSON_CreateBool(enabled));
            }
            return write_config(c) && save_manifest(c);
        }
        if (!strcmp(action, "create-disk"))
        {
            const char *source = str(cJSON_GetArrayItem(args, 2)),
                       *filesystem = str(cJSON_GetArrayItem(args, 3)),
                       *size = str(cJSON_GetArrayItem(args, 4));
            char *end = NULL;
            unsigned long long mib = size ? strtoull(size, &end, 10) : 0;
            if (!source || !filesystem || !size || !end || *end || mib < 8 || mib > 2097152)
                return fail(c, "Choose an image size from 8 MiB to 2 TiB");
            if (strcmp(filesystem, "fat32") && strcmp(filesystem, "exfat") &&
                strcmp(filesystem, "hpfs"))
                return fail(c, "Choose FAT32, exFAT or HPFS");
            if (!strcmp(filesystem, "fat32") && mib < 64)
                return fail(c, "FAT32 images need at least 64 MiB");
            if (!strcmp(filesystem, "hpfs") && (mib > 65536 || mib % 8))
                return fail(c, "Choose an HPFS size from 8 MiB to 64 GiB, in multiples of 8 MiB");
            if (existing &&
                (!field(existing, "device") || strcmp(field(existing, "device"), "disk")))
                return fail(c, "Choose a disk device or an unused drive letter");
            char *path = resolve_path(c, source, 0);
            if (!path)
                return 0;
            if (!tb_image_create_volume(path, filesystem, (uint64_t)mib * 1024 * 1024))
                return fail(c, "Cannot create %s image %s: %s", filesystem, path, strerror(errno));
            cJSON *mapping = existing ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
            put(mapping, "device", cJSON_CreateString("disk"));
            put(mapping, "image", cJSON_CreateString(path));
            put(drives, letter, mapping);
            return write_config(c) && save_manifest(c);
        }
        if (!strcmp(action, "create-floppy"))
        {
            const char *source = str(cJSON_GetArrayItem(args, 2)),
                       *size = str(cJSON_GetArrayItem(args, 3));
            char *end = NULL;
            unsigned long kib = size ? strtoul(size, &end, 10) : 0;
            if (!source || !size || !end || *end ||
                (kib != 360 && kib != 720 && kib != 1200 && kib != 1440 && kib != 2880))
                return fail(c, "Choose a 360, 720, 1200, 1440 or 2880 KiB floppy image");
            if (existing &&
                (!field(existing, "device") || strcmp(field(existing, "device"), "floppy")))
                return fail(c, "Choose a floppy device or an unused drive letter");
            char *path = resolve_path(c, source, 0);
            if (!path)
                return 0;
            if (!tb_image_create_floppy(path, (unsigned)kib))
                return fail(c, "Cannot create floppy image %s: %s", path, strerror(errno));
            cJSON *mapping = existing ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
            put(mapping, "device", cJSON_CreateString("floppy"));
            put(mapping, "image", cJSON_CreateString(path));
            put(drives, letter, mapping);
            return write_config(c) && save_manifest(c);
        }
        if (!strcmp(action, "map-image"))
        {
            const char *source = str(cJSON_GetArrayItem(args, 2));
            if (!source)
                return fail(c, "An ISO or floppy image is required");
            char *image = resolve_path(c, source, 1);
            if (!image)
                return 0;
            char *root = keep(c, tb_image_root(image));
            TbImageInfo info;
            if (!root || !tb_image_info(root, &info))
            {
                int error = errno;
                return fail(c, "Cannot read ISO 9660 or FAT12/FAT16 image %s: %s", image,
                            strerror(error));
            }
            cJSON *previous = get(drives, letter);
            cJSON *mapping =
                cJSON_IsObject(previous) ? cJSON_Duplicate(previous, 1) : cJSON_CreateObject();
            const char *detected = !strcmp(info.filesystem, "CDFS")  ? "cdrom"
                                   : !strcmp(info.filesystem, "FAT") ? "floppy"
                                                                     : "disk";
            const char *kind = field(get(drives, letter), "device");
            if (kind && strcmp(kind, detected) &&
                (strcmp(kind, "disk") || !strcmp(detected, "cdrom")))
            {
                cJSON_Delete(mapping);
                return fail(c, "Choose %s media for %s:",
                            !strcmp(kind, "floppy") ? "a FAT12/FAT16 floppy" : "an ISO 9660 CD",
                            letter);
            }
            put(mapping, "device", cJSON_CreateString(kind ? kind : detected));
            put(mapping, "image", cJSON_CreateString(image));
            put(drives, letter, mapping);
        }
        else if (!strcmp(action, "device"))
        {
            const char *kind = str(cJSON_GetArrayItem(args, 2));
            if (!kind || (strcmp(kind, "cdrom") && strcmp(kind, "floppy") && strcmp(kind, "disk")))
                return fail(c, "Choose a cdrom, floppy or disk device");
            if (get(drives, letter))
                return fail(c, "Drive %s: is already allocated", letter);
            cJSON *mapping = cJSON_CreateObject();
            cJSON_AddStringToObject(mapping, "device", kind);
            put(drives, letter, mapping);
        }
        else if (!strcmp(action, "eject"))
        {
            cJSON *mapping = get(drives, letter);
            const char *kind = field(mapping, "device");
            if (!kind && !field(mapping, "image"))
                return fail(c, "%s: is not a removable device", letter);
            if (!kind)
                cJSON_AddStringToObject(mapping, "device", "cdrom");
            cJSON_DeleteItemFromObjectCaseSensitive(mapping, "image");
            cJSON_DeleteItemFromObjectCaseSensitive(get(c->manifest, "workingDirectories"), letter);
        }
        else if (!strcmp(action, "restore-drive"))
        {
            const char *record = str(cJSON_GetArrayItem(args, 2));
            cJSON *mapping = record ? cJSON_Parse(record) : NULL;
            if (!mapping ||
                (!str(mapping) && !field(mapping, "device") && !field(mapping, "image")))
            {
                cJSON_Delete(mapping);
                return fail(c, "Invalid drive record");
            }
            put(drives, letter, mapping);
        }
        else if (!strcmp(action, "map"))
        {
            const char *dir = str(cJSON_GetArrayItem(args, 2));
            if (!dir)
                return fail(c, "A host directory is required");
            char *path = resolve_path(c, dir, 1);
            if (!path)
                return 0;
            if (!tb_is_dir(path))
                return fail(c, "Not a directory: %s", path);
            if (!tb_directory_readable(path))
            {
                int error = errno;
                return fail(c, "Cannot read directory %s: %s", path,
                            strerror(error ? error : EACCES));
            }
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
        char *absolute = resolve_path(c, build && *build ? build : ".", 0);
        if (!absolute)
            return 0;
        cJSON *env = environment(c, absolute, field(request, "backend"), NULL);
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
        cJSON *existing = get(apps, name);
        cJSON *app = existing ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
        put(app, "program", cJSON_CreateString(guest_for_input(c, host, program)));
        cJSON *argv = cJSON_CreateArray();
        put(app, "arguments", argv);
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
            put(app, "cwd", cJSON_CreateString(guest_for_input(c, directory, cwd)));
        }
        put(apps, name, app);
        return save_manifest(c);
    }
    if (!strcmp(action, "restore-app"))
    {
        const char *encoded = str(cJSON_GetArrayItem(args, 2));
        cJSON *app = encoded ? cJSON_ParseWithOpts(encoded, NULL, 1) : NULL;
        if (!name || !cJSON_IsObject(app) || !field(app, "program") ||
            !cJSON_IsArray(get(app, "arguments")))
        {
            cJSON_Delete(app);
            return fail(c, "Invalid application record");
        }
        cJSON *apps = object(c->manifest, "applications");
        if (!apps || get(apps, name))
        {
            cJSON_Delete(app);
            return fail(c, "An application with that name already exists");
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

static int validate_directories(Context *c, const cJSON *directories)
{
    if (!directories)
        return 1;
    if (!cJSON_IsObject(directories))
        return fail(c, "Working directories must be an object");
    cJSON *entry;
    cJSON_ArrayForEach(entry, directories)
    {
        const char *path = str(entry), *key = entry->string;
        if (!key || strlen(key) != 1 || key[0] < 'A' || key[0] > 'Z' ||
            (!c->roots[key[0] - 'A'] && !c->unavailable[key[0] - 'A']) || !path ||
            strlen(path) < 3 || path[0] != key[0] || path[1] != ':' || path[2] != '\\')
            return fail(c, "Use a mapped drive letter and its absolute OS/2 directory");
        if (c->unavailable[key[0] - 'A'])
            continue;
        char *host = host_path(c, path, 0);
        if (!host || !tb_is_dir(host))
            return fail(c, "Working directory does not exist: %s", path);
    }
    return 1;
}
static cJSON *browse_directory(Context *c, const char *path)
{
    char *host = host_path(c, path, 0);
    if (host && tb_is_file(host))
    {
        host = keep(c, strdup(host));
        if (host)
        {
            char *slash = strrchr(host, '/');
#ifdef _WIN32
            char *back = strrchr(host, '\\');
            if (back && (!slash || back > slash))
                slash = back;
#endif
            if (slash)
                *slash = 0;
        }
    }
    if (!host || !tb_is_dir(host) || !tb_directory_readable(host))
    {
        fail(c, "Choose an accessible mapped directory");
        return NULL;
    }

    /* Preserve the drive the user chose for nested or duplicate mappings. */
    int drive = path && isalpha((unsigned char)path[0]) && path[1] == ':'
                    ? toupper((unsigned char)path[0]) - 'A'
                    : current_drive(c);
    const char *tail = host + strlen(c->roots[drive]);
    while (*tail == '/' || *tail == '\\')
        tail++;
    char *guest = format(c, "%c:\\%s", 'A' + drive, tail);
    if (!guest)
        return NULL;
    for (char *p = guest; *p; p++)
        if (*p == '/')
            *p = '\\';
    cJSON *result = cJSON_CreateObject(), *entries = cJSON_AddArrayToObject(result, "entries");
    if (strlen(guest) > 3)
    {
        const char *last = strrchr(guest, '\\');
        size_t length = last == guest + 2 ? 3 : (size_t)(last - guest);
        cJSON_AddStringToObject(result, "parent", format(c, "%.*s", (int)length, guest));
    }
    else
        cJSON_AddNullToObject(result, "parent");
    cJSON_AddStringToObject(result, "path", guest);
    cJSON_AddStringToObject(result, "hostPath", host);
    size_t count = 0;
    char **names = tb_names(host, &count);
    if (!names && count == (size_t)-1)
    {
        cJSON_Delete(result);
        fail(c, "Cannot read directory: %s", guest);
        return NULL;
    }
    if (count > 1)
        qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0; i < count; i++)
    {
        char *child = join(c, host, names[i]);
        if (strpbrk(names[i], "\\:"))
            continue;
        char *resolved = case_path(c, child);
        if (!resolved || !inside(resolved, c->roots[drive]))
            continue;
        int directory = tb_is_dir(child);
        if (!directory && !tb_is_file(child))
            continue;
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "name", names[i]);
        cJSON_AddStringToObject(
            entry, "path",
            format(c, "%s%s%s", guest, guest[strlen(guest) - 1] == '\\' ? "" : "\\", names[i]));
        cJSON_AddStringToObject(entry, "hostPath", child);
        cJSON_AddBoolToObject(entry, "directory", directory);
        uint64_t size = 0;
        int64_t modified = INT64_MIN;
        if (tb_path_info(child, &size, &modified))
        {
            cJSON_AddNumberToObject(entry, "size", (double)size);
            if (modified != INT64_MIN)
                cJSON_AddNumberToObject(entry, "modified", (double)modified);
        }
        cJSON_AddItemToArray(entries, entry);
    }
    tb_names_free(names, count);
    return result;
}
static int plan_copy(Context *c, cJSON *plan, const char *source, const char *destination,
                     unsigned depth)
{
    if (depth > 64 || cJSON_GetArraySize(plan) > 100000)
        return fail(c, "The dropped folder is too deep or contains too many files");
    if (tb_is_link(source))
        return fail(c, "Symbolic links cannot be imported: %s", source);
    if (!tb_is_dir(source) && !tb_is_file(source))
        return fail(c, "Not a regular file or folder: %s", source);
    char *target = case_path(c, destination);
    if (!target)
        return 0;
    if (tb_is_dir(target) || tb_is_file(target) || tb_is_link(target))
        return fail(c, "A file or folder already exists: %s. Nothing was replaced.", target);
    cJSON *other;
    cJSON_ArrayForEach(
        other, plan) if (equal_case(field(other, "destination"),
                                    target)) return fail(c, "Dropped items have conflicting names");
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "source", source);
    cJSON_AddStringToObject(entry, "destination", target);
    cJSON_AddBoolToObject(entry, "directory", tb_is_dir(source));
    cJSON_AddItemToArray(plan, entry);
    if (tb_is_dir(source))
    {
        if (!tb_directory_readable(source))
            return fail(c, "Cannot read dropped folder: %s", source);
        size_t count = 0;
        char **names = tb_names(source, &count);
        if (!names && count == (size_t)-1)
            return fail(c, "Cannot read dropped folder: %s", source);
        int ok = 1;
        for (size_t i = 0; i < count && ok; i++)
        {
            if (strpbrk(names[i], "\\:"))
            {
                ok = fail(c, "Filename is not an OS/2 filename: %s", names[i]);
                break;
            }
            ok =
                plan_copy(c, plan, join(c, source, names[i]), join(c, target, names[i]), depth + 1);
        }
        tb_names_free(names, count);
        return ok;
    }
    return 1;
}
static cJSON *import_files(Context *c, const cJSON *request)
{
    const char *directory = field(request, "path");
    char *destination = directory ? host_path(c, directory, 0) : NULL;
    if (destination && tb_image_path(destination))
    {
        TbImageInfo info;
        if (!tb_image_info(destination, &info) || info.readonly)
        {
            fail(c, "This image drive is read-only or not ready");
            return NULL;
        }
    }
    cJSON *sources = get(request, "sources");
    if (!destination || !tb_is_dir(destination) || !array_strings(c, sources) ||
        !cJSON_GetArraySize(sources))
    {
        fail(c, "Choose a mapped destination and files to copy");
        return NULL;
    }
    cJSON *plan = cJSON_CreateArray(), *source;
    cJSON_ArrayForEach(source, sources)
    {
        const char *raw = str(source);
        if (tb_is_link(raw))
        {
            fail(c, "Symbolic links cannot be imported: %s", raw);
            break;
        }
        char *path = resolve_path(c, raw, 1);
        if (!path)
            break;
        const char *name = path;
        for (const char *p = path; *p; p++)
            if (*p == '/' || *p == '\\')
                name = p + 1;
        if (!*name || strpbrk(name, ":\\"))
        {
            fail(c, "Choose a file or named folder to copy");
            break;
        }
        if (inside(destination, path))
        {
            fail(c, "Cannot copy a folder into itself");
            break;
        }
        if (!plan_copy(c, plan, path, join(c, destination, name), 0))
            break;
    }
    if (!c->error)
    {
        cJSON *entry;
        cJSON_ArrayForEach(entry, plan)
        {
            const char *src = field(entry, "source"), *dst = field(entry, "destination");
            if (tb_is_link(src) || (cJSON_IsTrue(get(entry, "directory")) ? !tb_mkdir_new(dst)
                                                                          : !tb_copy_new(src, dst)))
            {
                fail(
                    c,
                    "Copy stopped at %s. Existing files were kept; earlier copied items may remain.",
                    dst);
                break;
            }
        }
    }
    cJSON_Delete(plan);
    return c->error ? NULL : browse_directory(c, directory);
}
static int save_application(Context *c, const cJSON *request)
{
    const char *name = field(request, "name"), *program = field(request, "program"),
               *previous = field(request, "originalName");
    if (!name || !*name || !program || !array_strings(c, get(request, "arguments")))
        return fail(c, "Application name, executable and arguments are required");
    cJSON *apps = object(c->manifest, "applications");
    if (!apps)
        return fail(c, "Invalid applications object");
    if (previous && strcmp(previous, name) && get(apps, name))
        return fail(c, "An application with this name already exists");
    cJSON *existing = get(apps, previous ? previous : name);
    cJSON *app = cJSON_IsObject(existing) ? cJSON_Duplicate(existing, 1) : cJSON_CreateObject();
    if (!app)
        return fail(c, "Out of memory");
    cJSON *dirs = get(request, "workingDirectories"), *env = get(request, "environment");
    if (!validate_directories(c, dirs))
        goto failed;
    if (dirs)
        put(app, "workingDirectories", cJSON_Duplicate(dirs, 1));
    if (env)
        put(app, "environment", cJSON_Duplicate(env, 1));
    c->application = app;
    char *host = host_path(c, program, 0);
    if (!host || !tb_is_file(host))
    {
        fail(c, "Program does not exist: %s", program);
        goto failed;
    }
    put(app, "program", cJSON_CreateString(guest_for_input(c, host, program)));
    put(app, "arguments", cJSON_Duplicate(get(request, "arguments"), 1));
    const char *cwd = field(request, "cwd");
    if (cwd && *cwd)
    {
        char *host_cwd = host_path(c, cwd, 0);
        if (!host_cwd || !tb_is_dir(host_cwd))
        {
            fail(c, "Working directory does not exist");
            goto failed;
        }
        put(app, "cwd", cJSON_CreateString(guest_for_input(c, host_cwd, cwd)));
    }
    else
        cJSON_DeleteItemFromObjectCaseSensitive(app, "cwd");
    cJSON *checked = guest_environment(c, get(app, "environment"));
    if (!checked)
        goto failed;
    cJSON_Delete(checked);
    if (get(request, "iconSource"))
    {
        const char *source = field(request, "iconSource");
        if (!source || !*source)
            cJSON_DeleteItemFromObjectCaseSensitive(app, "icon");
        else
        {
            if (!tb_is_file(source))
            {
                fail(c, "Choose an accessible icon image");
                goto failed;
            }
            unsigned char hash[32];
            if (!digest_file(c, source, hash))
                goto failed;
            const char *relative = format(c, "icons/%s.image", hex(c, hash));
            char *target = join(c, c->ball, relative);
            if (!tb_mkdirs(join(c, c->ball, "icons")) ||
                (!tb_is_file(target) && !tb_copy_new(source, target)))
            {
                fail(c, "Cannot import application icon");
                goto failed;
            }
            put(app, "icon", cJSON_CreateString(relative));
        }
    }
    c->application = NULL;
    put(apps, name, app);
    if (previous && strcmp(previous, name))
        cJSON_DeleteItemFromObjectCaseSensitive(apps, previous);
    return save_manifest(c);
failed:
    c->application = NULL;
    cJSON_Delete(app);
    return 0;
}
static cJSON *application_icon(Context *c, const cJSON *request)
{
    const char *name = field(request, "name"), *program = field(request, "program");
    cJSON *app = name ? get(get(c->manifest, "applications"), name) : NULL;
    if (!program)
        program = field(app, "program");
    const char *custom = field(app, "icon");
    cJSON *result = cJSON_CreateObject();
    if (program)
    {
        const char *base = program;
        for (const char *p = program; *p; p++)
            if (*p == '/' || *p == '\\')
                base = p + 1;
        char *fallback = keep(c, strdup(base));
        char *dot = fallback ? strrchr(fallback, '.') : NULL;
        if (dot && dot != fallback)
            *dot = 0;
        cJSON_AddStringToObject(result, "displayName", fallback ? fallback : base);
    }
    if (custom)
    {
        char *path = case_path(c, join(c, c->ball, custom));
        if (!path || !inside(path, c->ball))
        {
            cJSON_Delete(result);
            fail(c, "Icon path leaves the ball");
            return NULL;
        }
        size_t size = 0;
        char *bytes = read_file(c, path, &size, 0);
        if (bytes)
        {
            TbIconImage *icon = tb_icon_bitmap((const unsigned char *)bytes, size);
            if (icon)
            {
                char *rgba = tb_icon_rgba_base64(icon);
                cJSON_AddStringToObject(result, "rgba", rgba);
                cJSON_AddNumberToObject(result, "width", icon->width);
                cJSON_AddNumberToObject(result, "height", icon->height);
                free(rgba);
                tb_icon_free(icon);
            }
            else
            {
                char *encoded = tb_base64((const unsigned char *)bytes, size);
                cJSON_AddStringToObject(result, "image", encoded);
                free(encoded);
            }
            cJSON_AddStringToObject(result, "source", "custom");
            return result;
        }
    }
    else if (program)
    {
        char *host = host_path(c, program, 0);
        if (host && tb_is_file(host))
        {
            size_t size = 0;
            char *bytes = read_file(c, host, &size, 0);
            char *description =
                bytes ? tb_executable_description((const uint8_t *)bytes, size) : NULL;
            if (description)
            {
                cJSON_DeleteItemFromObjectCaseSensitive(result, "displayName");
                cJSON_AddStringToObject(result, "displayName", description);
                free(description);
            }
            TbIconImage *icon =
                bytes ? tb_icon_executable((const unsigned char *)bytes, size) : NULL;
            if (icon)
            {
                char *rgba = tb_icon_rgba_base64(icon);
                cJSON_AddStringToObject(result, "rgba", rgba);
                cJSON_AddNumberToObject(result, "width", icon->width);
                cJSON_AddNumberToObject(result, "height", icon->height);
                cJSON_AddStringToObject(result, "source", "embedded");
                free(rgba);
                tb_icon_free(icon);
                return result;
            }
        }
    }
    cJSON_AddStringToObject(result, "source", "none");
    return result;
}
#include "ball_archive.h"
#include "ball_rename.h"

static cJSON *dispatch(Context *c, const cJSON *request)
{
    const char *action = field(request, "action");
    if (!action)
    {
        fail(c, "An action is required");
        return NULL;
    }
    if (!strcmp(action, "effective-runtime"))
    {
        const char *candidates[] = {field(request, "path"), field(request, "build")};
        for (unsigned i = 0; i < 2; i++)
            if (candidates[i] && *candidates[i] && native_host_path(candidates[i]) &&
                (tb_host_executable(join(c, candidates[i], "lx_loader")) ||
                 tb_host_executable(join(c, candidates[i], "lx_loader.exe"))))
                return cJSON_CreateString(candidates[i]);
        fail(c, "No compatible host runtime is available. Choose a runtime for this computer.");
        return NULL;
    }
    if (!strcmp(action, "effective-backend"))
    {
        const char *build = field(request, "build");
        if (!build || !*build)
        {
            fail(c, "A receiving host runtime is required");
            return NULL;
        }
        const char *backend = effective_backend(c, build, field(request, "path"), 1);
        return backend ? cJSON_CreateString(backend) : NULL;
    }
    if (!strcmp(action, "format-arguments"))
    {
        const cJSON *args = get(request, "arguments");
        if (!array_strings(c, args))
            return NULL;
        char *out = keep(c, strdup(""));
        cJSON *item;
        cJSON_ArrayForEach(item, args)
        {
            const char *value = str(item);
            char *quoted = keep(c, strdup("\""));
            for (const char *p = value; *p && quoted; p++)
                quoted = format(c, "%s%c%s", quoted, *p, *p == '\"' ? "\"" : "");
            out = format(c, "%s%s%s\"", out, *out ? " " : "", quoted);
            if (!out)
                return NULL;
        }
        return cJSON_CreateString(out);
    }
    if (!strcmp(action, "parse-arguments"))
    {
        const char *text = field(request, "text");
        if (!text)
        {
            fail(c, "Argument text is required");
            return NULL;
        }
        cJSON *args = cJSON_CreateArray();
        char *word = keep(c, strdup(""));
        int started = 0;
        char quote = 0;
        for (const char *p = text; *p; p++)
        {
            if (quote)
            {
                if (*p == quote)
                {
                    if (p[1] == quote)
                    {
                        word = format(c, "%s%c", word, *p);
                        p++;
                    }
                    else
                        quote = 0;
                }
                else
                    word = format(c, "%s%c", word, *p);
            }
            else if (*p == '\"' || *p == '\'')
            {
                quote = *p;
                started = 1;
            }
            else if (isspace((unsigned char)*p))
            {
                if (started)
                {
                    cJSON_AddItemToArray(args, cJSON_CreateString(word));
                    word = keep(c, strdup(""));
                    started = 0;
                }
            }
            else
            {
                word = format(c, "%s%c", word, *p);
                started = 1;
            }
            if (!word)
                break;
        }
        if (quote)
            fail(c, "Close the quote in the arguments field.");
        if (c->error)
        {
            cJSON_Delete(args);
            return NULL;
        }
        if (started)
            cJSON_AddItemToArray(args, cJSON_CreateString(word));
        return args;
    }
    if (!strcmp(action, "image-preview"))
    {
        const char *path = field(request, "path");
        if (!path)
        {
            fail(c, "An image path is required");
            return NULL;
        }
        size_t size = 0;
        char *bytes = read_file(c, path, &size, 0);
        if (!bytes)
            return NULL;
        cJSON *result = cJSON_CreateObject();
        char *encoded = tb_base64((const uint8_t *)bytes, size);
        if (!encoded)
        {
            cJSON_Delete(result);
            fail(c, "Image is too large");
            return NULL;
        }
        cJSON_AddStringToObject(result, "source", "custom");
        cJSON_AddStringToObject(result, "image", encoded);
        free(encoded);
        return result;
    }
    if (!strcmp(action, "project-sources"))
    {
        const char *build = field(request, "build");
        if (build && *build)
        {
            char *path = join(c, build, "project-sources.json");
            if (tb_is_file(path))
            {
                char *text = read_file(c, path, NULL, 0);
                cJSON *catalogue = text ? cJSON_Parse(text) : NULL;
                if (!catalogue || !cJSON_IsArray(get(catalogue, "projects")))
                {
                    cJSON_Delete(catalogue);
                    fail(c, "Invalid packaged source catalogue");
                    return NULL;
                }
                return catalogue;
            }
        }
        cJSON *catalogue = cJSON_Parse(tb_project_sources);
        cJSON *project = cJSON_GetArrayItem(get(catalogue, "projects"), 0);
        if (project && strlen(TB_SOURCE_REVISION) == 40)
        {
            put(project, "version", cJSON_CreateString(TB_SOURCE_VERSION));
            put(project, "source",
                cJSON_CreateString(
                    format(c, "https://github.com/bitwisecook/2ine/tree/%s", TB_SOURCE_REVISION)));
            put(project, "download",
                cJSON_CreateString(format(c,
                                          "https://github.com/bitwisecook/2ine/archive/%s.tar.gz",
                                          TB_SOURCE_REVISION)));
        }
        return catalogue;
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
        char *home = tb_home();
        if (!home)
        {
            fail(c, errno == ENOMEM ? "Out of memory" : "Home directory is unavailable");
            return NULL;
        }
        home = keep(c, home);
        if (!home)
            return NULL;
        char *base = xdg && *xdg ? keep(c, strdup(xdg)) : join(c, home, ".local/share");
        char *directory = join(c, base, "2ine/ball");
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
    if (!strcmp(action, "import-ball"))
        return import_ball(c, request);
    if (!strcmp(action, "rename-ball"))
        return rename_ball(c, request);
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
    if (!strcmp(action, "audit-dependencies") || !strcmp(action, "record-install") ||
        !strcmp(action, "open-os2-source") || !strcmp(action, "plan-os2-extraction") ||
        !strcmp(action, "import-os2-support"))
    {
        runtime_feature_policy(c, field(request, "build"));
        if (c->error)
            return NULL;
    }
    if (!strcmp(action, "runtime-features"))
        return runtime_features(c, field(request, "build"));
    if (!strcmp(action, "config-sys"))
    {
        size_t n = 0;
        char *bytes = read_file(c, join(c, c->roots[2], "CONFIG.SYS"), &n, 1);
        return bytes ? cJSON_CreateString(latin1_to_utf8(c, bytes, n)) : NULL;
    }
    if (!strcmp(action, "configuration-files"))
        return configuration_files(c, request);
    if (!strcmp(action, "configuration-file-read"))
        return configuration_file_read(c, request);
    if (!strcmp(action, "configuration-file-write"))
        return configuration_file_write(c, request);
    if (!strcmp(action, "configuration-read"))
        return configuration_read(c, request);
    if (!strcmp(action, "configuration-write"))
        return configuration_write(c, request);
    if (!strcmp(action, "audio-snapshot"))
        return audio_snapshot(c);
    if (!strcmp(action, "save-audio"))
        return audio_save(c, request);
    if (!strcmp(action, "import-soundfont"))
        return audio_import_archive(c, request);
    if (!strcmp(action, "export-ball"))
        return export_ball(c, request);
    if (!strcmp(action, "os2-support"))
        return os2_support(c);
    if (!strcmp(action, "remember-os2-media"))
        return remember_os2_media(c, request);
    if (!strcmp(action, "browse-media"))
        return browse_media(c, field(request, "path"));
    if (!strcmp(action, "open-os2-source"))
        return open_os2_source(c, request);
    if (!strcmp(action, "plan-os2-extraction"))
        return plan_os2_extraction(c, request);
    if (!strcmp(action, "import-os2-support"))
        return get(request, "categories") ? extract_os2_categories(c, request)
                                          : import_os2_support(c, request);
    if (!strcmp(action, "snapshot"))
        return snapshot(c);
    if (!strcmp(action, "browse"))
        return browse_directory(c, field(request, "path")
                                       ? field(request, "path")
                                       : format(c, "%c:", 'A' + current_drive(c)));
    if (!strcmp(action, "import-files"))
        return import_files(c, request);
    if (!strcmp(action, "guest-environment"))
        return guest_environment(c, get(request, "environment"));
    if (!strcmp(action, "save-application"))
        return save_application(c, request) ? snapshot(c) : NULL;
    if (!strcmp(action, "application-icon"))
        return application_icon(c, request);
    if (!strcmp(action, "set-working-directory"))
    {
        const char *path = field(request, "path");
        if (!path || strlen(path) < 3 || path[1] != ':' || path[2] != '\\')
        {
            fail(c, "Use an absolute drive-letter directory");
            return NULL;
        }
        char key[2] = {path[0], 0};
        cJSON *check = cJSON_CreateObject();
        cJSON_AddStringToObject(check, key, path);
        int valid = validate_directories(c, check);
        cJSON_Delete(check);
        if (!valid)
            return NULL;
        cJSON *dirs = object(c->manifest, "workingDirectories");
        if (!dirs)
        {
            fail(c, "Invalid working directories");
            return NULL;
        }
        char *host = host_path(c, path, 0);
        put(dirs, key, cJSON_CreateString(guest_for_input(c, host, path)));
        put(c->manifest, "currentDrive", cJSON_CreateString(key));
        return save_manifest(c) ? snapshot(c) : NULL;
    }
    if (!strcmp(action, "command"))
    {
        if (!command(c, request))
            return NULL;
        return snapshot(c);
    }
    if (!strcmp(action, "runtime-build") || !strcmp(action, "restore-runtime"))
    {
        int restore = !strcmp(action, "restore-runtime");
        const char *name = field(request, "build");
        const char *build = restore ? name : name ? resolve_path(c, name, 1) : NULL;
        if (!restore && name && (!build || !loader_name(c, build)))
            return NULL;
        cJSON *runtime = object(c->manifest, "runtime");
        if (!runtime)
        {
            fail(c, "Invalid runtime object");
            return NULL;
        }
        if (build)
            put(runtime, "build", cJSON_CreateString(build));
        else
            cJSON_DeleteItemFromObjectCaseSensitive(runtime, "build");
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
    if (!strcmp(action, "audit-dependencies"))
    {
        const cJSON *resolve = get(request, "resolve");
        if (resolve && !cJSON_IsBool(resolve))
        {
            fail(c, "resolve must be a boolean");
            return NULL;
        }
        return audit_dependencies(c, request, !resolve || cJSON_IsTrue(resolve));
    }
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
        if (!field(saved, "program_sha256") && !digest_file(c, program, digest))
        {
            cJSON_Delete(saved);
            return NULL;
        }
        if (!field(saved, "program_sha256"))
            put(saved, "program_sha256", cJSON_CreateString(hex(c, digest)));
        put(saved, "exit_code", cJSON_Duplicate(exit, 1));
        put(saved, "finished", cJSON_CreateString(now(c)));
        cJSON *audit = audit_dependencies(c, request, exit->valuedouble == 0);
        if (audit)
        {
            put(saved, "dependencyAudit", dependency_compact(audit));
            cJSON_Delete(audit);
        }
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
        char *build = resolve_path(c, b && *b ? b : ".", 0);
        return build ? environment(c, build, field(request, "backend"),
                                   get(c->application, "environment"))
                     : NULL;
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
    tb_media_session_begin();
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
    tb_media_session_end();
    return c.error || !*result ? 2 : 0;
}
