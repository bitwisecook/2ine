/* Public domain. No rights reserved. Wholly AI-authored. */
/* A copied zlib state owns its dictionary and bit buffer. Pending input is
   re-read by compressed offset, so snapshots never reference a reused buffer. */
#define ZIP_CHECKPOINT_SPAN (4u * 1024u * 1024u)
typedef struct ZipCheckpoint
{
    struct ZipCheckpoint *next;
    z_stream state;
    uint64_t input, output;
    uLong crc;
} ZipCheckpoint;
typedef struct ZipSeek
{
    TbReadStream *parent, *self;
    z_stream state;
    ZipCheckpoint *points;
    uint64_t start, packed, expected, input, decoded, checkpoint_high;
    uint32_t expected_crc;
    uLong crc;
    int initialized, ended, failed;
    size_t used;
    unsigned char input_buffer[65536], output[65536];
} ZipSeek;
typedef union ZipAllocation
{
    max_align_t alignment;
    size_t bytes;
} ZipAllocation;
static voidpf zip_allocate(voidpf opaque, uInt count, uInt width)
{
    ZipSeek *zip = opaque;
    if (width && count > (SIZE_MAX - sizeof(ZipAllocation)) / width)
        return NULL;
    size_t bytes = sizeof(ZipAllocation) + (size_t)count * width;
    size_t before = tb_stream_cache_size(zip->self);
    if (bytes > SIZE_MAX - before || !tb_stream_cache_resize(zip->self, before + bytes))
        return NULL;
    ZipAllocation *allocation = calloc(1, bytes);
    if (!allocation)
    {
        tb_stream_cache_resize(zip->self, before);
        return NULL;
    }
    allocation->bytes = bytes;
    media_cache_peak();
    return allocation + 1;
}
static void zip_release(voidpf opaque, voidpf address)
{
    if (!address)
        return;
    ZipSeek *zip = opaque;
    ZipAllocation *allocation = (ZipAllocation *)address - 1;
    tb_stream_cache_resize(zip->self, tb_stream_cache_size(zip->self) - allocation->bytes);
    free(allocation);
}
static int zip_restart(ZipSeek *zip, uint64_t offset)
{
    ZipCheckpoint *best = NULL;
    for (ZipCheckpoint *point = zip->points; point; point = point->next)
        if (point->output <= offset && (!best || point->output > best->output))
            best = point;
    if (zip->initialized)
        inflateEnd(&zip->state);
    memset(&zip->state, 0, sizeof zip->state);
    zip->initialized = 0;
    int result;
    if (best)
    {
        result = inflateCopy(&zip->state, &best->state);
        zip->input = best->input;
        zip->decoded = best->output;
        zip->crc = best->crc;
        media_session.stats.inflate_restores++;
    }
    else
    {
        zip->state.zalloc = zip_allocate;
        zip->state.zfree = zip_release;
        zip->state.opaque = zip;
        result = inflateInit2(&zip->state, -MAX_WBITS);
        zip->input = zip->decoded = 0;
        zip->crc = crc32(0, NULL, 0);
    }
    zip->used = 0;
    zip->ended = 0;
    zip->initialized = result == Z_OK;
    return zip->initialized || bad(result == Z_MEM_ERROR ? ENOMEM : EILSEQ);
}
static void zip_checkpoint(ZipSeek *zip)
{
    if (zip->decoded < zip->checkpoint_high + ZIP_CHECKPOINT_SPAN || zip->ended)
        return;
    zip->checkpoint_high = zip->decoded;
    ZipCheckpoint *point = zip_allocate(zip, 1, sizeof *point);
    if (!point)
        return; /* Memory pressure affects seek cost, never decoded bytes. */
    if (inflateCopy(&point->state, &zip->state) != Z_OK)
    {
        zip_release(zip, point);
        return;
    }
    point->input = zip->input - zip->state.avail_in;
    point->output = zip->decoded;
    point->crc = zip->crc;
    point->state.next_in = NULL;
    point->state.avail_in = 0;
    point->state.next_out = NULL;
    point->state.avail_out = 0;
    point->next = zip->points;
    zip->points = point;
    media_session.stats.inflate_checkpoints++;
}
static int zip_append(ZipSeek *zip)
{
    if (zip->ended)
        return bad(EIO);
    zip->state.next_out = zip->output;
    zip->state.avail_out = sizeof zip->output;
    while (zip->state.avail_out)
    {
        if (!zip->state.avail_in && zip->input < zip->packed)
        {
            size_t amount = zip->packed - zip->input > sizeof zip->input_buffer
                                ? sizeof zip->input_buffer
                                : (size_t)(zip->packed - zip->input);
            if (!tb_stream_read_at(zip->parent, zip->start + zip->input, zip->input_buffer, amount))
                return 0;
            zip->input += amount;
            zip->state.next_in = zip->input_buffer;
            zip->state.avail_in = (uInt)amount;
        }
        uInt before_in = zip->state.avail_in, before_out = zip->state.avail_out;
        int result = inflate(&zip->state, Z_NO_FLUSH);
        if (result == Z_STREAM_END)
        {
            zip->ended = 1;
            break;
        }
        if (result != Z_OK ||
            (before_in == zip->state.avail_in && before_out == zip->state.avail_out))
            return bad(result == Z_MEM_ERROR ? ENOMEM : EILSEQ);
    }
    zip->used = sizeof zip->output - zip->state.avail_out;
    if ((!zip->used && !zip->ended) || zip->used > zip->expected - zip->decoded)
        return bad(EILSEQ);
    zip->crc = crc32(zip->crc, zip->output, (uInt)zip->used);
    zip->decoded += zip->used;
    media_session.stats.decompressed_bytes += zip->used;
    if (zip->ended && (zip->decoded != zip->expected || zip->crc != zip->expected_crc ||
                       zip->input - zip->state.avail_in != zip->packed))
        return bad(EILSEQ);
    zip_checkpoint(zip);
    return 1;
}
static int zip_seek_read(void *opaque, uint64_t offset, void *data, size_t size)
{
    ZipSeek *zip = opaque;
    if (zip->failed)
        return bad(EILSEQ);
    for (ZipCheckpoint *point = zip->points; point; point = point->next)
        if (point->output > zip->decoded && point->output <= offset)
        {
            if (!zip_restart(zip, offset))
                goto failed;
            break;
        }
    if ((!zip->initialized || offset < zip->decoded - zip->used) && !zip_restart(zip, offset))
        goto failed;
    unsigned char *out = data;
    while (size)
    {
        while (offset >= zip->decoded)
            if (!zip_append(zip))
                goto failed;
        size_t within = (size_t)(offset - (zip->decoded - zip->used));
        size_t amount = zip->used - within;
        if (amount > size)
            amount = size;
        memcpy(out, zip->output + within, amount);
        out += amount;
        offset += amount;
        size -= amount;
    }
    /* The caller may stop exactly at the declared length before zlib has
       consumed its end marker. Validate it before accepting a complete file. */
    if (offset == zip->expected && !zip->ended && !zip_append(zip))
        goto failed;
    return 1;
failed:
    zip->failed = 1;
    return 0;
}
static void zip_seek_close(void *opaque)
{
    ZipSeek *zip = opaque;
    while (zip->points)
    {
        ZipCheckpoint *point = zip->points;
        zip->points = point->next;
        inflateEnd(&point->state);
        zip_release(zip, point);
    }
    if (zip->initialized)
        inflateEnd(&zip->state);
    tb_stream_close(zip->parent);
    free(zip);
}
static TbReadStream *indexed_zip_member(TbReadStream *parent, struct archive *archive,
                                        const char *name, uint64_t size, int *handled)
{
    *handled = 0;
    if ((archive_format(archive) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_ZIP)
        return NULL;
    LegacyZipMetadata metadata;
    if (!legacy_zip_metadata(parent, name, &metadata) || metadata.method != 8)
        return NULL; /* Other codecs/ZIP64 retain the library reader. */
    *handled = 1;
    unsigned char header[30];
    if (size != metadata.size || (metadata.flags & 0x41) ||
        !tb_stream_read_at(parent, metadata.position, header, sizeof header) ||
        memcmp(header, "PK\3\4", 4) || u16(header + 8) != 8 ||
        ((u16(header + 6) ^ metadata.flags) & 0x4f) ||
        (!(metadata.flags & 8) &&
         (u32(header + 18) != metadata.packed || u32(header + 22) != metadata.size ||
          u32(header + 14) != metadata.crc)))
    {
        bad(EILSEQ);
        return NULL;
    }
    uint64_t start = metadata.position + sizeof header + u16(header + 26) + u16(header + 28);
    if (start > tb_stream_size(parent) || metadata.packed > tb_stream_size(parent) - start)
    {
        bad(EILSEQ);
        return NULL;
    }
    ZipSeek *zip = calloc(1, sizeof *zip);
    if (!zip)
        return NULL;
    zip->parent = tb_stream_retain(parent);
    zip->start = start;
    zip->packed = metadata.packed;
    zip->expected = size;
    zip->expected_crc = metadata.crc;
    TbReadStream *stream = tb_stream_create(size, zip, zip_seek_read, zip_seek_close);
    if (!stream)
    {
        zip_seek_close(zip);
        return NULL;
    }
    zip->self = stream;
    tb_stream_inherit_budget(stream, parent);
    if (!tb_stream_cache_resize(stream, sizeof *zip))
    {
        tb_stream_close(stream);
        return NULL;
    }
    if (!size && !zip_seek_read(zip, 0, NULL, 0))
    {
        tb_stream_close(stream);
        return NULL;
    }
    return stream;
}
