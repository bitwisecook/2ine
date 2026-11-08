/* Public domain. No rights reserved. Wholly AI-authored. */
/* FatFs stays inside the process. Its disk callbacks access image files only. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/volume/volume_internal.h"
#include "storage/common/file_time.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "storage/vendor/fatfs/ff.h"
#include "storage/vendor/fatfs/diskio.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* FatFs keeps one mounted volume in process-global state. */
#define LOCK() tb_storage_lock(TB_STORAGE_LOCK_FATFS)
#define UNLOCK() tb_storage_unlock(TB_STORAGE_LOCK_FATFS)

static struct
{
    FILE *file;
    uint64_t bytes;
    unsigned sector;
    int writable;
    FATFS fs;
} media;

static int result(FRESULT value)
{
    if (value == FR_OK)
        return 1;
    errno = value == FR_NO_FILE || value == FR_NO_PATH  ? ENOENT
            : value == FR_EXIST                         ? EEXIST
            : value == FR_DENIED                        ? EACCES
            : value == FR_WRITE_PROTECTED               ? EROFS
            : value == FR_NOT_READY                     ? ENXIO
            : value == FR_NOT_ENOUGH_CORE               ? ENOMEM
            : value == FR_LOCKED || value == FR_TIMEOUT ? EBUSY
            : value == FR_DISK_ERR                      ? EIO
                                                        : EINVAL;
    return 0;
}

void *ff_memalloc(UINT n)
{
    return malloc(n);
}

void ff_memfree(void *p)
{
    free(p);
}

DWORD get_fattime(void)
{
    time_t now = time(NULL);
    struct tm date;
    tb_host_localtime((int64_t)now, &date);
    unsigned year = date.tm_year + 1900;
    if (year < 1980)
        year = 1980;
    if (year > 2107)
        year = 2107;
    return (year - 1980) << 25 | (date.tm_mon + 1) << 21 | date.tm_mday << 16 | date.tm_hour << 11 |
           date.tm_min << 5 | date.tm_sec / 2;
}

DSTATUS disk_initialize(BYTE drive)
{
    return drive || !media.file ? STA_NOINIT : media.writable ? 0 : STA_PROTECT;
}

DSTATUS disk_status(BYTE drive)
{
    return disk_initialize(drive);
}

DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    if (drive || !media.file || !count || sector > media.bytes / media.sector ||
        count > media.bytes / media.sector - sector)
        return RES_PARERR;
    if (fseeko(media.file, (int64_t)(sector * media.sector), SEEK_SET) ||
        fread(data, media.sector, count, media.file) != count)
        return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count)
{
    if (!media.writable)
        return RES_WRPRT;
    if (drive || !media.file || !count || sector > media.bytes / media.sector ||
        count > media.bytes / media.sector - sector)
        return RES_PARERR;
    if (fseeko(media.file, (int64_t)(sector * media.sector), SEEK_SET) ||
        fwrite(data, media.sector, count, media.file) != count)
        return RES_ERROR;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    if (drive || !media.file)
        return RES_NOTRDY;
    if (command == GET_SECTOR_COUNT)
        *(LBA_t *)data = media.bytes / media.sector;
    else if (command == GET_SECTOR_SIZE)
        *(WORD *)data = media.sector;
    else if (command == GET_BLOCK_SIZE)
        *(DWORD *)data = 1;
    else if (command == CTRL_SYNC)
    {
        if (fflush(media.file))
            return RES_ERROR;
        if (media.writable && !tb_host_sync(media.file))
            return RES_ERROR;
    }
    else
        return RES_PARERR;
    return RES_OK;
}

/* Accepts exFAT (2), FAT32 (1) or an MBR with a FAT32 or type 07 primary partition (3).
   A partitioned image is probed internally by FatFs, never attached to macOS. */
int tb_fat_volume(const TbVolumeProbe *probe)
{
    if (probe->exfat)
        return 2;
    if (probe->fat32)
        return 1;
    if (probe->mbr)
        for (unsigned i = 0; i < MBR_PARTITIONS; i++)
        {
            unsigned type = probe->partitions[i].type;
            if (type == MBR_TYPE_FAT32 || type == MBR_TYPE_FAT32_LBA || type == MBR_TYPE_IFS)
                return 3;
        }
    return 0;
}

static int begin_source(const char *source, int writable, int mount)
{
    memset(&media, 0, sizeof media);
    media.file = tb_image_open_source(source, writable);
    if (!media.file)
        return 0;
    if (!tb_image_lock_source(media.file, writable) || fseeko(media.file, 0, SEEK_END))
        goto fail;
    int64_t size = ftello(media.file);
    if (size < 512)
    {
        errno = EINVAL;
        goto fail;
    }
    media.bytes = size;
    media.sector = 512;
    media.writable = writable;
    TbVolumeProbe probe;
    tb_volume_probe_file(media.file, media.bytes, &probe);
    if (probe.boot_bytes != PROBE_SECTOR_BYTES)
    {
        errno = probe.boot_error ? probe.boot_error : EIO;
        goto fail;
    }
    if (probe.sector_bytes)
        media.sector = probe.sector_bytes;
    if (mount && !result(f_mount(&media.fs, "0:", 1)))
        goto fail;
    return 1;
fail:
{
    int saved = errno;
    f_mount(NULL, "0:", 0);
    fclose(media.file);
    media.file = NULL;
    errno = saved;
    return 0;
}
}

static int begin(const char *path, int writable, const char *expected)
{
    int readonly = tb_image_protected(path);
    if (readonly < 0)
        return 0;
    if (writable && readonly)
    {
        errno = EROFS;
        return 0;
    }
    char *source = tb_image_source(path);
    if (!source)
        return 0;
    if (expected && strcmp(expected, source))
    {
        free(source);
        errno = ENXIO;
        return 0;
    }
    int ok = begin_source(source, writable, 1);
    free(source);
    return ok;
}

static void end(void)
{
    int saved = errno;
    f_mount(NULL, "0:", 0);
    if (media.file)
        fclose(media.file);
    media.file = NULL;
    errno = saved;
}

static char *member_path(const char *path)
{
    const char *member = tb_image_member(path);
    if (!member || strpbrk(member, "\\:"))
    {
        errno = EINVAL;
        return NULL;
    }
    size_t length = strlen(member), used = 3, count = 0;
    char *out = malloc(length + 4), *copy = strdup(member);
    size_t boundaries[256];
    if (!out || !copy)
    {
        free(out);
        free(copy);
        return NULL;
    }
    memcpy(out, "0:/", 3);
    char *cursor = copy;
    while (*cursor)
    {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char *part = cursor;
        while (*cursor && *cursor != '/')
            cursor++;
        if (*cursor)
            *cursor++ = 0;
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, ".."))
        {
            if (!count)
            {
                free(out);
                free(copy);
                errno = EACCES;
                return NULL;
            }
            used = boundaries[--count];
            continue;
        }
        if (count == 256)
        {
            free(out);
            free(copy);
            errno = ENAMETOOLONG;
            return NULL;
        }
        boundaries[count++] = used;
        if (used > 3)
            out[used++] = '/';
        size_t n = strlen(part);
        memcpy(out + used, part, n);
        used += n;
    }
    out[used] = 0;
    free(copy);
    return out;
}

int tb_fat_info(const char *path, TbImageInfo *info)
{
    char *name = member_path(path);
    if (!name)
        return 0;
    LOCK();
    int ok = begin(path, 0, NULL);
    FILINFO file = {0};
    if (ok && strlen(name) > 3)
        ok = result(f_stat(name, &file));
    else
        file.fattrib = AM_DIR;
    if (ok)
    {
        memset(info, 0, sizeof *info);
        info->directory = !!(file.fattrib & AM_DIR);
        info->size = file.fsize;
        info->modified = tb_dos_time(file.fdate, file.ftime);
        info->attributes = file.fattrib & (AM_HID | AM_SYS | AM_ARC);
        int64_t created = tb_dos_time(file.crdate, file.crtime);
        info->created = created == INT64_MIN ? 0 : created;
        WORD access = 0;
        if (f_accessdate(name, &access, 0) == FR_OK && access)
        {
            int64_t accessed = tb_dos_time(access, 0);
            info->accessed = accessed == INT64_MIN ? 0 : accessed;
        }
        info->fat = 1;
        info->readonly = tb_image_protected(path) || !!(file.fattrib & AM_RDO);
        info->sector_bytes = media.sector;
        info->cluster_bytes = media.fs.csize * media.sector;
        info->capacity = (uint64_t)(media.fs.n_fatent - 2) * info->cluster_bytes;
        strcpy(info->filesystem, media.fs.fs_type == FS_EXFAT   ? "EXFAT"
                                 : media.fs.fs_type == FS_FAT32 ? "FAT32"
                                                                : "FAT");
        char label[48];
        DWORD serial = 0;
        if (f_getlabel("0:", label, &serial) == FR_OK)
        {
            size_t n = strlen(label);
            if (n > 11)
                n = 11;
            memcpy(info->label, label, n);
        }
        info->serial = serial;
        DWORD clusters;
        FATFS *fs;
        if (!info->readonly && strlen(name) == 3)
        {
            ok = result(f_getfree("0:", &clusters, &fs));
            if (ok)
                info->available = (uint64_t)clusters * info->cluster_bytes;
        }
    }
    end();
    UNLOCK();
    free(name);
    return ok;
}

char **tb_fat_names(const char *path, size_t *count)
{
    *count = (size_t)-1;
    char *name = member_path(path);
    if (!name)
        return NULL;
    LOCK();
    int ok = begin(path, 0, NULL);
    DIR directory;
    int opened = 0;
    char **names = NULL;
    size_t n = 0;
    if (ok)
    {
        ok = result(f_opendir(&directory, name));
        opened = ok;
    }
    while (ok)
    {
        FILINFO file;
        ok = result(f_readdir(&directory, &file));
        if (!ok || !file.fname[0])
            break;
        char *copy = strdup(file.fname);
        char **next = copy ? realloc(names, (n + 1) * sizeof *names) : NULL;
        if (!next)
        {
            free(copy);
            ok = 0;
            errno = ENOMEM;
            break;
        }
        names = next;
        names[n++] = copy;
    }
    if (opened)
        f_closedir(&directory);
    end();
    UNLOCK();
    free(name);
    if (!ok)
    {
        for (size_t i = 0; i < n; i++)
            free(names[i]);
        free(names);
        return NULL;
    }
    *count = n;
    return names;
}

FILE *tb_fat_fopen(const char *path)
{
    char *name = member_path(path);
    if (!name)
        return NULL;
    LOCK();
    int ok = begin(path, 0, NULL);
    FIL file;
    int opened = 0;
    FILE *snapshot = NULL;
    if (ok)
    {
        ok = result(f_open(&file, name, FA_READ));
        opened = ok;
    }
    if (ok)
    {
        snapshot = tmpfile();
        ok = snapshot != NULL;
    }

    enum
    {
        BUFFER_SIZE = 65536
    };

    unsigned char *data = ok ? malloc(BUFFER_SIZE) : NULL;
    if (ok && !data)
        ok = 0;
    UINT n;
    while (ok)
    {
        ok = result(f_read(&file, data, BUFFER_SIZE, &n));
        if (!ok || !n)
            break;
        if (fwrite(data, 1, n, snapshot) != n)
            ok = 0;
    }
    free(data);
    if (opened)
        f_close(&file);
    end();
    UNLOCK();
    free(name);
    if (ok)
        ok = !fflush(snapshot) && !fseeko(snapshot, 0, SEEK_SET);
    if (!ok && snapshot)
    {
        int saved = errno;
        fclose(snapshot);
        snapshot = NULL;
        errno = saved;
    }
    return snapshot;
}

static int zero_until(FIL *file, FSIZE_t offset)
{
    if (f_size(file) >= offset)
        return result(f_lseek(file, offset));
    if (!result(f_lseek(file, f_size(file))))
        return 0;
    unsigned char zeros[4096] = {0};
    while (f_tell(file) < offset)
    {
        FSIZE_t remain = offset - f_tell(file);
        UINT n = remain > sizeof zeros ? sizeof zeros : (UINT)remain, actual;
        if (!result(f_write(file, zeros, n, &actual)))
            return 0;
        if (actual != n)
        {
            errno = ENOSPC;
            return 0;
        }
    }
    return 1;
}

int tb_fat_modify(const char *path, const char *expected, int action, uint64_t offset,
                  const void *data, size_t size, const char *target)
{
    char *name = member_path(path), *other = target ? member_path(target) : NULL;
    if (!name || (target && !other))
    {
        free(name);
        free(other);
        return 0;
    }
    if (target)
    {
        char *a = tb_image_source(path), *b = tb_image_source(target);
        int same = a && b && !strcmp(a, b);
        free(a);
        free(b);
        if (!same || tb_image_protected(target))
        {
            free(name);
            free(other);
            errno = same ? EROFS : EXDEV;
            return 0;
        }
    }
    LOCK();
    int ok = begin(path, 1, expected);
    FIL file;
    int opened = 0;
    if (ok)
    {
        if (action == TB_FS_MKDIR)
            ok = result(f_mkdir(name));
        else if (action == TB_FS_RENAME)
            ok = result(f_rename(name, other));
        else if (action == TB_FS_READONLY || action == TB_FS_WRITABLE)
            ok = result(f_chmod(name, action == TB_FS_READONLY ? AM_RDO : 0, AM_RDO));
        else if (action == TB_FS_ATTRIBUTES)
            ok = result(f_chmod(name, (BYTE)offset, AM_HID | AM_SYS | AM_ARC));
        else if (action == TB_FS_TOUCH)
        {
            const TbImageStamps *stamps = data;
            FILINFO info = {0};
            if (stamps->modified != INT64_MIN)
            {
                uint32_t stamp = tb_dos_stamp(stamps->modified);
                info.fdate = (WORD)(stamp >> 16);
                info.ftime = (WORD)stamp;
            }
            if (stamps->created != INT64_MIN)
            {
                uint32_t stamp = tb_dos_stamp(stamps->created);
                info.crdate = (WORD)(stamp >> 16);
                info.crtime = (WORD)stamp;
            }
            ok = result(f_utime(name, &info));
            if (ok && stamps->accessed != INT64_MIN)
                ok = result(f_accessdate(name, NULL, (WORD)(tb_dos_stamp(stamps->accessed) >> 16)));
        }
        else if (action == TB_FS_DELETE || action == TB_FS_RMDIR)
        {
            FILINFO info;
            ok = result(f_stat(name, &info));
            if (ok && !!(info.fattrib & AM_DIR) != (action == TB_FS_RMDIR))
            {
                errno = info.fattrib & AM_DIR ? EISDIR : ENOTDIR;
                ok = 0;
            }
            if (ok && action == TB_FS_RMDIR)
            {
                DIR directory;
                ok = result(f_opendir(&directory, name));
                if (ok)
                {
                    FILINFO child;
                    ok = result(f_readdir(&directory, &child));
                    f_closedir(&directory);
                    if (ok && child.fname[0])
                    {
                        errno = ENOTEMPTY;
                        ok = 0;
                    }
                }
            }
            if (ok)
                ok = result(f_unlink(name));
        }
        else
        {
            BYTE flags = FA_WRITE | (action == TB_FS_CREATE      ? FA_OPEN_ALWAYS
                                     : action == TB_FS_EXCLUSIVE ? FA_CREATE_NEW
                                                                 : FA_OPEN_EXISTING);
            ok = result(f_open(&file, name, flags));
            opened = ok;
            if (ok && (action == TB_FS_WRITE || action == TB_FS_RESIZE))
                ok = zero_until(&file, offset);
            if (ok && action == TB_FS_RESIZE)
                ok = result(f_truncate(&file));
            if (ok && action == TB_FS_WRITE)
                for (size_t at = 0; at < size;)
                {
                    UINT n = size - at > 65536 ? 65536 : (UINT)(size - at), actual;
                    if (!result(f_write(&file, (const unsigned char *)data + at, n, &actual)))
                    {
                        ok = 0;
                        break;
                    }
                    if (actual != n)
                    {
                        errno = ENOSPC;
                        ok = 0;
                        break;
                    }
                    at += n;
                }
        }
    }
    if (opened && !result(f_close(&file)))
        ok = 0;
    end();
    UNLOCK();
    free(name);
    free(other);
    return ok;
}

int tb_fat_format(const char *path, const char *filesystem, uint64_t bytes)
{
    if (bytes < 1024 * 1024 || bytes > INT64_MAX || bytes % 512)
    {
        errno = EINVAL;
        return 0;
    }
    /* Exclusive creation never overwrites existing media. */
    FILE *new_file = NULL;
    new_file = tb_host_fopen(path, "wbx");
    if (!new_file)
        return 0;
    int ok = !fseeko(new_file, (int64_t)bytes - 1, SEEK_SET) && fputc(0, new_file) == 0;
    if (fclose(new_file))
        ok = 0;
    if (!ok)
    {
        remove(path);
        return 0;
    }
    LOCK();
    ok = begin_source(path, 1, 0);
    if (ok)
    {
        MKFS_PARM options = {0};
        options.fmt = (!strcmp(filesystem, "exfat") ? FM_EXFAT : FM_FAT32) | FM_SFD;
        options.n_fat = 2;
        unsigned char work[65536];
        ok = result(f_mkfs("0:", &options, work, sizeof work));
    }
    end();
    UNLOCK();
    if (!ok)
    {
        int saved = errno;
        remove(path);
        errno = saved;
    }
    return ok;
}
