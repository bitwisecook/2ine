/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_PLATFORM_H
#define TB_PLATFORM_H
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
/* Path results are owned allocations. NULL preserves an errno failure reason. */
char *tb_absolute(const char *path);
char *tb_canonical(const char *path);
int tb_is_dir(const char *path);
int tb_is_file(const char *path);
/* Regular executable with a native host image format and supported CPU type. */
int tb_host_executable(const char *path);
int tb_host_native_execution(void);
int tb_host_uses_dyld(void);
int tb_mkdirs(const char *path);
char **tb_names(const char *directory, size_t *count);
void tb_names_free(char **names, size_t count);
FILE *tb_fopen(const char *path, const char *mode);
int tb_file_descriptor(FILE *file);
int tb_atomic_write(const char *path, const void *data, size_t length);
int tb_directory_readable(const char *path);
int tb_is_link(const char *path);
int tb_mkdir_new(const char *path);
int tb_copy_new(const char *source, const char *destination);
int tb_copy(const char *source, const char *destination);
int tb_file_info(FILE *file, int64_t *size, int *mode);
int tb_path_info(const char *path, uint64_t *size, int64_t *modified);

typedef struct TbFileIdentity
{
    uint64_t device, file, size, modified, changed, modified_fraction, changed_fraction;
} TbFileIdentity;

int tb_path_identity(const char *path, TbFileIdentity *identity);
typedef struct TbMutationLock TbMutationLock;
/* Nonblocking cooperative process/thread lock. directory must already exist.
 * Contention returns NULL with errno EBUSY. The persistent lock file is not removed. */
TbMutationLock *tb_mutation_lock(const char *directory);
void tb_mutation_unlock(TbMutationLock *lock);
/* Remove a regular host file only when its identity still matches expected.
 * Call while holding the ball mutation lock. POSIX checks the held descriptor and
 * directory entry, but cannot exclude an uncooperative rename after its final check. */
int tb_remove_managed_file(const char *path, const TbFileIdentity *expected);
int tb_file_mode(FILE *file, int mode);
int tb_replace(const char *source, const char *destination);
/* Atomic same-filesystem move; an existing destination is never replaced. */
int tb_move_new(const char *source, const char *destination);
int tb_remove_tree(const char *path);
char *tb_archive_workspace(const char *parent);
char *tb_home(void);
int tb_path_equal(const char *a, const char *b, size_t length);
#endif
