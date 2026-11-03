/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_READ_STREAM_H
#define TB_READ_STREAM_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct TbReadStream TbReadStream;
typedef int (*TbReadAt)(void *, uint64_t, void *, size_t);
typedef void (*TbReadClose)(void *);
TbReadStream *tb_stream_create(uint64_t size, void *context, TbReadAt read, TbReadClose close);
TbReadStream *tb_stream_file(FILE *file, int owned);
TbReadStream *tb_stream_memory(unsigned char *data, size_t size);
TbReadStream *tb_stream_slice(TbReadStream *parent, uint64_t offset, uint64_t size);
TbReadStream *tb_stream_retain(TbReadStream *stream);
void tb_stream_close(TbReadStream *stream);
uint64_t tb_stream_size(const TbReadStream *stream);
int tb_stream_read_at(TbReadStream *stream, uint64_t offset, void *data, size_t size);
int tb_stream_seek(TbReadStream *stream, int64_t offset, int whence);
uint64_t tb_stream_tell(const TbReadStream *stream);
size_t tb_stream_read(void *data, size_t size, size_t count, TbReadStream *stream);
#endif
