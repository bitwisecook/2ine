/* Public domain. No rights reserved. Wholly AI-authored. */
/* Bounded, per-request memory and bitstream adapter for Deark's FTCOMP codec. */
#ifndef TB_PACK_COMPAT_H
#define TB_PACK_COMPAT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <zlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#define I64_FMT PRId64
#define de_gnuc_attribute(x)
#define de_vsnprintf vsnprintf
#define de_snprintf snprintf
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t i64;
typedef unsigned UI;

typedef struct Allocation
{
    struct Allocation *next;
} Allocation;

typedef struct deark
{
    jmp_buf escape;
    Allocation *allocations;
    size_t allocated, work;
    int debug_level;
    void *persistent_item[6];
} deark;

typedef struct dbuf
{
    deark *c;
    u8 *data;
    i64 len, capacity;
} dbuf;

#define TB_PACK_LIMIT (16u * 1024u * 1024u)
#define de_dbg(...) ((void)0)
#define de_dbg2(...) ((void)0)
#define de_min_int(a, b) ((a) < (b) ? (a) : (b))
#define de_max_int(a, b) ((a) > (b) ? (a) : (b))
#define de_memcpy memcpy
#define de_memmove memmove
#define de_zeromem(p, n) memset(p, 0, n)

static void pack_work(deark *c)
{
    if (++c->work > 100000000u)
        longjmp(c->escape, 1);
}

static void *de_malloc(deark *c, i64 size)
{
    if (size < 0 || (u64)size > TB_PACK_LIMIT || c->allocated + (u64)size > 64u * 1024u * 1024u)
        longjmp(c->escape, 1);
    Allocation *a = calloc(1, sizeof *a + (size_t)size);
    if (!a)
        longjmp(c->escape, 1);
    a->next = c->allocations;
    c->allocations = a;
    c->allocated += (size_t)size;
    return a + 1;
}

/* All codec allocations are released together, including on a malformed stream. */
#define de_free(c, p) ((void)(p))
#define de_internal_err_fatal(c, ...) longjmp((c)->escape, 1)

static dbuf *dbuf_create_membuf(deark *c, i64 initial, int unused)
{
    dbuf *b = de_malloc(c, sizeof *b);
    b->c = c;
    if (initial)
    {
        b->data = de_malloc(c, initial);
        b->capacity = initial;
    }
    return b;
}

static void dbuf_reserve(dbuf *b, i64 length)
{
    if (length < 0 || length > TB_PACK_LIMIT)
        longjmp(b->c->escape, 1);
    if (length > b->capacity)
    {
        i64 size = de_max_int(length, de_max_int(256, b->capacity * 2));
        u8 *data = de_malloc(b->c, size);
        if (b->len)
            memcpy(data, b->data, (size_t)b->len);
        b->data = data;
        b->capacity = size;
    }
}

static void dbuf_write(dbuf *b, const void *p, i64 n)
{
    pack_work(b->c);
    dbuf_reserve(b, b->len + n);
    if (n)
        memcpy(b->data + b->len, p, (size_t)n);
    b->len += n;
}

static void dbuf_writebyte(dbuf *b, u8 v)
{
    dbuf_write(b, &v, 1);
}

static void dbuf_writeu16le(dbuf *b, i64 v)
{
    u8 p[2] = {(u8)v, (u8)(v >> 8)};
    dbuf_write(b, p, 2);
}

static u8 dbuf_getbyte(dbuf *b, i64 p)
{
    pack_work(b->c);
    return p >= 0 && p < b->len ? b->data[p] : 0;
}

static unsigned dbuf_getu16le(dbuf *b, i64 p)
{
    return dbuf_getbyte(b, p) | (unsigned)dbuf_getbyte(b, p + 1) << 8;
}

static u8 dbuf_getbyte_p(dbuf *b, i64 *p)
{
    u8 v = dbuf_getbyte(b, *p);
    (*p)++;
    return v;
}

static unsigned dbuf_getu16le_p(dbuf *b, i64 *p)
{
    unsigned v = dbuf_getu16le(b, *p);
    *p += 2;
    return v;
}

static void dbuf_read(dbuf *b, void *out, i64 p, i64 n)
{
    u8 *v = out;
    for (i64 i = 0; i < n; i++)
        v[i] = dbuf_getbyte(b, p + i);
}

static void dbuf_copy(dbuf *b, i64 p, i64 n, dbuf *out)
{
    for (i64 i = 0; i < n; i++)
        dbuf_writebyte(out, dbuf_getbyte(b, p + i));
}

static void dbuf_truncate(dbuf *b, i64 n)
{
    dbuf_reserve(b, n);
    if (n > b->len)
        memset(b->data + b->len, 0, (size_t)(n - b->len));
    b->len = n;
}

static void dbuf_write_run(dbuf *b, u8 v, i64 n)
{
    if (n < 0 || n > TB_PACK_LIMIT)
        longjmp(b->c->escape, 1);
    for (i64 i = 0; i < n; i++)
        dbuf_writebyte(b, v);
}

static int dbuf_search_byte(dbuf *b, u8 v, i64 p, i64 n, i64 *found)
{
    *found = p + n;
    for (i64 i = 0; i < n; i++)
        if (dbuf_getbyte(b, p + i) == v)
        {
            *found = p + i;
            return 1;
        }
    return 0;
}

#define dbuf_get_length(b) ((b)->len)
#define dbuf_close(b) ((void)0)
#define dbuf_flush(b) ((void)0)
#define dbuf_enable_wbuffer(b) ((void)0)
#define dbuf_empty(b) ((b)->len = 0)

struct de_fourcc
{
    u32 id;
};

static void dbuf_read_fourcc(dbuf *b, i64 p, struct de_fourcc *f, int n, int unused)
{
    f->id = 0;
    for (int i = 0; i < 4; i++)
        f->id = (f->id << 8) | dbuf_getbyte(b, p + i);
}

static int de_decompress_zlib_mem2dbuf(deark *c, const u8 *p, i64 n, dbuf *out, i64 expected)
{
    dbuf_reserve(out, expected);
    uLongf size = (uLongf)expected;
    int rc = uncompress(out->data, &size, p, (uLong)n);
    out->len = (i64)size;
    return rc == Z_OK;
}

struct de_dfilter_in_params
{
    dbuf *f;
    i64 pos, len;
};

struct de_dfilter_out_params
{
    dbuf *f;
    i64 expected_len;
};

struct de_dfilter_results
{
    int errcode, bytes_consumed_valid;
    i64 bytes_consumed;
};

#define de_dfilter_set_errorf(c, r, ...) ((r)->errcode = 1)
#define de_dfilter_set_generic_error(c, r, ...) ((r)->errcode = 1)

struct de_bitreader
{
    dbuf *f;
    i64 curpos, endpos;

    struct
    {
        unsigned nbits_in_bitbuf;
        u64 bits;
    } bbll;
};

static u64 de_bitreader_getbits(struct de_bitreader *b, unsigned n)
{
    while (b->bbll.nbits_in_bitbuf < n)
    {
        b->bbll.bits = (b->bbll.bits << 8) | dbuf_getbyte(b->f, b->curpos++);
        b->bbll.nbits_in_bitbuf += 8;
    }
    b->bbll.nbits_in_bitbuf -= n;
    return (b->bbll.bits >> b->bbll.nbits_in_bitbuf) & (((u64)1 << n) - 1);
}

#define FMTUTIL_HUFFMAN_MAX_CODE_LENGTH 64
#define fmtutil_huffman_valtype unsigned

struct PackHuffmanNode
{
    int child[2];
    int leaf;
    unsigned value;
};

struct PackHuffmanBook
{
    struct PackHuffmanNode node[1024];
    unsigned count;
    deark *c;
};

struct PackHuffmanCursor
{
    unsigned at;
};

struct fmtutil_huffman_decoder
{
    struct PackHuffmanBook *bk;
    struct PackHuffmanCursor *cursor;
};

static struct fmtutil_huffman_decoder *fmtutil_huffman_create_decoder(deark *c, unsigned leaves,
                                                                      int unused)
{
    struct fmtutil_huffman_decoder *h = de_malloc(c, sizeof *h);
    h->bk = de_malloc(c, sizeof *h->bk);
    h->cursor = de_malloc(c, sizeof *h->cursor);
    h->bk->count = 1;
    h->bk->c = c;
    return h;
}

#define fmtutil_huffman_destroy_decoder(c, h) ((void)0)
#define fmtutil_huffman_reset_cursor(p) ((p)->at = 0)

static int fmtutil_huffman_add_code(deark *c, struct PackHuffmanBook *b, u64 code, unsigned bits,
                                    unsigned value)
{
    unsigned at = 0;
    if (!bits || bits > 64)
        return 0;
    for (unsigned i = bits; i > 0; i--)
    {
        unsigned bit = (unsigned)((code >> (i - 1)) & 1);
        if (b->node[at].leaf)
            return 0;
        if (!b->node[at].child[bit])
        {
            if (b->count >= 1024)
                return 0;
            b->node[at].child[bit] = (int)b->count++;
        }
        at = (unsigned)b->node[at].child[bit];
    }
    if (b->node[at].leaf || b->node[at].child[0] || b->node[at].child[1])
        return 0;
    b->node[at].leaf = 1;
    b->node[at].value = value;
    return 1;
}

static int fmtutil_huffman_decode_bit(struct PackHuffmanBook *b, struct PackHuffmanCursor *p,
                                      unsigned bit, unsigned *value)
{
    pack_work(b->c);
    int next = b->node[p->at].child[bit & 1];
    if (!next)
        return 0;
    p->at = (unsigned)next;
    if (b->node[p->at].leaf)
    {
        *value = b->node[p->at].value;
        return 1;
    }
    return 2;
}

struct de_lz77buffer
{
    deark *c;
    u8 *buf;
    unsigned size, curpos;
    void *userdata;
    void (*writebyte_cb)(struct de_lz77buffer *, u8);
};

static struct de_lz77buffer *de_lz77buffer_create(deark *c, unsigned n)
{
    struct de_lz77buffer *b = de_malloc(c, sizeof *b);
    b->buf = de_malloc(c, n);
    b->size = n;
    b->c = c;
    return b;
}

#define de_lz77buffer_destroy(c, b) ((void)0)
#define de_lz77buffer_set_curpos(b, p) ((b)->curpos = (unsigned)(p))

static void de_lz77buffer_add_literal_byte(struct de_lz77buffer *b, u8 v)
{
    pack_work(b->c);
    b->buf[b->curpos++ & (b->size - 1)] = v;
    b->writebyte_cb(b, v);
}

static void de_lz77buffer_copy_from_hist(struct de_lz77buffer *b, unsigned p, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        de_lz77buffer_add_literal_byte(b, b->buf[(p + i) & (b->size - 1)]);
}
#endif
