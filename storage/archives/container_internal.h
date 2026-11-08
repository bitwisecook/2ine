/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared by the container dispatcher, its source cache, the media path codec and the
   per-format archive readers. */
#ifndef TB_STORAGE_CONTAINER_INTERNAL_H
#define TB_STORAGE_CONTAINER_INTERNAL_H
#include "storage/container.h"
#include "storage/read_stream.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

struct archive;

#define TB_MEDIA_PREFIX "/@2ine-media/"
/* Largest member any container may yield. */
#define TB_MEDIA_MEMBER_LIMIT (UINT64_C(2) * 1024 * 1024 * 1024)
/* A directory walk with more entries than this fails with EFBIG or EINVAL. */
#define TB_MEDIA_ENTRY_LIMIT 100000u
/* ARC, ARJ and zoo members are decoded whole in memory up to this many bytes. */
#define TB_LEGACY_ARCHIVE_LIMIT (16u * 1024u * 1024u)
/* Memory kept by one lazily decoded libarchive member. */
#define TB_MEDIA_CACHE_BYTES (1024u * 1024u)

/* How a walker's member is extracted. ARC, ARJ and zoo members add their format's
   compression method to the base value. */
typedef enum TbArchiveKind
{
    TB_ARCHIVE_LIBARCHIVE = 0,     /* input is the walking struct archive */
    TB_ARCHIVE_IBM_PACK = 1,       /* IBM PACK, Deark dskdcmps */
    TB_ARCHIVE_IBM_PACK2 = 2,      /* IBM PACK2, Deark FTCOMP */
    TB_ARCHIVE_DISK_IMAGE = 3,     /* a file in an ISO 9660 or FAT image */
    TB_ARCHIVE_STORED_SLICE = 4,   /* packed bytes are the member */
    TB_ARCHIVE_FILTERED_SLICE = 5, /* packed bytes are one libarchive-filtered stream */
    TB_ARCHIVE_ARC = 32,           /* + ARC method 1-9 */
    TB_ARCHIVE_ARJ = 64,           /* + ARJ method 0-4 */
    TB_ARCHIVE_ZOO = 96            /* + zoo method 0-2 */
} TbArchiveKind;

/* Called once per member. input is the walked TbReadStream, or the struct archive for
   TB_ARCHIVE_LIBARCHIVE; offset and packed locate the member's header or data as its
   kind requires. modified is INT64_MIN when absent. Returns 1 to continue, 2 to stop
   with success, or 0 to stop with errno set. Walkers return the stopping value, 1 at
   the end of the directory, or 0 with errno on a malformed container. */
typedef int (*TbArchiveVisit)(void *context, const char *name, uint64_t size, int directory,
                              void *input, uint64_t offset, uint64_t packed, TbArchiveKind kind,
                              int64_t modified);

static inline int tb_archive_fail(int error)
{
    errno = error;
    return 0;
}

/* container_path.c: decodes one hex-encoded path component; empty input yields "". */
char *tb_media_decode_part(const char *text, size_t length);
/* Returns an owned relative member name with "/" separators and no "." components,
   or NULL for "..", drive or stream syntax, or (unless strip_root) an absolute name. */
char *tb_media_sanitise_member(const char *name, int strip_root);

/* container_cache.c: request-scoped source and parent-container cache. */
void tb_media_cache_select(const char *host);
TbReadBudget *tb_media_cache_budget(void);
TbReadStream *tb_media_cache_lookup(const char *path, size_t length);
void tb_media_cache_store(const char *path, size_t length, TbReadStream *stream);
void tb_media_cache_peak(void);
TbMediaReadStats *tb_media_session_stats(void);

/* libarchive_reader.c */
typedef struct TbArchiveInput
{
    TbReadStream *file;
    uint64_t base, length, position;
    unsigned char buffer[65536];
} TbArchiveInput;

/* Creates a reader with every filter and format, including raw, and opens input.
   *archive is NULL when allocation failed; returns 1 when the open succeeded. */
int tb_libarchive_open(struct archive **archive, TbArchiveInput *input);
int tb_libarchive_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
/* Decodes the named member, or the only stream when raw, on demand. */
TbReadStream *tb_libarchive_member(TbReadStream *parent, const char *name, uint64_t size, int raw);

/* Format signatures at the start of a file, except as noted. */
#define TB_ARC_MARK 0x1a
#define TB_ARJ_SIGNATURE "\x60\xea"
#define TB_IBM_PACK_SIGNATURE "\xa5\x96"
#define TB_WARPIN_SIGNATURE "\x77\x04\x02\xbe" /* also after an executable stub */
#define TB_ZOO_SIGNATURE UINT32_C(0xfdc4a7dc)  /* little-endian, at TB_ZOO_SIGNATURE_AT */
#define TB_ZOO_SIGNATURE_AT 20

/* Format walkers and member decoders. */
int tb_ibm_pack_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
TbReadStream *tb_ibm_pack_member(TbReadStream *file, uint64_t offset, uint64_t packed,
                                 uint64_t size, TbArchiveKind kind);
int tb_warpin_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
int tb_arc_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
TbReadStream *tb_arc_member(TbReadStream *file, uint64_t offset, uint64_t packed, uint64_t size,
                            TbArchiveKind kind);
int tb_arj_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
/* An ARJ archive after an executable prefix; ENOTSUP when none is found. */
int tb_arj_sfx_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
TbReadStream *tb_arj_member(TbReadStream *file, uint64_t offset, uint64_t packed, uint64_t size,
                            TbArchiveKind kind);
int tb_zoo_walk(TbReadStream *file, TbArchiveVisit visit, void *context);
TbReadStream *tb_zoo_member(TbReadStream *file, uint64_t offset, uint64_t packed, uint64_t size,
                            TbArchiveKind kind);
/* ZIP members of a libarchive walk. The legacy-method and indexed-deflate readers set
   *handled when the central directory selects them, and then own the result. */
TbReadStream *tb_zip_legacy_member(TbReadStream *parent, struct archive *archive, const char *name,
                                   uint64_t size, int *handled);
TbReadStream *tb_zip_indexed_member(TbReadStream *parent, struct archive *archive, const char *name,
                                    uint64_t size, int *handled);
TbReadStream *tb_zip_stored_member(TbReadStream *parent, struct archive *archive, uint64_t size);

/* member_stream.c: the visitor that opens one named member, and the common end of the
   ARC and ARJ member decoders: frees output and returns NULL (errno EIO unless ENOTSUP)
   unless ok, otherwise wraps output as a memory stream. */
TbReadStream *tb_legacy_member_result(unsigned char *output, uint64_t size, int ok);

typedef struct TbArchiveExtract
{
    const char *name;
    TbReadStream *parent, *out;
} TbArchiveExtract;

int tb_archive_extract_visit(void *context, const char *name, uint64_t size, int directory,
                             void *input, uint64_t offset, uint64_t packed, TbArchiveKind kind,
                             int64_t modified);
#endif
