/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _XOPEN_SOURCE 700
#include "platform.h"
#include "disk_image.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
static _Thread_local CFCalendarRef file_time_calendar;
#endif
/* mktime on macOS may reload zoneinfo for every DOS archive timestamp. A
   request uses one local-zone interpretation; no process-wide TZ is changed. */
static _Thread_local struct
{
    uint64_t key;
    int64_t value;
    int valid;
} file_time_cache[4096];
void tb_file_time_cache_reset(void)
{
    memset(file_time_cache, 0, sizeof file_time_cache);
#ifdef __APPLE__
    if (file_time_calendar)
        CFRelease(file_time_calendar);
    file_time_calendar = NULL;
#endif
}
int64_t tb_local_file_time(unsigned date, unsigned clock, struct tm *parts)
{
    uint64_t key = ((uint64_t)date << 32) | clock;
    size_t slot =
        (size_t)((key ^ (key >> 17)) * UINT64_C(11400714819323198485)) >> (sizeof(size_t) * 8 - 12);
    if (!file_time_cache[slot].valid || file_time_cache[slot].key != key)
    {
        file_time_cache[slot].key = key;
#ifdef __APPLE__
        if (!file_time_calendar)
        {
            const char *environment = getenv("TZ");
            CFTimeZoneRef zone = NULL;
            if (environment && *environment)
            {
                CFStringRef name =
                    CFStringCreateWithCString(NULL, environment, kCFStringEncodingUTF8);
                if (name)
                {
                    zone = CFTimeZoneCreateWithName(NULL, name, true);
                    CFRelease(name);
                }
                if (!zone && (!strcmp(environment, "UTC0") || !strcmp(environment, "GMT0")))
                    zone = CFTimeZoneCreateWithTimeIntervalFromGMT(NULL, 0);
            }
            else
                zone = CFTimeZoneCopySystem();
            if (zone)
            {
                file_time_calendar = CFCalendarCreateWithIdentifier(NULL, kCFGregorianCalendar);
                if (file_time_calendar)
                    CFCalendarSetTimeZone(file_time_calendar, zone);
                CFRelease(zone);
            }
        }
        CFAbsoluteTime absolute;
        if (file_time_calendar &&
            CFCalendarComposeAbsoluteTime(file_time_calendar, &absolute, "yMdHms",
                                          parts->tm_year + 1900, parts->tm_mon + 1, parts->tm_mday,
                                          parts->tm_hour, parts->tm_min, parts->tm_sec))
            file_time_cache[slot].value = (int64_t)(absolute + kCFAbsoluteTimeIntervalSince1970);
        else
#endif
            file_time_cache[slot].value = (int64_t)mktime(parts);
        file_time_cache[slot].valid = 1;
    }
    return file_time_cache[slot].value;
}
#ifdef __linux__
#include <fcntl.h>
#include <sys/syscall.h>
#endif
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
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            errno = EEXIST;
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

int tb_move_new(const char *source, const char *destination)
{
#ifdef _WIN32
    wchar_t *a = wide(source), *b = wide(destination);
    int ok = a && b && MoveFileExW(a, b, MOVEFILE_WRITE_THROUGH);
    if (!ok && a && b)
        path_errno(GetLastError());
    free(a);
    free(b);
    return ok;
#elif defined(__APPLE__)
    return !renamex_np(source, destination, RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
    return !syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination,
                    1 /* RENAME_NOREPLACE */);
#else
    (void)source;
    (void)destination;
    errno = ENOTSUP;
    return 0;
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

int tb_path_info(const char *path, uint64_t *size, int64_t *modified)
{
    *modified = INT64_MIN;
    if (tb_image_path(path))
    {
        TbImageInfo info;
        if (!tb_image_info(path, &info))
            return 0;
        *size = info.size;
        *modified = info.modified;
        return 1;
    }
#ifdef _WIN32
    wchar_t *name = wide(path);
    if (!name)
        return 0;
    WIN32_FILE_ATTRIBUTE_DATA data;
    int ok = GetFileAttributesExW(name, GetFileExInfoStandard, &data);
    free(name);
    if (!ok)
    {
        path_errno(GetLastError());
        return 0;
    }
    *size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    uint64_t ticks =
        ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    if (ticks)
        *modified = (int64_t)(ticks / 10000000) - INT64_C(11644473600);
#else
    struct stat info;
    if (stat(path, &info))
        return 0;
    *size = info.st_size < 0 ? 0 : (uint64_t)info.st_size;
    *modified = (int64_t)info.st_mtime;
#endif
    return 1;
}

int tb_path_identity(const char *path, TbFileIdentity *identity)
{
    memset(identity, 0, sizeof *identity);
    if (tb_image_path(path))
        return 0; /* Virtual/mutable volumes are never retained as host media. */
#ifdef _WIN32
    wchar_t *name = wide(path);
    if (!name)
        return 0;
    HANDLE file = CreateFileW(name, FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(name);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION info;
    FILE_BASIC_INFO times;
    int ok = GetFileInformationByHandle(file, &info) &&
             GetFileInformationByHandleEx(file, FileBasicInfo, &times, sizeof times) &&
             !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    CloseHandle(file);
    if (!ok)
        return 0;
    identity->device = info.dwVolumeSerialNumber;
    identity->file = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    identity->size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    identity->modified = (uint64_t)times.LastWriteTime.QuadPart;
    identity->changed = (uint64_t)times.ChangeTime.QuadPart;
#else
    struct stat info;
    if (stat(path, &info) || !S_ISREG(info.st_mode))
        return 0;
    identity->device = (uint64_t)info.st_dev;
    identity->file = (uint64_t)info.st_ino;
    identity->size = (uint64_t)info.st_size;
    identity->modified = (uint64_t)info.st_mtime;
    identity->changed = (uint64_t)info.st_ctime;
#ifdef __APPLE__
    identity->modified_fraction = (uint64_t)info.st_mtimespec.tv_nsec;
    identity->changed_fraction = (uint64_t)info.st_ctimespec.tv_nsec;
#else
    identity->modified_fraction = (uint64_t)info.st_mtim.tv_nsec;
    identity->changed_fraction = (uint64_t)info.st_ctim.tv_nsec;
#endif
#endif
    return 1;
}

/* Host runtime selection probes headers only; guest executables are not run. */
static uint32_t runtime_word(const unsigned char *p, int big)
{
    return big ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
               : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static int runtime_read(FILE *file, uint64_t size, uint64_t at, void *buffer, size_t length)
{
    if (at > size || length > size - at || at > INT64_MAX)
        return 0;
#ifdef _WIN32
    if (_fseeki64(file, (__int64)at, SEEK_SET))
#else
    if (fseeko(file, (off_t)at, SEEK_SET))
#endif
        return 0;
    return fread(buffer, 1, length, file) == length;
}
static int runtime_cpu(uint32_t cpu, int format)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return cpu == (format == 1 ? 183u : format == 2 ? 0x0100000cu : 0xaa64u);
#elif defined(__x86_64__) || defined(_M_X64)
    if (format == 1)
        return cpu == 62 || cpu == 3; /* Native i386 workers also run on Linux x86-64. */
    return cpu == (format == 2 ? 0x01000007u : 0x8664u) || (format == 3 && cpu == 0x14c);
#elif defined(__i386__) || defined(_M_IX86)
    return cpu == (format == 1 ? 3u : format == 2 ? 7u : 0x14cu);
#else
    (void)cpu;
    (void)format;
    return 0;
#endif
}
int tb_host_native_execution(void)
{
#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
    return 1;
#else
    return 0;
#endif
}
int tb_host_uses_dyld(void)
{
#ifdef __APPLE__
    return 1;
#else
    return 0;
#endif
}
int tb_host_executable(const char *path)
{
    if (!path || tb_image_path(path))
        return 0;
#ifndef _WIN32
    if (access(path, X_OK))
        return 0;
#endif
    FILE *file = tb_fopen(path, "rb");
    if (!file)
        return 0;
    int64_t signed_size;
    int mode, ok = 0;
    unsigned char header[64];
    if (!tb_file_info(file, &signed_size, &mode) || signed_size < 64)
        goto done;
    uint64_t size = (uint64_t)signed_size;
    if (!runtime_read(file, size, 0, header, sizeof header))
        goto done;
#ifdef _WIN32
    if (header[0] == 'M' && header[1] == 'Z')
    {
        unsigned char pe[24];
        uint32_t at = runtime_word(header + 60, 0);
        if (runtime_read(file, size, at, pe, sizeof pe) && !memcmp(pe, "PE\0\0", 4))
        {
            unsigned machine = pe[4] | pe[5] << 8, flags = pe[22] | pe[23] << 8;
            ok = runtime_cpu(machine, 3) && (flags & 2) && !(flags & 0x2000);
        }
    }
#elif defined(__APPLE__)
    uint32_t magic = runtime_word(header, 1);
    if (magic == 0xcffaedfe || magic == 0xcefaedfe)
        ok = runtime_cpu(runtime_word(header + 4, 0), 2) && runtime_word(header + 12, 0) == 2 &&
             ((magic == 0xcffaedfe) == !!(runtime_word(header + 4, 0) & 0x01000000));
    else if (magic == 0xcafebabe || magic == 0xbebafeca || magic == 0xcafebabf ||
             magic == 0xbfbafeca)
    {
        int big = magic == 0xcafebabe || magic == 0xcafebabf;
        int wide = magic == 0xcafebabf || magic == 0xbfbafeca;
        uint32_t count = runtime_word(header + 4, big);
        for (uint32_t i = 0; !ok && count <= 64 && i < count; i++)
        {
            unsigned char arch[32], image[16];
            if (!runtime_read(file, size, 8 + (uint64_t)i * (wide ? 32 : 20), arch, wide ? 32 : 20))
                break;
            uint64_t offset = runtime_word(arch + 8, big);
            if (wide)
                offset = big ? offset << 32 | runtime_word(arch + 12, big)
                             : (uint64_t)runtime_word(arch + 12, big) << 32 | offset;
            uint64_t slice_size = runtime_word(arch + (wide ? 16 : 12), big);
            if (wide)
                slice_size = big ? slice_size << 32 | runtime_word(arch + 20, big)
                                 : (uint64_t)runtime_word(arch + 20, big) << 32 | slice_size;
            if (runtime_cpu(runtime_word(arch, big), 2) && slice_size >= sizeof image &&
                offset <= size && slice_size <= size - offset &&
                runtime_read(file, size, offset, image, sizeof image))
                ok = (runtime_word(image, 1) == 0xcffaedfe ||
                      runtime_word(image, 1) == 0xcefaedfe) &&
                     runtime_cpu(runtime_word(image + 4, 0), 2) &&
                     runtime_word(image + 12, 0) == 2 &&
                     ((runtime_word(image, 1) == 0xcffaedfe) ==
                      !!(runtime_word(image + 4, 0) & 0x01000000));
        }
    }
#else
    if (!memcmp(header, "\177ELF", 4) && header[5] == 1 && header[6] == 1)
    {
        uint32_t identity = runtime_word(header + 16, 0);
        unsigned type = identity & 0xffff, cpu = identity >> 16;
        ok = header[4] == (cpu == 62 || cpu == 183 ? 2 : 1) && (type == 2 || type == 3) &&
             (header[7] == 0 || header[7] == 3) && runtime_cpu(cpu, 1);
    }
#endif
done:
    fclose(file);
    return ok;
}
