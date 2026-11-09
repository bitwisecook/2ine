/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metafile DCs: CreateMetaFile and CloseMetaFile, and the hooks through which drawing calls record
   themselves. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"
#include "os2/include/private/pmgrep.h"

/* A recording DC owns an ordinary checked DC plus an independent WMF stream.
   The original PMWINX likewise separates metafile records from its HPS wrapper. */
typedef struct Open32Recorder
{
    ULONG dc, objects, maximum;
    size_t size, capacity;
    uint8 *data;
    char *name;
    BOOL failed;
    struct Open32Recorder *next;
} Open32Recorder;

static _Thread_local Open32Recorder *recorders;

static Open32Recorder *recorder(ULONG dc)
{
    for (Open32Recorder *r = recorders; r; r = r->next)
        if (r->dc == dc)
            return r;
    return NULL;
}

static BOOL append_record(Open32Recorder *r, USHORT function, const void *data, size_t bytes)
{
    size_t padded = (bytes + 1) & ~(size_t)1, total = padded + 6;
    if (r->failed || total > 64u * 1024 * 1024 - r->size)
    {
        r->failed = TRUE;
        return FALSE;
    }
    if (r->capacity < r->size + total)
    {
        size_t next = r->capacity ? r->capacity * 2 : 256;
        while (next < r->size + total)
            next *= 2;
        uint8 *more = realloc(r->data, next);
        if (!more)
        {
            r->failed = TRUE;
            return FALSE;
        }
        r->data = more;
        r->capacity = next;
    }
    uint8 *p = r->data + r->size;
    lx_guest_store_u32(p, (ULONG)(total / 2));
    lx_guest_store_u16(p + 4, function);
    if (bytes)
        memcpy(p + 6, data, bytes);
    if (bytes & 1)
        p[6 + bytes] = 0;
    r->size += total;
    if (total / 2 > r->maximum)
        r->maximum = (ULONG)(total / 2);
    return TRUE;
}

static void select_recorded(Open32Recorder *r, USHORT function, ULONG index)
{
    uint8 word[2];
    lx_guest_store_u16(word, (uint16)index);
    append_record(r, function, word, 2);
}

void open32MetafileWords(ULONG dc, USHORT function, size_t count, const LONG *words)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    uint8 parameters[64];
    if (count > 32)
    {
        r->failed = TRUE;
        return;
    }
    for (size_t i = 0; i < count; i++)
    {
        if (words[i] < INT16_MIN || words[i] > INT16_MAX)
        {
            r->failed = TRUE;
            return;
        }
        lx_guest_store_u16(parameters + i * 2, (uint16)words[count - 1 - i]);
    }
    append_record(r, function, parameters, count * 2);
}

void open32MetafilePoints(ULONG dc, USHORT function, PVOID input, LONG count)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (count < 0 || count > 65535)
    {
        r->failed = TRUE;
        return;
    }
    size_t bytes = 2 + (size_t)count * 4;
    uint8 *p = malloc(bytes);
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, (uint16)count);
    for (LONG i = 0; i < count; i++)
        for (unsigned k = 0; k < 2; k++)
        {
            LONG value = (LONG)lx_guest_load_u32((uint8 *)input + i * 8 + k * 4);
            if (value < INT16_MIN || value > INT16_MAX)
                r->failed = TRUE;
            lx_guest_store_u16(p + 2 + i * 4 + k * 2, (uint16)value);
        }
    if (!r->failed)
        append_record(r, function, p, bytes);
    free(p);
}

void open32MetafileObject(ULONG dc, ULONG object)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    uint8 source[60], wire[50];
    size_t bytes = 0;
    USHORT type = 0;
    ULONG kind = GetObjectType(object);
    if (kind == 1 && open32PenObject(object, sizeof source, source))
    {
        type = 0x2fa;
        bytes = 10;
        lx_guest_store_u16(wire, (uint16)lx_guest_load_u32(source));
        lx_guest_store_u16(wire + 2, (uint16)lx_guest_load_u32(source + 4));
        lx_guest_store_u16(wire + 4, 0);
        memcpy(wire + 6, source + 12, 4);
    }
    else if (kind == 2 && open32BrushObject(object, sizeof source, source) &&
             lx_guest_load_u32(source) == 3)
    {
        /* Pattern tiles travel as META_DIBCREATEPATTERNBRUSH: style 3 stays monochrome. */
        size_t head = 0, total = 0;
        BOOL monochrome = FALSE;
        uint8 *dib = open32BrushDib(object, &head, &total, &monochrome);
        uint8 *record = dib ? malloc(total + 4) : NULL;
        if (!record || r->objects >= 65535)
            r->failed = TRUE;
        else
        {
            lx_guest_store_u16(record, monochrome ? 3 : 5);
            lx_guest_store_u16(record + 2, 0);
            memcpy(record + 4, dib, total);
            if (append_record(r, 0x142, record, total + 4))
                select_recorded(r, 0x12d, r->objects++);
        }
        free(record);
        free(dib);
        return;
    }
    else if (kind == 2 && open32BrushObject(object, sizeof source, source))
    {
        type = 0x2fc;
        bytes = 8;
        lx_guest_store_u16(wire, (uint16)lx_guest_load_u32(source));
        memcpy(wire + 2, source + 4, 4);
        lx_guest_store_u16(wire + 6, (uint16)lx_guest_load_u32(source + 8));
    }
    else if (kind == 6 && open32FontObject(object, sizeof source, source))
    {
        type = 0x2fb;
        bytes = 50;
        for (unsigned i = 0; i < 5; i++)
            lx_guest_store_u16(wire + i * 2, (uint16)lx_guest_load_u32(source + i * 4));
        memcpy(wire + 10, source + 20, 40);
    }
    else
    {
        r->failed = TRUE;
        return;
    }
    if (r->objects >= 65535 || !append_record(r, type, wire, bytes))
    {
        r->failed = TRUE;
        return;
    }
    uint8 index[2];
    lx_guest_store_u16(index, (uint16)r->objects++);
    append_record(r, 0x12d, index, 2);
}

void open32MetafileExtText(ULONG dc, LONG x, LONG y, ULONG flags, PVOID rectangle, PCHAR text,
                           ULONG count, PLONG advances)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    ULONG options = flags & 6;
    BOOL valid =
        count <= 65535 && x >= INT16_MIN && x <= INT16_MAX && y >= INT16_MIN && y <= INT16_MAX;
    size_t padded = (count + 1) & ~(size_t)1, text_at = options ? 16 : 8;
    size_t bytes = text_at + padded + (advances ? (size_t)count * 2 : 0);
    uint8 *p = valid ? calloc(bytes, 1) : NULL;
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, (uint16)y);
    lx_guest_store_u16(p + 2, (uint16)x);
    lx_guest_store_u16(p + 4, (uint16)count);
    lx_guest_store_u16(p + 6, (uint16)options);
    for (unsigned i = 0; options && i < 4; i++)
    {
        LONG value = (LONG)lx_guest_load_u32((uint8 *)rectangle + i * 4);
        valid = valid && value >= INT16_MIN && value <= INT16_MAX;
        lx_guest_store_u16(p + 8 + i * 2, (uint16)value);
    }
    if (count)
        memcpy(p + text_at, text, count);
    for (ULONG i = 0; advances && i < count; i++)
    {
        LONG value = (LONG)lx_guest_load_u32((uint8 *)advances + i * 4);
        valid = valid && value >= INT16_MIN && value <= INT16_MAX;
        lx_guest_store_u16(p + text_at + padded + i * 2, (uint16)value);
    }
    if (valid)
        append_record(r, 0xa32, p, bytes);
    else
        r->failed = TRUE;
    free(p);
}

/* Each region is one band per rectangle. A NULL region selects no clipping. */
void open32MetafileClip(ULONG dc, ULONG region)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (!region)
    {
        select_recorded(r, 0x12c, 0x8000);
        return;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(region, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        r->failed = TRUE;
        return;
    }
    PmRegion *source = borrow.resource;
    size_t bytes = 22 + source->count * 12;
    uint8 *p = r->objects < 65535 && source->count <= 65535 ? calloc(bytes, 1) : NULL;
    BOOL valid = p != NULL;
    if (p)
    {
        lx_guest_store_u16(p + 2, 6);
        lx_guest_store_u16(p + 8, (uint16)bytes);
        lx_guest_store_u16(p + 10, (uint16)source->count);
        lx_guest_store_u16(p + 12, 12);
        for (size_t i = 0; i < source->count; i++)
        {
            const RECTL *q = &source->rects[i];
            uint8 *scan = p + 22 + i * 12;
            LONG values[] = {2, q->yBottom, q->yTop, q->xLeft, q->xRight, 2};
            for (unsigned k = 0; k < 6; k++)
            {
                valid = valid && values[k] >= INT16_MIN && values[k] <= INT16_MAX;
                lx_guest_store_u16(scan + k * 2, (uint16)values[k]);
            }
        }
    }
    lx_handle_release(&borrow);
    if (valid && append_record(r, 0x6ff, p, bytes))
        select_recorded(r, 0x12c, r->objects++);
    else
        r->failed = TRUE;
    free(p);
}

void open32MetafilePalette(ULONG dc, ULONG palette)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    ULONG n = GetPaletteEntries(palette, 0, 0, NULL);
    uint8 *p = n && n <= 65535 && r->objects < 65535 ? malloc(4 + (size_t)n * 4) : NULL;
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, 0x300);
    lx_guest_store_u16(p + 2, (uint16)n);
    GetPaletteEntries(palette, 0, n, p + 4);
    if (append_record(r, 0xf7, p, 4 + (size_t)n * 4))
        select_recorded(r, 0x234, r->objects++);
    free(p);
}

void open32MetafileRealize(ULONG dc)
{
    Open32Recorder *r = recorder(dc);
    if (r)
        append_record(r, 0x35, NULL, 0);
}

void open32MetafilePaletteRecord(ULONG dc, USHORT function, ULONG first, ULONG count, PVOID entries)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (function == 0x139)
    {
        select_recorded(r, 0x139, first);
        return;
    }
    uint8 *p = first <= 65535 && count <= 65535 ? malloc(4 + (size_t)count * 4) : NULL;
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, (uint16)first);
    lx_guest_store_u16(p + 2, (uint16)count);
    if (count)
        memcpy(p + 4, entries, (size_t)count * 4);
    append_record(r, function, p, 4 + (size_t)count * 4);
    free(p);
}

void open32MetafileDCCleanup(ULONG dc)
{
    Open32Recorder **link = &recorders;
    while (*link && (*link)->dc != dc)
        link = &(*link)->next;
    if (*link)
    {
        Open32Recorder *r = *link;
        *link = r->next;
        free(r->data);
        free(r->name);
        free(r);
    }
}

ULONG CreateMetaFile(PSZ name)
{
    ULONG dc = CreateCompatibleDC(0);
    if (!dc)
        return 0;
    Open32Recorder *r = calloc(1, sizeof *r);
    if (!r)
    {
        DeleteDC(dc);
        open32SetError(8);
        return 0;
    }
    r->dc = dc;
    if (name)
        r->name = strdup(name);
    if (name && !r->name)
    {
        free(r);
        DeleteDC(dc);
        open32SetError(8);
        return 0;
    }
    r->size = 18;
    r->next = recorders;
    recorders = r;
    open32MetafileObject(dc, GetStockObject(0));
    open32MetafileObject(dc, GetStockObject(7));
    open32MetafileObject(dc, GetStockObject(13));
    return dc;
}

ULONG CloseMetaFile(ULONG dc)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
    {
        open32SetError(6);
        return 0;
    }
    ULONG result = 0;
    if (append_record(r, 0, NULL, 0))
    {
        lx_guest_store_u16(r->data, 1);
        lx_guest_store_u16(r->data + 2, 9);
        lx_guest_store_u16(r->data + 4, 0x300);
        lx_guest_store_u32(r->data + 6, (ULONG)(r->size / 2));
        lx_guest_store_u16(r->data + 10, (uint16)r->objects);
        lx_guest_store_u32(r->data + 12, r->maximum);
        lx_guest_store_u16(r->data + 16, 0);
        if (!r->name || metafile_save(r->name, r->data, (ULONG)r->size))
            result = SetMetaFileBitsEx((ULONG)r->size, r->data);
    }
    else
        open32SetError(8);
    open32MetafileDCCleanup(dc);
    DeleteDC(dc);
    return result;
}

void open32MetafileColor(ULONG dc, USHORT function, ULONG color)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    uint8 bytes[4];
    lx_guest_store_u32(bytes, color);
    append_record(r, function, bytes, 4);
}

BOOL open32IsMetafile(ULONG dc)
{
    return recorder(dc) != NULL;
}

void open32MetafileDib(ULONG dc, LONG x, LONG y, LONG w, LONG h, LONG sx, LONG sy, LONG sw, LONG sh,
                       PVOID bits, PVOID information, ULONG usage, ULONG operation)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    size_t ib, pb;
    if (!open32DibBuffers(information, 0, UINT32_MAX, usage, &ib, &pb) ||
        ib + pb > 64u * 1024 * 1024 - 22)
    {
        r->failed = TRUE;
        return;
    }
    LONG coords[] = {sh, sw, sy, sx, h, w, y, x};
    for (unsigned i = 0; i < 8; i++)
        if (coords[i] < INT16_MIN || coords[i] > INT16_MAX)
        {
            r->failed = TRUE;
            return;
        }
    uint8 *p = malloc(22 + ib + pb);
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u32(p, operation);
    lx_guest_store_u16(p + 4, (uint16)usage);
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u16(p + 6 + i * 2, (uint16)coords[i]);
    memcpy(p + 22, information, ib);
    memcpy(p + 22 + ib, bits, pb);
    append_record(r, 0xf43, p, 22 + ib + pb);
    free(p);
}

void open32MetafilePatBlt(ULONG dc, LONG x, LONG y, LONG w, LONG h, ULONG operation)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    LONG values[] = {h, w, y, x};
    uint8 p[12];
    lx_guest_store_u32(p, operation);
    for (unsigned i = 0; i < 4; i++)
    {
        if (values[i] < INT16_MIN || values[i] > INT16_MAX)
        {
            r->failed = TRUE;
            return;
        }
        lx_guest_store_u16(p + 4 + i * 2, (uint16)values[i]);
    }
    append_record(r, 0x61d, p, 12);
}

BOOL open32MetafilePolyPolygon(ULONG dc, PVOID points, PVOID counts, LONG polygons)
{
    Open32Recorder *r = recorder(dc);
    if (!r || polygons < 1 || polygons > 65535)
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG total = 0;
    for (LONG i = 0; i < polygons; i++)
    {
        ULONG n = lx_guest_load_u32((uint8 *)counts + i * 4);
        if (n < 2 || n > 65535 - total)
        {
            open32SetError(87);
            return FALSE;
        }
        total += n;
    }
    size_t bytes = 2 + (size_t)polygons * 2 + (size_t)total * 4;
    uint8 *p = malloc(bytes);
    if (!p)
    {
        open32SetError(8);
        return FALSE;
    }
    lx_guest_store_u16(p, (uint16)polygons);
    for (LONG i = 0; i < polygons; i++)
        lx_guest_store_u16(p + 2 + i * 2, (uint16)lx_guest_load_u32((uint8 *)counts + i * 4));
    BOOL ok = TRUE;
    for (ULONG i = 0; i < total; i++)
        for (unsigned k = 0; k < 2; k++)
        {
            LONG value = (LONG)lx_guest_load_u32((uint8 *)points + i * 8 + k * 4);
            if (value < INT16_MIN || value > INT16_MAX)
            {
                ok = FALSE;
                break;
            }
            lx_guest_store_u16(p + 2 + polygons * 2 + i * 4 + k * 2, (uint16)value);
        }
    if (ok)
        ok = append_record(r, 0x538, p, bytes);
    free(p);
    if (!ok)
        open32SetError(87);
    return ok;
}

void open32MetafilePixel(ULONG dc, LONG x, LONG y, ULONG color)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX)
    {
        r->failed = TRUE;
        return;
    }
    uint8 p[8];
    lx_guest_store_u32(p, color);
    lx_guest_store_u16(p + 4, (uint16)y);
    lx_guest_store_u16(p + 6, (uint16)x);
    append_record(r, 0x41f, p, 8);
}
