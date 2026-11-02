/* Public domain. No rights reserved. Wholly AI-authored. */
/* POSIX process locks require deferring unrelated closes while an inode has locked ranges. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "hal/posix/fs_internal.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
typedef struct Range
{
    uint64_t offset, length;
    struct Range *next;
} Range;
typedef struct Owner
{
    LxHostFile *file;
    dev_t device;
    ino_t inode;
    Range *ranges;
    struct Owner *next;
} Owner;
typedef struct Retired
{
    int fd;
    dev_t device;
    ino_t inode;
    struct Retired *next;
} Retired;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static Owner *owners;
static Retired *retired;
static int enter(void)
{
    int previous;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    pthread_mutex_lock(&mutex);
    return previous;
}
static void leave(int previous)
{
    pthread_mutex_unlock(&mutex);
    pthread_setcancelstate(previous, NULL);
}
static LxHostFsError result_error(int error)
{
    return !error                                  ? LX_HOST_FS_OK
           : error == EAGAIN || error == EACCES    ? LX_HOST_FS_BUSY
           : error == EBADF                        ? LX_HOST_FS_DENIED
           : error == EINVAL || error == EOVERFLOW ? LX_HOST_FS_INVALID
                                                   : LX_HOST_FS_IO;
}
static int native_range(int fd, uint64_t offset, uint64_t length, short type)
{
    struct flock lock = {0};
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = (off_t)offset;
    lock.l_len = (off_t)length;
    int result;
    do
        result = fcntl(fd, F_SETLK, &lock);
    while (result < 0 && errno == EINTR);
    return result ? errno : 0;
}
static int active(dev_t device, ino_t inode)
{
    for (Owner *owner = owners; owner; owner = owner->next)
        if (owner->device == device && owner->inode == inode && owner->ranges)
            return 1;
    return 0;
}
static void drain(dev_t device, ino_t inode)
{
    if (active(device, inode))
        return;
    for (Retired **link = &retired; *link;)
    {
        Retired *entry = *link;
        if (entry->device != device || entry->inode != inode)
        {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        close(entry->fd);
        free(entry);
    }
}
static int overlap(uint64_t offset, uint64_t length, const Range *range)
{
    return length && offset < range->offset + range->length && range->offset < offset + length;
}
void lx_host_file_dispose(LxHostFile *file)
{
    int previous = enter();
    struct stat info;
    int known = !fstat(file->fd, &info);
    for (Owner **link = &owners; *link; link = &(*link)->next)
        if ((*link)->file == file)
        {
            Owner *owner = *link;
            *link = owner->next;
            while (owner->ranges)
            {
                Range *range = owner->ranges;
                owner->ranges = range->next;
                native_range(file->fd, range->offset, range->length, F_UNLCK);
                free(range);
            }
            free(owner);
            break;
        }
    if (known && active(info.st_dev, info.st_ino))
    {
        Retired *entry = malloc(sizeof *entry);
        if (entry)
        {
            *entry = (Retired){file->fd, info.st_dev, info.st_ino, retired};
            retired = entry;
        }
        /* On allocation failure, retain the descriptor to preserve other owners' locks. */
    }
    else
    {
        close(file->fd);
        if (known)
            drain(info.st_dev, info.st_ino);
    }
    free(file);
    leave(previous);
}
LxHostFsError lx_host_file_lock_range(LxHostFile *file, uint64_t offset, uint64_t length,
                                      int acquire)
{
    if (!file || !length || offset > INT64_MAX || length > (uint64_t)INT64_MAX - offset)
        return LX_HOST_FS_INVALID;
    int previous = enter();
    struct stat info;
    if (fstat(file->fd, &info))
    {
        LxHostFsError result = result_error(errno);
        leave(previous);
        return result;
    }
    Owner *owner;
    for (owner = owners; owner && owner->file != file; owner = owner->next)
    {
    }
    if (acquire)
    {
        for (Owner *other = owners; other; other = other->next)
            if (other->device == info.st_dev && other->inode == info.st_ino)
                for (Range *range = other->ranges; range; range = range->next)
                    if (overlap(offset, length, range))
                    {
                        leave(previous);
                        return LX_HOST_FS_BUSY;
                    }
        if (!owner)
        {
            owner = calloc(1, sizeof *owner);
            if (owner)
            {
                owner->file = file;
                owner->device = info.st_dev;
                owner->inode = info.st_ino;
                owner->next = owners;
                owners = owner;
            }
        }
        Range *range = owner ? calloc(1, sizeof *range) : NULL;
        if (!range)
        {
            leave(previous);
            return LX_HOST_FS_NO_MEMORY;
        }
        int error = native_range(file->fd, offset, length, F_WRLCK);
        if (!error)
        {
            *range = (Range){offset, length, owner->ranges};
            owner->ranges = range;
        }
        else
            free(range);
        leave(previous);
        return result_error(error);
    }
    Range **link = owner ? &owner->ranges : NULL;
    while (link && *link && ((*link)->offset != offset || (*link)->length != length))
        link = &(*link)->next;
    if (!link || !*link)
    {
        leave(previous);
        return LX_HOST_FS_INVALID;
    }
    int error = native_range(file->fd, offset, length, F_UNLCK);
    if (!error)
    {
        Range *range = *link;
        *link = range->next;
        free(range);
        drain(info.st_dev, info.st_ino);
    }
    leave(previous);
    return result_error(error);
}
LxHostFsError lx_host_file_range_conflict(LxHostFile *file, uint64_t offset, uint64_t length,
                                          int *conflict)
{
    if (!file || !conflict || offset > INT64_MAX || length > (uint64_t)INT64_MAX - offset)
        return LX_HOST_FS_INVALID;
    *conflict = 0;
    if (!length)
        return LX_HOST_FS_OK;
    int previous = enter();
    struct stat info;
    if (fstat(file->fd, &info))
    {
        LxHostFsError result = result_error(errno);
        leave(previous);
        return result;
    }
    for (Owner *owner = owners; owner; owner = owner->next)
        if (owner->file != file && owner->device == info.st_dev && owner->inode == info.st_ino)
            for (Range *range = owner->ranges; range; range = range->next)
                if (overlap(offset, length, range))
                {
                    *conflict = 1;
                    leave(previous);
                    return LX_HOST_FS_OK;
                }
    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = (off_t)offset;
    lock.l_len = (off_t)length;
    int error = fcntl(file->fd, F_GETLK, &lock) ? errno : 0;
    if (!error)
        *conflict = lock.l_type != F_UNLCK;
    leave(previous);
    return result_error(error);
}
