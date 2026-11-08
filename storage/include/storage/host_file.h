/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host files named by UTF-8 paths on every host. */
#ifndef TB_STORAGE_HOST_FILE_H
#define TB_STORAGE_HOST_FILE_H
#include <stdint.h>
#include <stdio.h>

/* Identifies one regular host file and its last change; equal identities mean
   the same unchanged file. */
typedef struct TbFileIdentity
{
    uint64_t device, file, size, modified, changed, modified_fraction, changed_fraction;
} TbFileIdentity;

FILE *tb_host_fopen(const char *path, const char *mode);
/* Owned absolute path with links resolved; NULL preserves an errno reason. */
char *tb_host_canonical(const char *path);
/* Regular files only. */
int tb_host_identity(const char *path, TbFileIdentity *identity);
#endif
