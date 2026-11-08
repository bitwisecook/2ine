/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "storage/read_stream.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define fseeko _fseeki64
#define ftello _ftelli64
#endif
struct TbReadStream
{
    uint64_t size, position;
    unsigned references;
    void *context;
    TbReadAt read;
    TbReadClose close;
    TbReadBudget *budget;
    size_t cache_bytes, index_bytes;
    void *index;
    TbReadClose index_close;
};

TbReadStream *tb_stream_create(uint64_t size, void *context, TbReadAt read, TbReadClose close)
{
    TbReadStream *stream = calloc(1, sizeof *stream);
    if (stream)
        *stream = (TbReadStream){
            .size = size, .references = 1, .context = context, .read = read, .close = close};
    return stream;
}

TbReadStream *tb_stream_retain(TbReadStream *stream)
{
    if (stream)
        stream->references++;
    return stream;
}

void tb_stream_close(TbReadStream *stream)
{
    if (stream && !--stream->references)
    {
        if (stream->index_close)
            stream->index_close(stream->index);
        if (stream->close)
            stream->close(stream->context);
        if (stream->budget)
            stream->budget->used -= stream->cache_bytes + stream->index_bytes;
        free(stream);
    }
}

void tb_stream_set_budget(TbReadStream *stream, TbReadBudget *budget)
{
    if (stream)
        stream->budget = budget;
}

void tb_stream_inherit_budget(TbReadStream *stream, TbReadStream *parent)
{
    tb_stream_set_budget(stream, parent->budget);
}

int tb_stream_cache_resize(TbReadStream *stream, size_t bytes)
{
    if (stream->budget)
    {
        TbReadBudget *budget = stream->budget;
        if (bytes > stream->cache_bytes &&
            bytes - stream->cache_bytes > budget->limit - budget->used)
        {
            errno = ENOMEM;
            return 0;
        }
        budget->used -= stream->cache_bytes;
        budget->used += bytes;
    }
    stream->cache_bytes = bytes;
    return 1;
}

size_t tb_stream_cache_size(TbReadStream *stream)
{
    return stream->cache_bytes;
}

void *tb_stream_index(TbReadStream *stream)
{
    return stream->index;
}

int tb_stream_set_index(TbReadStream *stream, void *index, size_t bytes, TbReadClose close)
{
    if (stream->index || (stream->budget && bytes > stream->budget->limit - stream->budget->used))
    {
        errno = ENOMEM;
        return 0;
    }
    stream->index = index;
    stream->index_bytes = bytes;
    stream->index_close = close;
    if (stream->budget)
        stream->budget->used += bytes;
    return 1;
}

int tb_stream_index_resize(TbReadStream *stream, size_t bytes)
{
    if (stream->budget)
    {
        TbReadBudget *budget = stream->budget;
        if (bytes > stream->index_bytes &&
            bytes - stream->index_bytes > budget->limit - budget->used)
        {
            errno = ENOMEM;
            return 0;
        }
        budget->used = budget->used - stream->index_bytes + bytes;
    }
    stream->index_bytes = bytes;
    return 1;
}

uint64_t tb_stream_size(const TbReadStream *stream)
{
    return stream->size;
}

int tb_stream_read_at(TbReadStream *stream, uint64_t offset, void *data, size_t size)
{
    if (offset > stream->size || size > stream->size - offset)
    {
        errno = EIO;
        return 0;
    }
    return !size || stream->read(stream->context, offset, data, size);
}

int tb_stream_seek(TbReadStream *stream, int64_t offset, int whence)
{
    uint64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? stream->position : stream->size;
    uint64_t distance = offset < 0 ? (uint64_t)(-(offset + 1)) + 1 : (uint64_t)offset;
    if ((whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) ||
        (offset < 0 ? distance > base : distance > UINT64_MAX - base))
    {
        errno = EINVAL;
        return -1;
    }
    stream->position = offset < 0 ? base - distance : base + distance;
    return 0;
}

uint64_t tb_stream_tell(const TbReadStream *stream)
{
    return stream->position;
}

size_t tb_stream_read(void *data, size_t size, size_t count, TbReadStream *stream)
{
    if (!size || count > SIZE_MAX / size || stream->position > stream->size)
        return 0;
    uint64_t available = stream->size - stream->position;
    size_t wanted = size * count;
    if (available < wanted)
        wanted = (size_t)available;
    if (!tb_stream_read_at(stream, stream->position, data, wanted))
        return 0;
    stream->position += wanted;
    return wanted / size;
}

typedef struct FileStream
{
    FILE *file;
    int owned;
} FileStream;

static int file_read(void *context, uint64_t offset, void *data, size_t size)
{
    FileStream *file = context;
    return offset <= INT64_MAX && !fseeko(file->file, (int64_t)offset, SEEK_SET) &&
           fread(data, 1, size, file->file) == size;
}

static void file_close(void *context)
{
    FileStream *file = context;
    if (file->owned)
        fclose(file->file);
    free(file);
}

TbReadStream *tb_stream_file(FILE *file, int owned)
{
    if (!file)
        return NULL;
    if (fseeko(file, 0, SEEK_END))
    {
        if (owned)
            fclose(file);
        return NULL;
    }
    int64_t size = ftello(file);
    FileStream *context = size < 0 ? NULL : calloc(1, sizeof *context);
    if (!context)
    {
        if (owned)
            fclose(file);
        return NULL;
    }
    *context = (FileStream){file, owned};
    TbReadStream *stream = tb_stream_create((uint64_t)size, context, file_read, file_close);
    if (!stream)
        file_close(context);
    return stream;
}

static int memory_read(void *context, uint64_t offset, void *data, size_t size)
{
    memcpy(data, (unsigned char *)context + offset, size);
    return 1;
}

TbReadStream *tb_stream_memory(unsigned char *data, size_t size)
{
    TbReadStream *stream = tb_stream_create(size, data, memory_read, free);
    if (!stream)
        free(data);
    return stream;
}

typedef struct SliceStream
{
    TbReadStream *parent;
    uint64_t offset;
} SliceStream;

static int slice_read(void *context, uint64_t offset, void *data, size_t size)
{
    SliceStream *slice = context;
    return tb_stream_read_at(slice->parent, slice->offset + offset, data, size);
}

static void slice_close(void *context)
{
    SliceStream *slice = context;
    tb_stream_close(slice->parent);
    free(slice);
}

TbReadStream *tb_stream_slice(TbReadStream *parent, uint64_t offset, uint64_t size)
{
    if (offset > tb_stream_size(parent) || size > tb_stream_size(parent) - offset)
    {
        errno = EIO;
        return NULL;
    }
    SliceStream *slice = malloc(sizeof *slice);
    if (!slice)
        return NULL;
    *slice = (SliceStream){tb_stream_retain(parent), offset};
    TbReadStream *stream = tb_stream_create(size, slice, slice_read, slice_close);
    tb_stream_inherit_budget(stream, parent);
    if (!stream)
        slice_close(slice);
    return stream;
}
