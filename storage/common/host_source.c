/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host file access for image and archive sources: UTF-8 paths, advisory
   locks, durable writes and file identity. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "storage/common/host.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <wchar.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

#ifdef _WIN32
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
        errno = EINVAL;
        return NULL;
    }
    wchar_t *w = malloc((size_t)n * sizeof(*w));
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n))
    {
        errno = EINVAL;
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
#endif

FILE *tb_host_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *name = wide(path), *how = wide(mode);
    FILE *file = name && how ? _wfopen(name, how) : NULL;
    free(name);
    free(how);
    return file;
#else
    return fopen(path, mode);
#endif
}

int tb_host_lock(FILE *file, int write)
{
#ifdef _WIN32
    OVERLAPPED overlapped = {0};
    if (!LockFileEx((HANDLE)_get_osfhandle(_fileno(file)), write ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0,
                    MAXDWORD, MAXDWORD, &overlapped))
    {
        errno = EACCES;
        return 0;
    }
    return 1;
#else
    int result;
    do
        result = flock(fileno(file), write ? LOCK_EX : LOCK_SH);
    while (result && errno == EINTR);
    return result == 0;
#endif
}

int tb_host_sync(FILE *file)
{
#ifdef _WIN32
    return !_commit(_fileno(file));
#else
    return !fsync(fileno(file));
#endif
}

#ifdef _WIN32
char *tb_host_canonical(const char *path)
{
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

int tb_host_identity(const char *path, TbFileIdentity *identity)
{
    memset(identity, 0, sizeof *identity);
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
    if (ok)
    {
        identity->device = info.dwVolumeSerialNumber;
        identity->file = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
        identity->size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        identity->modified = (uint64_t)times.LastWriteTime.QuadPart;
        identity->changed = (uint64_t)times.ChangeTime.QuadPart;
    }
    CloseHandle(file);
    return ok;
}
#else
char *tb_host_canonical(const char *path)
{
    return realpath(path, NULL);
}

int tb_host_identity(const char *path, TbFileIdentity *identity)
{
    memset(identity, 0, sizeof *identity);
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
    return 1;
}
#endif
