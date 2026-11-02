/* Public domain. No rights reserved. Wholly AI-authored. */
/* POSIX filesystem adapter for Linux and macOS. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include "hal/posix/fs_internal.h"
#include "ball/disk_image.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
struct LxImageLock
{
    pthread_mutex_t mutex;
    atomic_uint references;
};
static void image_lock(LxHostFile *file)
{
    if (file->image_lock)
        pthread_mutex_lock(&file->image_lock->mutex);
}
static LxHostFsError image_done(LxHostFile *file, LxHostFsError result)
{
    if (file->image_lock)
        pthread_mutex_unlock(&file->image_lock->mutex);
    return result;
}
struct LxHostDirectory
{
    DIR *object;
    char **names;
    char *path;
    size_t count, position;
    int image;
};
LxHostFsError lx_host_file_lock(LxHostFile *file, int acquire)
{
    struct flock lock = {0};
    lock.l_type = acquire ? F_WRLCK : F_UNLCK;
    lock.l_whence = SEEK_SET;
    int result;
    do
        result = fcntl(file->fd, F_SETLKW, &lock);
    while (result < 0 && errno == EINTR);
    return result == 0 ? LX_HOST_FS_OK : LX_HOST_FS_IO;
}
static LxHostFsError fs_error(int error)
{
    switch (error)
    {
        case 0:
            return LX_HOST_FS_OK;
        case ENXIO:
            return LX_HOST_FS_NOT_READY;
        case ENOENT:
            return LX_HOST_FS_MISSING;
        case ENOTDIR:
            return LX_HOST_FS_NOT_DIRECTORY;
        case ENOTEMPTY:
            return LX_HOST_FS_NOT_EMPTY;
        case EEXIST:
            return LX_HOST_FS_EXISTS;
        case EACCES:
        case EPERM:
        case ETXTBSY:
            return LX_HOST_FS_DENIED;
        case ENOMEM:
            return LX_HOST_FS_NO_MEMORY;
        case ENFILE:
        case EMFILE:
            return LX_HOST_FS_TOO_MANY_FILES;
        case ENOSPC:
        case EDQUOT:
            return LX_HOST_FS_FULL;
        case EISDIR:
            return LX_HOST_FS_IS_DIRECTORY;
        case ENAMETOOLONG:
            return LX_HOST_FS_NAME_LONG;
        case EAGAIN:
        case EBUSY:
            return LX_HOST_FS_BUSY;
        case EROFS:
            return LX_HOST_FS_READ_ONLY;
        case EXDEV:
            return LX_HOST_FS_CROSS_DEVICE;
        case EINVAL:
        case EOVERFLOW:
            return LX_HOST_FS_INVALID;
        default:
            return LX_HOST_FS_IO;
    }
}
static LxHostFile *adopt(int fd, LxHostFsError *error)
{
    if (fd < 0)
    {
        if (error)
            *error = fs_error(errno);
        return NULL;
    }
    LxHostFile *file = calloc(1, sizeof *file);
    if (!file)
    {
        close(fd);
        if (error)
            *error = LX_HOST_FS_NO_MEMORY;
        return NULL;
    }
    file->fd = fd;
    file->image = 0;
    if (error)
        *error = LX_HOST_FS_OK;
    return file;
}
LxHostFile *lx_host_file_open(const char *path, unsigned flags, int readonly, LxHostFsError *error)
{
    unsigned access = flags & (LX_HOST_FILE_READ | LX_HOST_FILE_WRITE);
    if (!access || flags & ~127u)
    {
        if (error)
            *error = LX_HOST_FS_INVALID;
        return NULL;
    }
    if (tb_image_path(path))
    {
        int modifying = flags & (LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE | LX_HOST_FILE_TRUNCATE);
        if (flags & LX_HOST_FILE_CREATE)
        {
            if (!tb_image_create_file(path, !!(flags & LX_HOST_FILE_EXCLUSIVE)))
            {
                if (error)
                    *error = fs_error(errno);
                return NULL;
            }
        }
        TbImageInfo image_info;
        if (!tb_image_info(path, &image_info))
        {
            if (error)
                *error = fs_error(errno);
            return NULL;
        }
        if (modifying && image_info.readonly)
        {
            if (error)
                *error = LX_HOST_FS_READ_ONLY;
            return NULL;
        }
        if ((flags & LX_HOST_FILE_TRUNCATE) && !tb_image_resize(path, NULL, 0))
        {
            if (error)
                *error = fs_error(errno);
            return NULL;
        }
        FILE *snapshot = tb_image_fopen(path);
        if (!snapshot)
        {
            if (error)
                *error = fs_error(errno);
            return NULL;
        }
        int fd = dup(fileno(snapshot));
        fclose(snapshot);
        LxHostFile *file = adopt(fd, error);
        if (file)
        {
            file->image = 1;
            file->image_writable = !!(flags & LX_HOST_FILE_WRITE);
            file->image_readonly = image_info.readonly;
            file->image_lock = malloc(sizeof *file->image_lock);
            if (!file->image_lock)
            {
                lx_host_file_close(file);
                if (error)
                    *error = LX_HOST_FS_NO_MEMORY;
                return NULL;
            }
            int lock_error = pthread_mutex_init(&file->image_lock->mutex, NULL);
            if (lock_error)
            {
                free(file->image_lock);
                file->image_lock = NULL;
                lx_host_file_close(file);
                if (error)
                    *error = fs_error(lock_error);
                return NULL;
            }
            atomic_init(&file->image_lock->references, 1);
            file->image_path = strdup(path);
            file->image_source = tb_image_source(path);
            if (!file->image_path || !file->image_source)
            {
                lx_host_file_close(file);
                if (error)
                    *error = LX_HOST_FS_NO_MEMORY;
                return NULL;
            }
            if (flags & LX_HOST_FILE_NOINHERIT)
            {
                LxHostFsError result = lx_host_file_inherit(file, 0);
                if (result)
                {
                    lx_host_file_close(file);
                    if (error)
                        *error = result;
                    return NULL;
                }
            }
        }
        return file;
    }
    int native = access == (LX_HOST_FILE_READ | LX_HOST_FILE_WRITE) ? O_RDWR
                 : access == LX_HOST_FILE_WRITE                     ? O_WRONLY
                                                                    : O_RDONLY;
    if (flags & LX_HOST_FILE_CREATE)
        native |= O_CREAT;
    if (flags & LX_HOST_FILE_EXCLUSIVE)
        native |= O_EXCL;
    if (flags & LX_HOST_FILE_TRUNCATE)
        native |= O_TRUNC;
    if (flags & LX_HOST_FILE_SYNC)
        native |= O_SYNC;
    if (flags & LX_HOST_FILE_NOINHERIT)
        native |= O_CLOEXEC;
    int fd;
    do
    {
        fd = open(path, native, readonly ? S_IRUSR : S_IRUSR | S_IWUSR);
    } while (fd < 0 && errno == EINTR);
    return adopt(fd, error);
}
LxHostFile *lx_host_file_standard(unsigned stream)
{
    return stream <= 2 ? adopt(dup((int)stream), NULL) : NULL;
}
LxHostFsError lx_host_file_inherit(LxHostFile *file, int inherit)
{
    int flags = fcntl(file->fd, F_GETFD);
    if (flags < 0 ||
        fcntl(file->fd, F_SETFD, inherit ? flags & ~FD_CLOEXEC : flags | FD_CLOEXEC) < 0)
        return fs_error(errno);
    return LX_HOST_FS_OK;
}
LxHostFile *lx_host_file_duplicate(LxHostFile *file, LxHostFsError *error)
{
    LxHostFile *copy = adopt(dup(file->fd), error);
    if (copy && file->image)
    {
        copy->image = 1;
        copy->image_writable = file->image_writable;
        copy->image_readonly = file->image_readonly;
        copy->image_lock = file->image_lock;
        atomic_fetch_add(&copy->image_lock->references, 1);
        copy->image_path = strdup(file->image_path);
        copy->image_source = strdup(file->image_source);
        if (!copy->image_path || !copy->image_source)
        {
            lx_host_file_close(copy);
            if (error)
                *error = LX_HOST_FS_NO_MEMORY;
            return NULL;
        }
    }
    return copy;
}
void lx_host_file_close(LxHostFile *file)
{
    if (file)
    {
        free(file->image_path);
        free(file->image_source);
        if (file->image_lock && atomic_fetch_sub(&file->image_lock->references, 1) == 1)
        {
            pthread_mutex_destroy(&file->image_lock->mutex);
            free(file->image_lock);
        }
        lx_host_file_dispose(file);
    }
}
static LxHostFsError refresh_image(LxHostFile *file)
{
    if (!file->image || tb_image_device_kind(file->image_path) == 1)
        return LX_HOST_FS_OK;
    char *source = tb_image_source(file->image_path);
    int same = source && !strcmp(source, file->image_source);
    free(source);
    if (!same)
        return LX_HOST_FS_OK;
    FILE *snapshot = tb_image_fopen(file->image_path);
    if (!snapshot)
        return LX_HOST_FS_OK;
    unsigned char data[65536];
    size_t n;
    off_t at = 0;
    LxHostFsError status = LX_HOST_FS_OK;
    while ((n = fread(data, 1, sizeof data, snapshot)))
    {
        ssize_t written;
        do
            written = pwrite(file->fd, data, n, at);
        while (written < 0 && errno == EINTR);
        if (written != (ssize_t)n)
        {
            status = written < 0 ? fs_error(errno) : LX_HOST_FS_IO;
            break;
        }
        at += n;
    }
    if (status == LX_HOST_FS_OK && ferror(snapshot))
        status = LX_HOST_FS_IO;
    if (status == LX_HOST_FS_OK)
    {
        int result;
        do
            result = ftruncate(file->fd, at);
        while (result < 0 && errno == EINTR);
        if (result < 0)
            status = fs_error(errno);
    }
    fclose(snapshot);
    return status;
}
LxHostFsError lx_host_file_read(LxHostFile *file, void *data, size_t size, size_t *actual)
{
    image_lock(file);
    LxHostFsError refreshed = refresh_image(file);
    if (refreshed != LX_HOST_FS_OK)
    {
        *actual = 0;
        return image_done(file, refreshed);
    }
    ssize_t result;
    do
    {
        result = read(file->fd, data, size);
    } while (result < 0 && errno == EINTR);
    *actual = result < 0 ? 0 : (size_t)result;
    return image_done(file, result < 0 ? fs_error(errno) : LX_HOST_FS_OK);
}
LxHostFsError lx_host_file_write(LxHostFile *file, const void *data, size_t size, size_t *actual)
{
    image_lock(file);
    if (file->image)
    {
        *actual = 0;
        if (!file->image_writable)
            return image_done(file,
                              file->image_readonly ? LX_HOST_FS_READ_ONLY : LX_HOST_FS_DENIED);
        off_t position = lseek(file->fd, 0, SEEK_CUR);
        if (position < 0)
            return image_done(file, fs_error(errno));
        if (!tb_image_write(file->image_path, file->image_source, (uint64_t)position, data, size))
            return image_done(file, fs_error(errno));
    }
    ssize_t result;
    do
    {
        result = write(file->fd, data, size);
    } while (result < 0 && errno == EINTR);
    *actual = result < 0 ? 0 : (size_t)result;
    return image_done(file, result < 0 ? fs_error(errno) : LX_HOST_FS_OK);
}
LxHostFsError lx_host_file_seek(LxHostFile *file, int64_t distance, unsigned origin,
                                uint64_t *position)
{
    if (origin > 2 || (int64_t)(off_t)distance != distance)
        return LX_HOST_FS_INVALID;
    image_lock(file);
    LxHostFsError refreshed = refresh_image(file);
    if (refreshed != LX_HOST_FS_OK)
        return image_done(file, refreshed);
    const int origins[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    off_t result = lseek(file->fd, (off_t)distance, origins[origin]);
    if (result < 0)
        return image_done(file, fs_error(errno));
    if (position)
        *position = (uint64_t)result;
    return image_done(file, LX_HOST_FS_OK);
}
static void describe(const struct stat *native, LxHostFileInfo *info)
{
    info->size = native->st_size < 0 ? 0 : (uint64_t)native->st_size;
    info->allocated = native->st_blocks < 0 ? 0 : (uint64_t)native->st_blocks * 512;
    info->accessed = native->st_atime;
    info->modified = native->st_mtime;
    info->readonly = !(native->st_mode & S_IWUSR);
    info->kind = S_ISREG(native->st_mode)                               ? LX_HOST_FILE_REGULAR
                 : S_ISDIR(native->st_mode)                             ? LX_HOST_FILE_DIRECTORY
                 : S_ISCHR(native->st_mode) || S_ISBLK(native->st_mode) ? LX_HOST_FILE_DEVICE
                                                                        : LX_HOST_FILE_PIPE;
}
LxHostFsError lx_host_file_info(LxHostFile *file, LxHostFileInfo *info)
{
    image_lock(file);
    LxHostFsError refreshed = refresh_image(file);
    if (refreshed != LX_HOST_FS_OK)
        return image_done(file, refreshed);
    struct stat native;
    if (fstat(file->fd, &native))
        return image_done(file, fs_error(errno));
    describe(&native, info);
    if (file->image)
    {
        TbImageInfo image;
        info->readonly =
            tb_image_info(file->image_path, &image) ? image.readonly : file->image_readonly;
    }
    return image_done(file, LX_HOST_FS_OK);
}
LxHostFsError lx_host_file_resize(LxHostFile *file, uint64_t size)
{
    if (size > INT64_MAX || (uint64_t)(off_t)size != size)
        return LX_HOST_FS_INVALID;
    image_lock(file);
    if (file->image)
    {
        if (!file->image_writable)
            return image_done(file,
                              file->image_readonly ? LX_HOST_FS_READ_ONLY : LX_HOST_FS_DENIED);
        if (!tb_image_resize(file->image_path, file->image_source, size))
            return image_done(file, fs_error(errno));
    }
    return image_done(file, ftruncate(file->fd, (off_t)size) ? fs_error(errno) : LX_HOST_FS_OK);
}
LxHostFsError lx_host_file_flush(LxHostFile *file)
{
    return fsync(file->fd) ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_file_readonly(LxHostFile *file, int readonly)
{
    if (file->image)
        return tb_image_readonly(file->image_path, readonly) ? LX_HOST_FS_OK : fs_error(errno);
    struct stat info;
    if (fstat(file->fd, &info))
        return fs_error(errno);
    mode_t mode = info.st_mode & 07777;
    if (readonly)
        mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        mode |= S_IWUSR;
    return fchmod(file->fd, mode) ? fs_error(errno) : LX_HOST_FS_OK;
}
int lx_host_path_exists(const char *path)
{
    if (tb_image_path(path))
    {
        TbImageInfo info;
        return tb_image_info(path, &info);
    }
    return access(path, F_OK) == 0;
}
LxHostFsError lx_host_path_readonly(const char *path, int readonly)
{
    if (tb_image_path(path))
        return tb_image_readonly(path, readonly) ? LX_HOST_FS_OK : fs_error(errno);
    struct stat info;
    if (stat(path, &info))
        return fs_error(errno);
    mode_t mode = info.st_mode & 07777;
    if (readonly)
        mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        mode |= S_IWUSR;
    return chmod(path, mode) ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_info(const char *path, LxHostFileInfo *info)
{
    if (tb_image_path(path))
    {
        TbImageInfo image;
        if (!tb_image_info(path, &image))
            return fs_error(errno);
        *info = (LxHostFileInfo){0};
        info->size = image.size;
        info->allocated = image.size;
        info->readonly = image.readonly;
        info->kind = image.directory ? LX_HOST_FILE_DIRECTORY : LX_HOST_FILE_REGULAR;
        return LX_HOST_FS_OK;
    }
    struct stat native;
    if (stat(path, &native))
        return fs_error(errno);
    describe(&native, info);
    return LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_remove(const char *path, int directory)
{
    if (tb_image_path(path))
        return tb_image_remove(path, directory) ? LX_HOST_FS_OK : fs_error(errno);
    int result = directory ? rmdir(path) : unlink(path);
    return result ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_move(const char *source, const char *target)
{
    if (tb_image_path(source) || tb_image_path(target))
    {
        if (!tb_image_path(source) || !tb_image_path(target))
            return LX_HOST_FS_CROSS_DEVICE;
        return tb_image_move(source, target) ? LX_HOST_FS_OK : fs_error(errno);
    }
    return rename(source, target) ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_mkdir(const char *path)
{
    if (tb_image_path(path))
        return tb_image_mkdir(path) ? LX_HOST_FS_OK : fs_error(errno);
    return mkdir(path, 0755) ? fs_error(errno) : LX_HOST_FS_OK;
}
LxHostFsError lx_host_path_chdir(const char *path)
{
    if (tb_image_path(path))
    {
        TbImageInfo info;
        return !tb_image_info(path, &info) ? fs_error(errno)
               : info.directory            ? LX_HOST_FS_OK
                                           : LX_HOST_FS_NOT_DIRECTORY;
    }
    return chdir(path) ? fs_error(errno) : LX_HOST_FS_OK;
}
char *lx_host_path_current(void)
{
    return getcwd(NULL, 0);
}
char *lx_host_path_canonical(const char *path)
{
    if (tb_image_path(path))
        return tb_image_canonical(path);
    return realpath(path, NULL);
}
LxHostDirectory *lx_host_directory_open(const char *path, LxHostFsError *error)
{
    if (tb_image_path(path))
    {
        size_t count;
        char **names = tb_image_names(path, &count);
        if (count == (size_t)-1)
        {
            if (error)
                *error = fs_error(errno);
            return NULL;
        }
        LxHostDirectory *directory = calloc(1, sizeof *directory);
        char *copy = strdup(path);
        if (!directory || !copy)
        {
            for (size_t i = 0; i < count; i++)
                free(names[i]);
            free(names);
            free(directory);
            free(copy);
            if (error)
                *error = LX_HOST_FS_NO_MEMORY;
            return NULL;
        }
        directory->names = names;
        directory->count = count;
        directory->path = copy;
        directory->image = 1;
        if (error)
            *error = LX_HOST_FS_OK;
        return directory;
    }
    DIR *object = opendir(path);
    if (!object)
    {
        if (error)
            *error = fs_error(errno);
        return NULL;
    }
    LxHostDirectory *directory = calloc(1, sizeof *directory);
    if (!directory)
    {
        closedir(object);
        if (error)
            *error = LX_HOST_FS_NO_MEMORY;
        return NULL;
    }
    directory->object = object;
    if (error)
        *error = LX_HOST_FS_OK;
    return directory;
}
int lx_host_directory_read(LxHostDirectory *directory, const char **name, LxHostFileInfo *info,
                           LxHostFsError *error)
{
    if (directory->image)
    {
        if (directory->position == directory->count)
        {
            if (error)
                *error = LX_HOST_FS_OK;
            return 0;
        }
        *name = directory->names[directory->position++];
        if (info)
        {
            size_t n = strlen(directory->path) + strlen(*name) + 2;
            char *path = malloc(n);
            if (!path)
            {
                if (error)
                    *error = LX_HOST_FS_NO_MEMORY;
                return -1;
            }
            snprintf(path, n, "%s/%s", directory->path, *name);
            LxHostFsError result = lx_host_path_info(path, info);
            free(path);
            if (result)
            {
                if (error)
                    *error = result;
                return -1;
            }
        }
        if (error)
            *error = LX_HOST_FS_OK;
        return 1;
    }
    for (;;)
    {
        errno = 0;
        struct dirent *entry = readdir(directory->object);
        if (!entry)
        {
            int saved = errno;
            if (error)
                *error = fs_error(saved);
            return saved ? -1 : 0;
        }
        if (info)
        {
            struct stat native;
            if (fstatat(dirfd(directory->object), entry->d_name, &native, 0))
                continue;
            describe(&native, info);
        }
        *name = entry->d_name;
        if (error)
            *error = LX_HOST_FS_OK;
        return 1;
    }
}
uint64_t lx_host_directory_position(LxHostDirectory *directory)
{
    return directory->image ? directory->position : (uint64_t)telldir(directory->object);
}
void lx_host_directory_seek(LxHostDirectory *directory, uint64_t position)
{
    if (directory->image)
    {
        if (position <= directory->count)
            directory->position = (size_t)position;
        return;
    }
    if (position <= LONG_MAX)
        seekdir(directory->object, (long)position);
}
void lx_host_directory_close(LxHostDirectory *directory)
{
    if (directory)
    {
        if (directory->image)
        {
            for (size_t i = 0; i < directory->count; i++)
                free(directory->names[i]);
            free(directory->names);
            free(directory->path);
        }
        else
            closedir(directory->object);
        free(directory);
    }
}
void *lx_host_file_snapshot(const char *path, size_t maximum, size_t *size, LxHostFsError *error)
{
    *size = 0;
    LxHostFsError result;
    LxHostFile *file = lx_host_file_open(path, LX_HOST_FILE_READ, 0, &result);
    if (!file)
    {
        if (error)
            *error = result;
        return NULL;
    }
    LxHostFileInfo info;
    result = lx_host_file_info(file, &info);
    if (!result &&
        (info.kind != LX_HOST_FILE_REGULAR || info.size > maximum || info.size >= SIZE_MAX))
        result = LX_HOST_FS_INVALID;
    void *data = NULL;
    if (!result)
    {
        data = malloc((size_t)info.size + 1);
        if (!data)
            result = LX_HOST_FS_NO_MEMORY;
    }
    size_t used = 0;
    while (!result && used < info.size)
    {
        size_t actual;
        result = lx_host_file_read(file, (uint8_t *)data + used, (size_t)info.size - used, &actual);
        if (!result && !actual)
            result = LX_HOST_FS_IO;
        used += actual;
    }
    lx_host_file_close(file);
    if (result)
    {
        free(data);
        data = NULL;
    }
    else
    {
        ((uint8_t *)data)[used] = 0;
        *size = used;
    }
    if (error)
        *error = result;
    return data;
}

LxHostFsError lx_host_path_space(const char *path, uint64_t *total, uint64_t *available)
{
    if (tb_image_path(path))
    {
        TbImageInfo info;
        char *root = tb_image_volume_root(path);
        int ok = root && tb_image_info(root, &info);
        free(root);
        if (!ok)
            return fs_error(errno);
        if (!info.capacity)
            return LX_HOST_FS_NOT_READY;
        *total = info.capacity;
        *available = info.available;
        return LX_HOST_FS_OK;
    }
    struct statvfs info;
    if (statvfs(path, &info))
        return fs_error(errno);
    uint64_t block = info.f_frsize ? info.f_frsize : info.f_bsize;
    if (!block || (uint64_t)info.f_blocks > UINT64_MAX / block ||
        (uint64_t)info.f_bavail > UINT64_MAX / block)
        return LX_HOST_FS_INVALID;
    *total = (uint64_t)info.f_blocks * block;
    *available = (uint64_t)info.f_bavail * block;
    return LX_HOST_FS_OK;
}

LxHostFsError lx_host_path_volume(const char *path, LxHostVolumeInfo *info)
{
    memset(info, 0, sizeof *info);
    if (tb_image_path(path))
    {
        TbImageInfo image;
        if (!tb_image_info(path, &image))
            return fs_error(errno);
        strcpy(info->filesystem, *image.filesystem ? image.filesystem : image.fat ? "FAT" : "CDFS");
        memcpy(info->label, image.label, sizeof info->label);
        info->serial = image.serial;
        info->sector_bytes = image.sector_bytes;
        info->cluster_bytes = image.cluster_bytes;
        info->readonly = image.readonly;
        info->removable = tb_image_device_kind(path) != 3;
        info->reserved_sectors = image.reserved_sectors;
        info->fat_count = image.fat_count;
        info->root_entries = image.root_entries;
        info->fat_sectors = image.fat_sectors;
        info->sectors_per_track = image.sectors_per_track;
        info->heads = image.heads;
        info->hidden_sectors = image.hidden_sectors;
        info->media = image.media;
    }
    else
    {
        strcpy(info->filesystem, "HPFS");
        strcpy(info->label, "2ine");
        info->sector_bytes = 512;
        info->cluster_bytes = 4096;
    }
    return LX_HOST_FS_OK;
}
