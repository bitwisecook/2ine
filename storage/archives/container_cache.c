/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded process cache of container sources and opened parent containers, and the
   per-thread request session with its read statistics. A source is leased exclusively
   for one request; a concurrent reader gets its own state instead of sharing
   file/decoder cursors. */
#define _POSIX_C_SOURCE 200809L
#include "storage/archives/container_internal.h"
#include "storage/common/file_time.h"
#include "storage/common/host.h"
#include "storage/common/host_stdio.h"
#include "storage/volume.h"
#include <stdlib.h>
#include <string.h>
#define CACHE_LOCK() tb_storage_lock(TB_STORAGE_LOCK_CONTAINER_CACHE)
#define CACHE_UNLOCK() tb_storage_unlock(TB_STORAGE_LOCK_CONTAINER_CACHE)
#define MEDIA_CACHED_SOURCES 4
#define MEDIA_CACHED_PARENTS 16
#define MEDIA_SOURCE_CACHE_BYTES (64u * 1024u * 1024u)

typedef struct MediaCacheEntry
{
    char *path;
    TbReadStream *stream;
    uint64_t used;
} MediaCacheEntry;

typedef struct MediaSourceCache
{
    char *host;
    TbFileIdentity identity;
    MediaCacheEntry entries[MEDIA_CACHED_PARENTS];
    TbReadBudget budget;
    uint64_t clock, used;
    int leased;
} MediaSourceCache;

static MediaSourceCache *media_sources[MEDIA_CACHED_SOURCES];
static uint64_t media_cache_clock;

/* Image volumes are mutable, so their files are never cached as sources. */
static int source_identity(const char *path, TbFileIdentity *identity)
{
    return !tb_image_path(path) && tb_host_identity(path, identity);
}

static _Thread_local struct
{
    MediaSourceCache *sources[MEDIA_CACHED_SOURCES], *current;
    size_t count;
    int active;
    TbMediaReadStats stats;
} media_session;

static void destroy_media_cache(MediaSourceCache *source)
{
    for (size_t i = 0; i < MEDIA_CACHED_PARENTS; i++)
    {
        free(source->entries[i].path);
        tb_stream_close(source->entries[i].stream);
    }
    free(source->host);
    free(source);
}

void tb_media_cache_peak(void)
{
    uint64_t bytes = 0;
    for (size_t i = 0; i < media_session.count; i++)
        bytes += media_session.sources[i]->budget.used;
    if (bytes > media_session.stats.peak_cache_bytes)
        media_session.stats.peak_cache_bytes = bytes;
}

void tb_media_session_end(void)
{
    CACHE_LOCK();
    for (size_t i = 0; i < media_session.count; i++)
        media_session.sources[i]->leased = 0;
    CACHE_UNLOCK();
    memset(&media_session, 0, sizeof media_session);
    tb_file_time_cache_reset();
}

void tb_media_session_begin(void)
{
    tb_media_session_end();
    media_session.active = 1;
}

TbMediaReadStats *tb_media_session_stats(void)
{
    return &media_session.stats;
}

TbReadBudget *tb_media_cache_budget(void)
{
    return media_session.current ? &media_session.current->budget : NULL;
}

TbMediaReadStats tb_media_read_stats(void)
{
    tb_media_cache_peak();
    return media_session.stats;
}

void tb_media_cache_select(const char *host)
{
    media_session.current = NULL;
    if (!media_session.active)
        return;
    TbFileIdentity identity;
    if (!source_identity(host, &identity))
        return;
    for (size_t i = 0; i < media_session.count; i++)
    {
        MediaSourceCache *source = media_session.sources[i];
        if (!strcmp(source->host, host) && !memcmp(&source->identity, &identity, sizeof identity))
        {
            media_session.current = source;
            return;
        }
    }
    /* Extra sources are read uncached; requests cannot pin an unbounded pool. */
    if (media_session.count == MEDIA_CACHED_SOURCES)
        return;
    CACHE_LOCK();
    MediaSourceCache *source = NULL;
    size_t oldest = MEDIA_CACHED_SOURCES;
    for (size_t i = 0; i < MEDIA_CACHED_SOURCES; i++)
    {
        MediaSourceCache *candidate = media_sources[i];
        if (candidate && !candidate->leased && !strcmp(candidate->host, host) &&
            !memcmp(&candidate->identity, &identity, sizeof identity))
        {
            source = candidate;
            break;
        }
        if ((!candidate || !candidate->leased) &&
            (oldest == MEDIA_CACHED_SOURCES || !candidate ||
             (media_sources[oldest] && candidate->used < media_sources[oldest]->used)))
            oldest = i;
    }
    if (!source && oldest != MEDIA_CACHED_SOURCES)
    {
        source = calloc(1, sizeof *source);
        if (source)
        {
            source->host = strdup(host);
            if (!source->host)
            {
                free(source);
                source = NULL;
            }
            else
            {
                source->identity = identity;
                source->budget.limit = MEDIA_SOURCE_CACHE_BYTES;
                if (media_sources[oldest])
                    destroy_media_cache(media_sources[oldest]);
                media_sources[oldest] = source;
            }
        }
    }
    if (source)
    {
        source->leased = 1;
        source->used = ++media_cache_clock;
        media_session.sources[media_session.count++] = source;
        media_session.current = source;
    }
    CACHE_UNLOCK();
    tb_media_cache_peak();
}

TbReadStream *tb_media_cache_lookup(const char *path, size_t length)
{
    MediaSourceCache *source = media_session.current;
    if (!source)
        return NULL;
    for (size_t i = 0; i < MEDIA_CACHED_PARENTS; i++)
    {
        MediaCacheEntry *entry = &source->entries[i];
        if (entry->path && strlen(entry->path) == length && !strncmp(entry->path, path, length))
        {
            entry->used = ++source->clock;
            media_session.stats.container_cache_hits++;
            return tb_stream_retain(entry->stream);
        }
    }
    return NULL;
}

void tb_media_cache_store(const char *path, size_t length, TbReadStream *stream)
{
    MediaSourceCache *source = media_session.current;
    if (!source || !stream)
        return;
    size_t oldest = 0;
    for (size_t i = 0; i < MEDIA_CACHED_PARENTS; i++)
    {
        if (source->entries[i].path && strlen(source->entries[i].path) == length &&
            !strncmp(source->entries[i].path, path, length))
        {
            oldest = i;
            break;
        }
        if (!source->entries[i].path || source->entries[i].used < source->entries[oldest].used)
            oldest = i;
    }
    char *name = malloc(length + 1);
    if (!name)
        return;
    memcpy(name, path, length);
    name[length] = 0;
    MediaCacheEntry *entry = &source->entries[oldest];
    tb_stream_retain(stream);
    free(entry->path);
    tb_stream_close(entry->stream);
    *entry = (MediaCacheEntry){name, stream, ++source->clock};
}
