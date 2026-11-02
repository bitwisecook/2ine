/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _XOPEN_SOURCE 700
#include "platform.h"
#include "disk_image.h"
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
static void path_errno(DWORD error)
{
    switch (error)
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            errno = ENOENT;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            errno = EACCES;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            errno = ENOMEM;
            break;
        case ERROR_DIRECTORY:
            errno = ENOTDIR;
            break;
        case ERROR_FILENAME_EXCED_RANGE:
            errno = ENAMETOOLONG;
            break;
        case ERROR_INVALID_NAME:
        case ERROR_NO_UNICODE_TRANSLATION:
            errno = EINVAL;
            break;
        default:
            errno = EIO;
            break;
    }
}
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n)
    {
        path_errno(GetLastError());
        return NULL;
    }
    wchar_t *w = malloc((size_t)n * sizeof(*w));
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n))
    {
        path_errno(GetLastError());
        free(w);
        return NULL;
    }
    return w;
}
static char *utf8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, NULL, 0, NULL, NULL);
    if (!n)
    {
        path_errno(GetLastError());
        return NULL;
    }
    char *s = malloc((size_t)n);
    if (s && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, s, n, NULL, NULL))
    {
        path_errno(GetLastError());
        free(s);
        return NULL;
    }
    return s;
}
char *tb_absolute(const char *path)
{
    if (tb_image_path(path))
        return tb_image_absolute(path);
    wchar_t *w = wide(path);
    if (!w)
        return NULL;
    DWORD n = GetFullPathNameW(w, 0, NULL, NULL);
    if (!n)
    {
        path_errno(GetLastError());
        free(w);
        return NULL;
    }
    wchar_t *full = n ? malloc((size_t)n * sizeof(*full)) : NULL;
    char *result = NULL;
    if (full)
    {
        DWORD length = GetFullPathNameW(w, n, full, NULL);
        if (length && length < n)
            result = utf8(full);
        else if (!length)
            path_errno(GetLastError());
        else
            errno = ENAMETOOLONG;
    }
    free(full);
    free(w);
    return result;
}
char *tb_canonical(const char *path)
{
    if (tb_image_path(path))
        return tb_image_canonical(path);
    wchar_t *w = wide(path);
    if (!w)
        return NULL;
    HANDLE f = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (f == INVALID_HANDLE_VALUE)
    {
        path_errno(GetLastError());
        free(w);
        return NULL;
    }
    free(w);
    DWORD n = GetFinalPathNameByHandleW(f, NULL, 0, FILE_NAME_NORMALIZED);
    if (!n)
    {
        path_errno(GetLastError());
        int error = errno;
        CloseHandle(f);
        errno = error;
        return NULL;
    }
    wchar_t *full = n ? malloc(((size_t)n + 1) * sizeof(*full)) : NULL;
    char *result = NULL;
    DWORD length = full ? GetFinalPathNameByHandleW(f, full, n + 1, FILE_NAME_NORMALIZED) : 0;
    if (full && !length)
        path_errno(GetLastError());
    else if (length > n)
        errno = ENAMETOOLONG;
    if (length && length <= n)
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
    int error = errno;
    free(full);
    CloseHandle(f);
    errno = error;
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
    if (tb_image_path(p))
    {
        TbImageInfo info;
        return tb_image_info(p, &info) && info.directory == 1;
    }
    DWORD a = attributes(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
int tb_is_file(const char *p)
{
    if (tb_image_path(p))
    {
        TbImageInfo info;
        return tb_image_info(p, &info) && info.directory == 0;
    }
    DWORD a = attributes(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
FILE *tb_fopen(const char *p, const char *mode)
{
    if (tb_image_path(p))
    {
        if (mode[0] != 'r' || strchr(mode, '+'))
        {
            errno = EROFS;
            return NULL;
        }
        return tb_image_fopen(p);
    }
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
    if (tb_image_path(dir))
        return tb_image_names(dir, count);
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
    if (!p)
    {
        errno = ENOENT;
        return NULL;
    }
    return utf8(p);
}
#else
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
char *tb_canonical(const char *p)
{
    if (tb_image_path(p))
        return tb_image_canonical(p);
    return realpath(p, NULL);
}
char *tb_absolute(const char *p)
{
    if (tb_image_path(p))
        return tb_image_absolute(p);
    char *real = realpath(p, NULL);
    if (real)
        return real;
    if (errno != ENOENT)
        return NULL;
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
    if (tb_image_path(p))
    {
        TbImageInfo info;
        return tb_image_info(p, &info) && info.directory == 1;
    }
    struct stat s;
    return !stat(p, &s) && S_ISDIR(s.st_mode);
}
int tb_is_file(const char *p)
{
    if (tb_image_path(p))
    {
        TbImageInfo info;
        return tb_image_info(p, &info) && info.directory == 0;
    }
    struct stat s;
    return !stat(p, &s) && S_ISREG(s.st_mode);
}
FILE *tb_fopen(const char *p, const char *m)
{
    if (tb_image_path(p))
    {
        if (m[0] != 'r' || strchr(m, '+'))
        {
            errno = EROFS;
            return NULL;
        }
        return tb_image_fopen(p);
    }
    return fopen(p, m);
}
static int mkdir_one(const char *p)
{
    return !mkdir(p, 0700) || errno == EEXIST;
}
char **tb_names(const char *dir, size_t *count)
{
    if (tb_image_path(dir))
        return tb_image_names(dir, count);
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
    if (!p)
    {
        errno = ENOENT;
        return NULL;
    }
    return strdup(p);
}
#endif
void tb_names_free(char **names, size_t count)
{
    if (!names)
        return;
    for (size_t i = 0; i < count; i++)
        free(names[i]);
    free(names);
}
int tb_mkdirs(const char *p)
{
    if (tb_image_path(p))
    {
        errno = EROFS;
        return 0;
    }
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
    if (tb_image_path(path))
    {
        if (source)
        {
            FILE *in = tb_fopen(source, "rb");
            if (!in)
                return 0;
            int ok = tb_image_copy(in, path, 0);
            fclose(in);
            return ok;
        }
        return tb_image_create_file(path, 0) && tb_image_resize(path, NULL, 0) &&
               tb_image_write(path, NULL, 0, data, length);
    }
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

int tb_file_info(FILE *file, int64_t *size, int *mode)
{
#ifdef _WIN32
    struct _stat64 info;
    if (_fstat64(_fileno(file), &info) || !(info.st_mode & _S_IFREG))
        return 0;
#else
    struct stat info;
    if (fstat(fileno(file), &info) || !S_ISREG(info.st_mode))
        return 0;
#endif
    *size = info.st_size;
    *mode = info.st_mode & 0777;
    return 1;
}

char *tb_archive_workspace(const char *parent)
{
#ifdef _WIN32
    wchar_t *w = wide(parent), temporary[MAX_PATH];
    char *result = NULL;
    if (w && GetTempFileNameW(w, L"2in", 0, temporary))
    {
        if (DeleteFileW(temporary) && CreateDirectoryW(temporary, NULL))
            result = utf8(temporary);
    }
    free(w);
    return result;
#else
    size_t length = strlen(parent) + 24;
    char *result = malloc(length);
    if (!result)
        return NULL;
    snprintf(result, length, "%s/.2ine-export-XXXXXX", parent);
    if (!mkdtemp(result))
    {
        free(result);
        return NULL;
    }
    return result;
#endif
}

int tb_replace(const char *source, const char *destination)
{
#ifdef _WIN32
    wchar_t *a = wide(source), *b = wide(destination);
    int ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(a);
    free(b);
    return ok;
#else
    return !rename(source, destination);
#endif
}

int tb_file_descriptor(FILE *file)
{
#ifdef _WIN32
    return _fileno(file);
#else
    return fileno(file);
#endif
}

int tb_file_mode(FILE *file, int mode)
{
#ifdef _WIN32
    (void)file;
    (void)mode;
    return 1;
#else
    return !fchmod(fileno(file), mode & 0777);
#endif
}

/* Used only for a newly reserved archive workspace or failed import. */
int tb_remove_tree(const char *path)
{
    int directory = !tb_is_link(path) && tb_is_dir(path);
    if (directory)
    {
        size_t count = 0;
        char **names = tb_names(path, &count);
        if (count == (size_t)-1)
            return 0;
        int ok = 1;
        for (size_t i = 0; i < count; i++)
        {
            size_t length = strlen(path) + strlen(names[i]) + 2;
            char *child = malloc(length);
            if (!child)
            {
                ok = 0;
                break;
            }
            snprintf(child, length, "%s/%s", path, names[i]);
            if (!tb_remove_tree(child))
                ok = 0;
            free(child);
        }
        tb_names_free(names, count);
        if (!ok)
            return 0;
    }
#ifdef _WIN32
    wchar_t *w = wide(path);
    int ok = w && (directory ? RemoveDirectoryW(w) : DeleteFileW(w));
    free(w);
    return ok;
#else
    return !(directory ? rmdir(path) : unlink(path));
#endif
}

/* Import operations never replace existing items, including dangling links. */
int tb_is_link(const char *path)
{
    if (tb_image_path(path))
        return 0;
#ifdef _WIN32
    DWORD a = attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat s;
    return !lstat(path, &s) && S_ISLNK(s.st_mode);
#endif
}
int tb_mkdir_new(const char *path)
{
    if (tb_image_path(path))
        return tb_image_mkdir(path);
#ifdef _WIN32
    wchar_t *w = wide(path);
    int ok = w && CreateDirectoryW(w, NULL);
    free(w);
    return ok;
#else
    return !mkdir(path, 0700);
#endif
}
int tb_copy_new(const char *source, const char *destination)
{
    if (tb_image_path(destination))
    {
        FILE *in = tb_fopen(source, "rb");
        if (!in)
            return 0;
        int ok = tb_image_copy(in, destination, 1);
        fclose(in);
        return ok;
    }
#ifdef _WIN32
    wchar_t *a = wide(source), *b = wide(destination);
    int ok = a && b && CopyFileW(a, b, TRUE);
    free(a);
    free(b);
    return ok;
#else
    FILE *in = fopen(source, "rb");
    if (!in)
        return 0;
    int fd = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0600);
    FILE *out = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (!out)
    {
        if (fd >= 0)
            close(fd);
        fclose(in);
        return 0;
    }
    int ok = 1;
    unsigned char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)))
        if (fwrite(buffer, 1, n, out) != n)
        {
            ok = 0;
            break;
        }
    if (ferror(in))
        ok = 0;
    struct stat info;
    if (!fstat(fileno(in), &info) && fchmod(fd, info.st_mode & 0777))
        ok = 0;
    fclose(in);
    if (fclose(out))
        ok = 0;
    if (!ok)
        unlink(destination);
    return ok;
#endif
}

int tb_directory_readable(const char *path)
{
    if (tb_image_path(path))
    {
        size_t count = 0;
        char **names = tb_image_names(path, &count);
        if (count == (size_t)-1)
            return 0;
        tb_names_free(names, count);
        return 1;
    }
#ifdef _WIN32
    wchar_t *w = wide(path);
    if (!w)
        return 0;
    HANDLE handle =
        CreateFileW(w, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        path_errno(GetLastError());
        free(w);
        return 0;
    }
    free(w);
    CloseHandle(handle);
    return 1;
#else
    DIR *directory = opendir(path);
    if (!directory)
        return 0;
    closedir(directory);
    return 1;
#endif
}
