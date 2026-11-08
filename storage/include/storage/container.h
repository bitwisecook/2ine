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
    int64_t modified; /* INT64_MIN when absent. */
} TbMediaEntry;

int tb_media_path(const char *path);
/* Opaque paths retain each container boundary without repeated path encoding. */
char *tb_media_root(const char *source);
char *tb_media_child(const char *directory, const char *name);
char *tb_media_parent(const char *path);
char *tb_media_label(const char *path);
/* Relative name in the innermost container, without display separators. */
char *tb_media_member_name(const char *path);
char *tb_media_host_source(const char *path);
FILE *tb_media_fopen(const char *path);
TbMediaEntry *tb_media_entries(const char *path, size_t *count);
void tb_media_entries_free(TbMediaEntry *entries, size_t count);
int tb_media_container(const char *path);
/* A request-scoped reader cache retains seekable parent containers. */
void tb_media_session_begin(void);
void tb_media_session_end(void);

typedef struct TbMediaReadStats
{
    uint64_t decompressed_bytes, peak_cache_bytes, direct_slices, materialized_files;
    uint64_t container_cache_hits, directory_index_builds, directory_index_hits;
    uint64_t inflate_checkpoints, inflate_restores;
} TbMediaReadStats;

TbMediaReadStats tb_media_read_stats(void);
#endif
