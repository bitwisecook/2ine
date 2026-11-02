/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_MEDIA_H
#define TB_MEDIA_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
typedef struct TbMediaEntry
{
    char *name;
    uint64_t size;
    int directory;
} TbMediaEntry;
int tb_media_path(const char *path);
/* Opaque paths retain each container boundary without repeated path encoding. */
char *tb_media_root(const char *source);
char *tb_media_child(const char *directory, const char *name);
char *tb_media_parent(const char *path);
char *tb_media_label(const char *path);
FILE *tb_media_fopen(const char *path);
TbMediaEntry *tb_media_entries(const char *path, size_t *count);
void tb_media_entries_free(TbMediaEntry *entries, size_t count);
int tb_media_container(const char *path);
#endif
