/* Host filesystem objects and normalized results. No guest ABI appears here. */
#ifndef TWOINE_HOST_FS_H
#define TWOINE_HOST_FS_H
#include <stddef.h>
#include <stdint.h>
typedef struct LxHostFile LxHostFile;
typedef struct LxHostDirectory LxHostDirectory;
typedef enum LxHostFsError {
    LX_HOST_FS_OK, LX_HOST_FS_MISSING, LX_HOST_FS_NOT_DIRECTORY, LX_HOST_FS_EXISTS,
    LX_HOST_FS_DENIED, LX_HOST_FS_NO_MEMORY, LX_HOST_FS_TOO_MANY_FILES,
    LX_HOST_FS_FULL, LX_HOST_FS_IS_DIRECTORY, LX_HOST_FS_NAME_LONG,
    LX_HOST_FS_BUSY, LX_HOST_FS_READ_ONLY, LX_HOST_FS_CROSS_DEVICE,
    LX_HOST_FS_INVALID, LX_HOST_FS_IO, LX_HOST_FS_NOT_EMPTY
} LxHostFsError;
typedef enum LxHostFileKind {
    LX_HOST_FILE_REGULAR, LX_HOST_FILE_DIRECTORY, LX_HOST_FILE_DEVICE, LX_HOST_FILE_PIPE
} LxHostFileKind;
typedef struct LxHostFileInfo {
    uint64_t size, allocated;
    int64_t accessed, modified;
    LxHostFileKind kind;
    int readonly;
} LxHostFileInfo;
enum {
    LX_HOST_FILE_READ = 1, LX_HOST_FILE_WRITE = 2, LX_HOST_FILE_CREATE = 4,
    LX_HOST_FILE_EXCLUSIVE = 8, LX_HOST_FILE_TRUNCATE = 16,
    LX_HOST_FILE_SYNC = 32, LX_HOST_FILE_NOINHERIT = 64
};
LxHostFile *lx_host_file_open(const char *path, unsigned flags, int readonly, LxHostFsError *error);
/* A bounded regular-file snapshot is caller-owned and has an extra NUL byte. */
void *lx_host_file_snapshot(const char *path, size_t maximum, size_t *size, LxHostFsError *error);
LxHostFile *lx_host_file_standard(unsigned stream);
LxHostFile *lx_host_file_duplicate(LxHostFile *file, LxHostFsError *error);
void lx_host_file_close(LxHostFile *file);
LxHostFsError lx_host_file_read(LxHostFile *file, void *data, size_t size, size_t *actual);
LxHostFsError lx_host_file_write(LxHostFile *file, const void *data, size_t size, size_t *actual);
/* Origin: zero=start, one=current, two=end. The resulting position is unsigned. */
LxHostFsError lx_host_file_seek(LxHostFile *file, int64_t distance, unsigned origin, uint64_t *position);
LxHostFsError lx_host_file_info(LxHostFile *file, LxHostFileInfo *info);
LxHostFsError lx_host_file_resize(LxHostFile *file, uint64_t size);
LxHostFsError lx_host_file_flush(LxHostFile *file);
LxHostFsError lx_host_file_readonly(LxHostFile *file, int readonly);
int lx_host_path_exists(const char *path);
LxHostFsError lx_host_path_readonly(const char *path, int readonly);
LxHostFsError lx_host_path_info(const char *path, LxHostFileInfo *info);
LxHostFsError lx_host_path_remove(const char *path, int directory);
LxHostFsError lx_host_path_move(const char *source, const char *target);
LxHostFsError lx_host_path_mkdir(const char *path);
LxHostFsError lx_host_path_chdir(const char *path);
char *lx_host_path_current(void);
char *lx_host_path_canonical(const char *path);
LxHostDirectory *lx_host_directory_open(const char *path, LxHostFsError *error);
/* 1=entry, 0=end, -1=error. A returned name belongs to the directory and remains
   valid until the next read. Entries are followed through symbolic links. */
int lx_host_directory_read(LxHostDirectory *directory, const char **name, LxHostFileInfo *info, LxHostFsError *error);
uint64_t lx_host_directory_position(LxHostDirectory *directory);
void lx_host_directory_seek(LxHostDirectory *directory, uint64_t position);
void lx_host_directory_close(LxHostDirectory *directory);
#endif
