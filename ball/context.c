/* Public domain. No rights reserved. Wholly AI-authored. */
/* Request context: owned allocations, the first error, paths and JSON files. */
#include "ball/ball_internal.h"
#include "os2/config/config_sys.h"
#include "storage/volume.h"
#include "third_party/sha256/sha256.h"
#include <time.h>

/* Whole-file reads hold the file in memory; larger files are not ball metadata. */
enum
{
    BALL_READ_FILE_LIMIT = 64 * 1024 * 1024
};

enum
{
    BALL_HASH_BUFFER_SIZE = 65536
};

int tb_ball_fail(TbBall *c, const char *format, ...)
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

char *tb_ball_keep(TbBall *c, char *p)
{
    if (!p)
    {
        tb_ball_fail(c, "Out of memory");
        return NULL;
    }
    char **next = realloc(c->allocations, (c->count + 1) * sizeof(*next));
    if (!next)
    {
        free(p);
        tb_ball_fail(c, "Out of memory");
        return NULL;
    }
    c->allocations = next;
    c->allocations[c->count++] = p;
    return p;
}

char *tb_ball_resolve_path(TbBall *c, const char *path, int existing)
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
            tb_ball_fail(c, "No media is inserted in this drive. Attach an image and try again.");
            return NULL;
        }
        if (tb_image_path(path) && error != ENOMEM)
        {
            char *source = tb_image_source(path);
            if (source)
            {
                if (error == ENOENT && tb_is_file(source))
                    tb_ball_fail(c, "File or folder not found in image %s: %s", source,
                                 tb_image_member(path));
                else
                    tb_ball_fail(c, "Cannot read image %s: %s", source,
                                 strerror(error ? error : EIO));
                free(source);
                return NULL;
            }
        }
        if (error == ENOMEM)
            tb_ball_fail(c, "Out of memory");
        else if (error == ENOENT)
            tb_ball_fail(c, "Path does not exist: %s", path);
        else if (error == EACCES || error == EPERM)
            tb_ball_fail(c, "Permission denied accessing path: %s", path);
        else if (error == ENOTDIR)
            tb_ball_fail(c, "A path component is not a directory: %s", path);
        else
            tb_ball_fail(c, "Cannot resolve path %s: %s", path, strerror(error ? error : EIO));
        return NULL;
    }
    return tb_ball_keep(c, result);
}

char *tb_ball_format(TbBall *c, const char *fmt, ...)
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
    return tb_ball_keep(c, p);
}

char *tb_ball_join(TbBall *c, const char *a, const char *b)
{
    char *path = a && b ? tb_ball_format(c, "%s/%s", a, b) : NULL;
#ifdef _WIN32
    if (path)
        for (char *p = path; *p; p++)
            if (*p == '/')
                *p = '\\';
#endif
    return path;
}

char *tb_ball_read_file(TbBall *c, const char *path, size_t *length, int optional)
{
    FILE *f = tb_fopen(path, "rb");
    if (!f)
    {
        if (optional && !tb_is_file(path) && !tb_is_dir(path))
        {
            if (length)
                *length = 0;
            return tb_ball_keep(c, strdup(""));
        }
        tb_ball_fail(c, "Cannot read %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END))
    {
        fclose(f);
        tb_ball_fail(c, "Cannot seek %s", path);
        return NULL;
    }
    long size = ftell(f);
    rewind(f);
    if (size < 0 || size > BALL_READ_FILE_LIMIT)
    {
        fclose(f);
        tb_ball_fail(c, "File is too large: %s", path);
        return NULL;
    }
    char *p = malloc((size_t)size + 1);
    if (!p)
    {
        fclose(f);
        tb_ball_fail(c, "Out of memory");
        return NULL;
    }
    size_t got = fread(p, 1, (size_t)size, f);
    int ok = !ferror(f) && got == (size_t)size;
    fclose(f);
    if (!ok)
    {
        free(p);
        tb_ball_fail(c, "Cannot read %s", path);
        return NULL;
    }
    p[got] = 0;
    if (length)
        *length = got;
    return tb_ball_keep(c, p);
}

int tb_ball_write_json(TbBall *c, const char *path, cJSON *value)
{
    char *text = cJSON_Print(value);
    if (!text)
        return tb_ball_fail(c, "Cannot serialize JSON");
    size_t n = strlen(text);
    char *line = malloc(n + 2); /* trailing newline and terminator */
    if (!line)
    {
        cJSON_free(text);
        return tb_ball_fail(c, "Out of memory");
    }
    memcpy(line, text, n);
    line[n++] = '\n';
    line[n] = 0;
    int ok = tb_atomic_write(path, line, n);
    free(line);
    cJSON_free(text);
    return ok ? 1 : tb_ball_fail(c, "Cannot save %s", path);
}

char *tb_ball_latin1_to_utf8(TbBall *c, const char *bytes, size_t n)
{
    char *text = malloc(n * 2 + 1); /* each Latin-1 byte becomes at most two UTF-8 bytes */
    if (!text)
    {
        tb_ball_fail(c, "Out of memory");
        return NULL;
    }
    os2_latin1_to_utf8(bytes, n, text);
    return tb_ball_keep(c, text);
}

char *tb_ball_utf8_to_latin1(TbBall *c, const char *text, size_t *length)
{
    char *bytes = malloc(strlen(text) + 1);
    if (!bytes)
    {
        tb_ball_fail(c, "Out of memory");
        return NULL;
    }
    if (!os2_utf8_to_latin1(text, bytes, length))
    {
        free(bytes);
        tb_ball_fail(c, "CONFIG.SYS uses Latin-1; remove characters outside that encoding");
        return NULL;
    }
    return tb_ball_keep(c, bytes);
}

int tb_ball_digest_file(TbBall *c, const char *path, unsigned char hash[TB_BALL_DIGEST_SIZE])
{
    FILE *f = tb_fopen(path, "rb");
    if (!f)
        return tb_ball_fail(c, "Cannot hash runtime file: %s", path);
    SHA256_CTX state;
    sha256_init(&state);
    unsigned char buffer[BALL_HASH_BUFFER_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)))
        sha256_update(&state, buffer, n);
    int ok = !ferror(f);
    fclose(f);
    if (!ok)
        return tb_ball_fail(c, "Cannot read runtime file: %s", path);
    sha256_final(&state, hash);
    return 1;
}

char *tb_ball_hex(TbBall *c, const unsigned char hash[TB_BALL_DIGEST_SIZE])
{
    char *out = malloc(TB_BALL_DIGEST_HEX_LENGTH + 1);
    if (!out)
    {
        tb_ball_fail(c, "Out of memory");
        return NULL;
    }
    for (int i = 0; i < TB_BALL_DIGEST_SIZE; i++)
        sprintf(out + i * 2, "%02x", hash[i]);
    return tb_ball_keep(c, out);
}

char *tb_ball_trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    return s;
}

char *tb_ball_now(TbBall *c)
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
    return tb_ball_keep(c, strdup(date));
}

int tb_ball_array_strings(TbBall *c, const cJSON *array)
{
    if (!cJSON_IsArray(array))
        return tb_ball_fail(c, "Arguments must be an array of strings");
    cJSON *a;
    cJSON_ArrayForEach(a, array) if (!str(a)) return tb_ball_fail(c, "Arguments must be strings");
    return 1;
}

int tb_ball_regenerate(TbBall *c)
{
    return c->hooks && c->hooks->regenerate ? c->hooks->regenerate(c) : tb_ball_roots(c);
}

int tb_ball_validate_settings(TbBall *c, const cJSON *request)
{
    return c->hooks && c->hooks->validate_settings ? c->hooks->validate_settings(c, request) : 1;
}

cJSON *tb_ball_client_snapshot(TbBall *c)
{
    return c->hooks && c->hooks->snapshot ? c->hooks->snapshot(c) : tb_ball_snapshot(c);
}

void tb_ball_detach_application(TbBall *c, cJSON *application)
{
    if (c->hooks && c->hooks->detach_application)
        c->hooks->detach_application(c, application);
}

void tb_ball_hide_application(TbBall *c, const cJSON *application)
{
    if (c->hooks && c->hooks->hide_application)
        c->hooks->hide_application(c, application);
}
