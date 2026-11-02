/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "platform.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <io.h>
#define strdup _strdup
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *w = n ? malloc((size_t)n * sizeof(*w)) : NULL;
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n))
    {
        free(w);
        return NULL;
    }
    return w;
}
static char *utf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, NULL, 0, NULL, NULL);
    char *s = n ? malloc((size_t)n) : NULL;
    if (s && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, s, n, NULL, NULL))
    {
        free(s);
        return NULL;
    }
    return s;
}
char *tb_absolute(const char *path)
{
    wchar_t *w = wide(path);
    if (!w)
        return NULL;
    DWORD n = GetFullPathNameW(w, 0, NULL, NULL);
    wchar_t *full = n ? malloc((size_t)n * sizeof(*full)) : NULL;
    char *result = NULL;
    if (full && GetFullPathNameW(w, n, full, NULL))
        result = utf8(full);
    free(full);
    free(w);
    return result;
}
char *tb_canonical(const char *path)
{
    wchar_t *w = wide(path);
    if (!w)
        return NULL;
    HANDLE f = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(w);
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    DWORD n = GetFinalPathNameByHandleW(f, NULL, 0, FILE_NAME_NORMALIZED);
    wchar_t *full = n ? malloc(((size_t)n + 1) * sizeof(*full)) : NULL;
    char *result = NULL;
    if (full && GetFinalPathNameByHandleW(f, full, n + 1, FILE_NAME_NORMALIZED))
    {
        if (!wcsncmp(full, L"\\\\?\\UNC\\", 8))
        {
            size_t count = wcslen(full + 8);
            wchar_t *unc = malloc((count + 3) * sizeof(*unc));
            if (unc)
            {
                unc[0] = unc[1] = L'\\';
                wcscpy(unc + 2, full + 8);
                result = utf8(unc);
                free(unc);
            }
        }
        else
            result = utf8(wcsncmp(full, L"\\\\?\\", 4) == 0 ? full + 4 : full);
    }
    free(full);
    CloseHandle(f);
    return result;
}
static DWORD attributes(const char *path)
{
    wchar_t *w = wide(path);
    if (!w)
        return INVALID_FILE_ATTRIBUTES;
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a;
}
int tb_is_dir(const char *p)
{
    DWORD a = attributes(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
int tb_is_file(const char *p)
{
    DWORD a = attributes(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
FILE *tb_fopen(const char *p, const char *mode)
{
    wchar_t *w = wide(p), *m = wide(mode);
    FILE *f = w && m ? _wfopen(w, m) : NULL;
    free(w);
    free(m);
    return f;
}
static int mkdir_one(const char *p)
{
    wchar_t *w = wide(p);
    int ok = w && (CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    free(w);
    return ok;
}
char **tb_names(const char *dir, size_t *count)
{
    *count = 0;
    size_t n = strlen(dir);
    char *pattern = malloc(n + 3);
    if (!pattern)
        return NULL;
    sprintf(pattern, "%s/*", dir);
    wchar_t *w = wide(pattern);
    free(pattern);
    if (!w)
        return NULL;
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW(w, &data);
    free(w);
    char **list = NULL;
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    do
    {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
            continue;
        char *name = utf8(data.cFileName);
        if (!name)
            break;
        char **next = realloc(list, (*count + 1) * sizeof(*next));
        if (!next)
        {
            free(name);
            break;
        }
        list = next;
        list[(*count)++] = name;
    } while (FindNextFileW(h, &data));
    CloseHandle(h);
    return list;
}
int tb_path_equal(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n) == 0;
}
char *tb_home(void)
{
    const wchar_t *p = _wgetenv(L"USERPROFILE");
    return p ? utf8(p) : NULL;
}
#else
#include <dirent.h>
#include <unistd.h>
char *tb_canonical(const char *p)
{
    return realpath(p, NULL);
}
char *tb_absolute(const char *p)
{
    char *real = realpath(p, NULL);
    if (real)
        return real;
    char *copy = strdup(p);
    if (!copy)
        return NULL;
    char *slash = strrchr(copy, '/');
    char *result = NULL;
    if (slash && slash[1])
    {
        char *name = strdup(slash + 1);
        *slash = 0;
        char *parent = tb_absolute(*copy ? copy : "/");
        if (parent && name)
        {
            if (!strcmp(name, "."))
                result = strdup(parent);
            else if (!strcmp(name, ".."))
            {
                char *last = strrchr(parent, '/');
                if (last && last != parent)
                    *last = 0;
                else if (last)
                    last[1] = 0;
                result = strdup(parent);
            }
            else
            {
                result = malloc(strlen(parent) + strlen(name) + 2);
                if (result)
                    sprintf(result, "%s/%s", parent, name);
            }
        }
        free(name);
        free(parent);
    }
    else if (!slash && *copy)
    {
        char *cwd = getcwd(NULL, 0);
        if (cwd)
        {
            result = malloc(strlen(cwd) + strlen(copy) + 2);
            if (result)
                sprintf(result, "%s/%s", cwd, copy);
            free(cwd);
        }
    }
    free(copy);
    return result;
}
int tb_is_dir(const char *p)
{
    struct stat s;
    return !stat(p, &s) && S_ISDIR(s.st_mode);
}
int tb_is_file(const char *p)
{
    struct stat s;
    return !stat(p, &s) && S_ISREG(s.st_mode);
}
FILE *tb_fopen(const char *p, const char *m)
{
    return fopen(p, m);
}
static int mkdir_one(const char *p)
{
    return !mkdir(p, 0700) || errno == EEXIST;
}
char **tb_names(const char *dir, size_t *count)
{
    *count = 0;
    DIR *d = opendir(dir);
    if (!d)
        return NULL;
    struct dirent *entry;
    char **list = NULL;
    while ((entry = readdir(d)))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char **next = realloc(list, (*count + 1) * sizeof(*next));
        if (!next)
            break;
        list = next;
        list[*count] = strdup(entry->d_name);
        if (!list[*count])
            break;
        (*count)++;
    }
    closedir(d);
    return list;
}
int tb_path_equal(const char *a, const char *b, size_t n)
{
    return !strncmp(a, b, n);
}
char *tb_home(void)
{
    const char *p = getenv("HOME");
    return p ? strdup(p) : NULL;
}
#endif
void tb_names_free(char **names, size_t count)
{
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
}
int tb_mkdirs(const char *p)
{
    if (tb_is_dir(p))
        return 1;
    char *copy = strdup(p);
    if (!copy)
        return 0;
    char *slash = strrchr(copy, '/');
#ifdef _WIN32
    char *back = strrchr(copy, '\\');
    if (back && (!slash || back > slash))
        slash = back;
#endif
    int ok = 1;
    if (slash && slash != copy)
    {
        *slash = 0;
        ok = tb_mkdirs(copy);
    }
    free(copy);
    return ok && mkdir_one(p) && tb_is_dir(p);
}
static int write_atomic(const char *path, const void *data, size_t length, const char *source)
{
    size_t n = strlen(path);
    char *tmp = malloc(n + 16);
    if (!tmp)
        return 0;
    FILE *out = NULL;
#ifdef _WIN32
    char *parent = strdup(path);
    if (!parent)
    {
        free(tmp);
        return 0;
    }
    char *slash = strrchr(parent, '/'), *back = strrchr(parent, '\\');
    if (back && (!slash || back > slash))
        slash = back;
    if (slash)
        *slash = 0;
    wchar_t *w = wide(parent);
    wchar_t temp[MAX_PATH];
    free(parent);
    if (w && GetTempFileNameW(w, L"2in", 0, temp))
    {
        char *s = utf8(temp);
        if (s)
        {
            free(tmp);
            tmp = s;
            out = tb_fopen(tmp, "wb");
        }
    }
    free(w);
#else
    sprintf(tmp, "%s.XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd >= 0)
        out = fdopen(fd, "wb");
#endif
    int ok = out != NULL;
    if (ok && source)
    {
        FILE *in = tb_fopen(source, "rb");
        ok = in != NULL;
        unsigned char buffer[65536];
        size_t got;
        if (in)
        {
            while ((got = fread(buffer, 1, sizeof(buffer), in)))
                if (fwrite(buffer, 1, got, out) != got)
                {
                    ok = 0;
                    break;
                }
            if (ferror(in))
                ok = 0;
            fclose(in);
        }
#ifndef _WIN32
        struct stat status;
        if (!stat(source, &status) && fchmod(fileno(out), status.st_mode & 0777))
            ok = 0;
#endif
    }
    else if (ok && length && fwrite(data, 1, length, out) != length)
        ok = 0;
    if (out && fclose(out))
        ok = 0;
#ifdef _WIN32
    wchar_t *a = wide(tmp), *b = wide(path);
    if (ok)
        ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (a)
        DeleteFileW(a);
    free(a);
    free(b);
#else
    if (ok)
        ok = !rename(tmp, path);
    unlink(tmp);
#endif
    free(tmp);
    return ok;
}
int tb_atomic_write(const char *p, const void *d, size_t n)
{
    return write_atomic(p, d, n, NULL);
}
int tb_copy(const char *s, const char *d)
{
    return write_atomic(d, NULL, 0, s);
}
