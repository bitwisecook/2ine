/* Public domain. No rights reserved. Wholly AI-authored. */
/* POSIX filesystem adapter for Linux and macOS. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "host_fs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
struct LxHostFile { int fd; };
struct LxHostDirectory { DIR *object; };
static LxHostFsError fs_error(int error)
{
    switch (error) {
        case 0: return LX_HOST_FS_OK;
        case ENOENT: return LX_HOST_FS_MISSING;
        case ENOTDIR: return LX_HOST_FS_NOT_DIRECTORY;
        case ENOTEMPTY: return LX_HOST_FS_NOT_EMPTY;
        case EEXIST: return LX_HOST_FS_EXISTS;
        case EACCES: case EPERM: case ETXTBSY: return LX_HOST_FS_DENIED;
        case ENOMEM: return LX_HOST_FS_NO_MEMORY;
        case ENFILE: case EMFILE: return LX_HOST_FS_TOO_MANY_FILES;
        case ENOSPC: case EDQUOT: return LX_HOST_FS_FULL;
        case EISDIR: return LX_HOST_FS_IS_DIRECTORY;
        case ENAMETOOLONG: return LX_HOST_FS_NAME_LONG;
        case EBUSY: return LX_HOST_FS_BUSY;
        case EROFS: return LX_HOST_FS_READ_ONLY;
        case EXDEV: return LX_HOST_FS_CROSS_DEVICE;
        case EINVAL: case EOVERFLOW: return LX_HOST_FS_INVALID;
        default: return LX_HOST_FS_IO;
    }
}
static LxHostFile *adopt(int fd, LxHostFsError *error)
{
    if (fd < 0) { if (error) *error = fs_error(errno); return NULL; }
    LxHostFile *file = malloc(sizeof *file);
    if (!file) { close(fd); if (error) *error = LX_HOST_FS_NO_MEMORY; return NULL; }
    file->fd = fd; if (error) *error = LX_HOST_FS_OK; return file;
}
LxHostFile *lx_host_file_open(const char *path, unsigned flags, int readonly, LxHostFsError *error)
{
    unsigned access = flags & (LX_HOST_FILE_READ | LX_HOST_FILE_WRITE);
    if (!access || flags & ~127u) { if (error) *error = LX_HOST_FS_INVALID; return NULL; }
    int native = access == (LX_HOST_FILE_READ | LX_HOST_FILE_WRITE) ? O_RDWR :
        access == LX_HOST_FILE_WRITE ? O_WRONLY : O_RDONLY;
    if (flags & LX_HOST_FILE_CREATE) native |= O_CREAT;
    if (flags & LX_HOST_FILE_EXCLUSIVE) native |= O_EXCL;
    if (flags & LX_HOST_FILE_TRUNCATE) native |= O_TRUNC;
    if (flags & LX_HOST_FILE_SYNC) native |= O_SYNC;
    if (flags & LX_HOST_FILE_NOINHERIT) native |= O_CLOEXEC;
    int fd;
    do { fd = open(path, native, readonly ? S_IRUSR : S_IRUSR | S_IWUSR); } while (fd < 0 && errno == EINTR);
    return adopt(fd, error);
}
LxHostFile *lx_host_file_standard(unsigned stream)
{
    return stream <= 2 ? adopt(dup((int) stream), NULL) : NULL;
}
LxHostFile *lx_host_file_duplicate(LxHostFile *file, LxHostFsError *error) { return adopt(dup(file->fd), error); }
void lx_host_file_close(LxHostFile *file) { if (file) { close(file->fd); free(file); } }
LxHostFsError lx_host_file_read(LxHostFile *file, void *data, size_t size, size_t *actual)
{
    ssize_t result;
    do { result = read(file->fd, data, size); } while (result < 0 && errno == EINTR);
    *actual = result < 0 ? 0 : (size_t) result; return result < 0 ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_file_write(LxHostFile *file, const void *data, size_t size, size_t *actual)
{
    ssize_t result;
    do { result = write(file->fd, data, size); } while (result < 0 && errno == EINTR);
    *actual = result < 0 ? 0 : (size_t) result; return result < 0 ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_file_seek(LxHostFile *file, int64_t distance, unsigned origin, uint64_t *position)
{
    if (origin > 2 || (int64_t) (off_t) distance != distance) return LX_HOST_FS_INVALID;
    const int origins[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    off_t result = lseek(file->fd, (off_t) distance, origins[origin]);
    if (result < 0) return fs_error(errno);
    if (position) *position = (uint64_t) result;
    return LX_HOST_FS_OK;
}
static void describe(const struct stat *native, LxHostFileInfo *info)
{
    info->size = native->st_size < 0 ? 0 : (uint64_t) native->st_size;
    info->allocated = native->st_blocks < 0 ? 0 : (uint64_t) native->st_blocks * 512;
    info->accessed = native->st_atime; info->modified = native->st_mtime;
    info->readonly = !(native->st_mode & S_IWUSR);
    info->kind = S_ISREG(native->st_mode) ? LX_HOST_FILE_REGULAR : S_ISDIR(native->st_mode) ?
        LX_HOST_FILE_DIRECTORY : S_ISCHR(native->st_mode) || S_ISBLK(native->st_mode) ? LX_HOST_FILE_DEVICE : LX_HOST_FILE_PIPE;
}
LxHostFsError lx_host_file_info(LxHostFile *file, LxHostFileInfo *info)
{
    struct stat native;
    if (fstat(file->fd, &native)) return fs_error(errno);
    describe(&native, info); return LX_HOST_FS_OK;
}
LxHostFsError lx_host_file_resize(LxHostFile *file, uint64_t size)
{
    if (size > INT64_MAX || (uint64_t) (off_t) size != size) return LX_HOST_FS_INVALID;
    return ftruncate(file->fd, (off_t) size) ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_file_flush(LxHostFile *file) { return fsync(file->fd) ? fs_error(errno) : LX_HOST_FS_OK; }
LxHostFsError lx_host_file_readonly(LxHostFile *file, int readonly)
{
    struct stat info;
    if (fstat(file->fd, &info)) return fs_error(errno);
    mode_t mode = info.st_mode & 07777;
    if (readonly) mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH); else mode |= S_IWUSR;
    return fchmod(file->fd, mode) ? fs_error(errno) : LX_HOST_FS_OK;
}
int lx_host_path_exists(const char *path) { return access(path, F_OK) == 0; }
LxHostFsError lx_host_path_readonly(const char *path, int readonly)
{
    struct stat info;
    if (stat(path, &info)) return fs_error(errno);
    mode_t mode = info.st_mode & 07777;
    if (readonly) mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH); else mode |= S_IWUSR;
    return chmod(path, mode) ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_info(const char *path, LxHostFileInfo *info)
{
    struct stat native;
    if (stat(path, &native)) return fs_error(errno);
    describe(&native, info); return LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_remove(const char *path, int directory)
{
    int result = directory ? rmdir(path) : unlink(path); return result ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_move(const char *source, const char *target) { return rename(source, target) ? fs_error(errno) : LX_HOST_FS_OK; }
LxHostFsError lx_host_path_mkdir(const char *path) { return mkdir(path, 0755) ? fs_error(errno) : LX_HOST_FS_OK; }
LxHostFsError lx_host_path_chdir(const char *path) { return chdir(path) ? fs_error(errno) : LX_HOST_FS_OK; }
char *lx_host_path_current(void) { return getcwd(NULL, 0); }
char *lx_host_path_canonical(const char *path) { return realpath(path, NULL); }
LxHostDirectory *lx_host_directory_open(const char *path, LxHostFsError *error)
{
    DIR *object = opendir(path);
    if (!object) { if (error) *error = fs_error(errno); return NULL; }
    LxHostDirectory *directory = malloc(sizeof *directory);
    if (!directory) { closedir(object); if (error) *error = LX_HOST_FS_NO_MEMORY; return NULL; }
    directory->object = object; if (error) *error = LX_HOST_FS_OK; return directory;
}
int lx_host_directory_read(LxHostDirectory *directory, const char **name, LxHostFileInfo *info, LxHostFsError *error)
{
    for (;;) {
        errno = 0; struct dirent *entry = readdir(directory->object);
        if (!entry) { int saved = errno; if (error) *error = fs_error(saved); return saved ? -1 : 0; }
        if (info) {
            struct stat native;
            if (fstatat(dirfd(directory->object), entry->d_name, &native, 0)) continue;
            describe(&native, info);
        }
        *name = entry->d_name; if (error) *error = LX_HOST_FS_OK; return 1;
    }
}
uint64_t lx_host_directory_position(LxHostDirectory *directory) { return (uint64_t) telldir(directory->object); }
void lx_host_directory_seek(LxHostDirectory *directory, uint64_t position)
{
    if (position <= LONG_MAX) seekdir(directory->object, (long) position);
}
void lx_host_directory_close(LxHostDirectory *directory) { if (directory) { closedir(directory->object); free(directory); } }
void *lx_host_file_snapshot(const char *path, size_t maximum, size_t *size, LxHostFsError *error)
{
    *size = 0;
    LxHostFsError result;
    LxHostFile *file = lx_host_file_open(path, LX_HOST_FILE_READ, 0, &result);
    if (!file) { if (error) *error = result; return NULL; }
    LxHostFileInfo info;
    result = lx_host_file_info(file, &info);
    if (!result && (info.kind != LX_HOST_FILE_REGULAR || info.size > maximum || info.size >= SIZE_MAX)) result = LX_HOST_FS_INVALID;
    void *data = NULL;
    if (!result) {
        data = malloc((size_t) info.size + 1);
        if (!data) result = LX_HOST_FS_NO_MEMORY;
    }
    size_t used = 0;
    while (!result && used < info.size) {
        size_t actual;
        result = lx_host_file_read(file, (uint8_t *) data + used, (size_t) info.size - used, &actual);
        if (!result && !actual) result = LX_HOST_FS_IO;
        used += actual;
    }
    lx_host_file_close(file);
    if (result) { free(data); data = NULL; }
    else { ((uint8_t *) data)[used] = 0; *size = used; }
    if (error) *error = result;
    return data;
}
