/* Public domain. No rights reserved. Wholly AI-authored. */
/* Metafile DCs: CreateMetaFile and CloseMetaFile, and the hooks through which drawing calls record
   themselves. */
#include "os2/include/private/export.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include "os2/ps/awe/gdi/metafiles/metafile_objects.h"
#include "os2/include/private/pmgrep.h"

enum
{
    /* Initial recording buffer; it doubles as records are added. */
    RECORDING_INITIAL_CAPACITY = 256,
    /* Most parameter words open32MetafileWords records. */
    RECORDED_WORDS_MAX = 32,
    /* A recorded clip region holds one scan per rectangle: count, top, bottom, left,
       right and count again, six words. */
    RECORDED_SCAN_SIZE = 12,
    /* META_STRETCHDIB: raster operation, usage and eight coordinates before the DIB. */
    STRETCHDIB_HEAD = 22,
    /* palVersion of a recorded LOGPALETTE. */
    LOGPALETTE_VERSION = 0x300
};

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
    size_t padded = (bytes + 1) & ~(size_t)1, total = padded + WIN32_METARECORD_PARAMETERS;
    if (r->failed || total > METAFILE_SIZE_MAX - r->size)
    {
        r->failed = TRUE;
        return FALSE;
    }
    if (r->capacity < r->size + total)
    {
        size_t next = r->capacity ? r->capacity * 2 : RECORDING_INITIAL_CAPACITY;
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
    lx_guest_store_u32(p + WIN32_METARECORD_WORDS, (ULONG)(total / 2));
    lx_guest_store_u16(p + WIN32_METARECORD_FUNCTION, function);
    if (bytes)
        memcpy(p + WIN32_METARECORD_PARAMETERS, data, bytes);
    if (bytes & 1)
        p[WIN32_METARECORD_PARAMETERS + bytes] = 0;
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
    uint8 parameters[RECORDED_WORDS_MAX * 2];
    if (count > RECORDED_WORDS_MAX)
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
    if (count < 0 || count > UINT16_MAX)
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
    uint8 source[WIN32_LOGFONT_SIZE], wire[WIN32_LOGFONT16_SIZE];
    size_t bytes = 0;
    USHORT type = 0;
    ULONG kind = GetObjectType(object);
    if (kind == WIN32_OBJ_PEN && open32PenObject(object, sizeof source, source))
    {
        /* LOGPEN16: style, width x and y as words, then the colour. */
        type = WIN32_META_CREATEPENINDIRECT;
        bytes = 10;
        lx_guest_store_u16(wire, (uint16)lx_guest_load_u32(source + WIN32_LOGPEN_STYLE));
        lx_guest_store_u16(wire + 2, (uint16)lx_guest_load_u32(source + WIN32_LOGPEN_WIDTH));
        lx_guest_store_u16(wire + 4, 0);
        memcpy(wire + 6, source + WIN32_LOGPEN_COLOR, 4);
    }
    else if (kind == WIN32_OBJ_BRUSH && open32BrushObject(object, sizeof source, source) &&
             lx_guest_load_u32(source + WIN32_LOGBRUSH_STYLE) == BS_PATTERN)
    {
        /* Pattern tiles travel as META_DIBCREATEPATTERNBRUSH: BS_PATTERN stays monochrome. */
        size_t head = 0, total = 0;
        BOOL monochrome = FALSE;
        uint8 *dib = open32BrushDib(object, &head, &total, &monochrome);
        uint8 *record = dib ? malloc(total + 4) : NULL;
        if (!record || r->objects >= UINT16_MAX)
            r->failed = TRUE;
        else
        {
            lx_guest_store_u16(record, monochrome ? BS_PATTERN : BS_DIBPATTERN);
            lx_guest_store_u16(record + 2, 0);
            memcpy(record + 4, dib, total);
            if (append_record(r, WIN32_META_DIBCREATEPATTERNBRUSH, record, total + 4))
                select_recorded(r, WIN32_META_SELECTOBJECT, r->objects++);
        }
        free(record);
        free(dib);
        return;
    }
    else if (kind == WIN32_OBJ_BRUSH && open32BrushObject(object, sizeof source, source))
    {
        /* LOGBRUSH16: style and hatch are words. */
        type = WIN32_META_CREATEBRUSHINDIRECT;
        bytes = 8;
        lx_guest_store_u16(wire, (uint16)lx_guest_load_u32(source + WIN32_LOGBRUSH_STYLE));
        memcpy(wire + 2, source + WIN32_LOGBRUSH_COLOR, 4);
        lx_guest_store_u16(wire + 6, (uint16)lx_guest_load_u32(source + WIN32_LOGBRUSH_HATCH));
    }
    else if (kind == WIN32_OBJ_FONT && open32FontObject(object, sizeof source, source))
    {
        type = WIN32_META_CREATEFONTINDIRECT;
        bytes = WIN32_LOGFONT16_SIZE;
        for (unsigned i = 0; i < WIN32_LOGFONT16_ITALIC / 2; i++)
            lx_guest_store_u16(wire + i * 2, (uint16)lx_guest_load_u32(source + i * 4));
        memcpy(wire + WIN32_LOGFONT16_ITALIC, source + WIN32_LOGFONT_ITALIC,
               WIN32_LOGFONT_SIZE - WIN32_LOGFONT_ITALIC);
    }
    else
    {
        r->failed = TRUE;
        return;
    }
    if (r->objects >= UINT16_MAX || !append_record(r, type, wire, bytes))
    {
        r->failed = TRUE;
        return;
    }
    uint8 index[2];
    lx_guest_store_u16(index, (uint16)r->objects++);
    append_record(r, WIN32_META_SELECTOBJECT, index, 2);
}

void open32MetafileExtText(ULONG dc, LONG x, LONG y, ULONG flags, PVOID rectangle, PCHAR text,
                           ULONG count, PLONG advances)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    ULONG options = flags & (WIN32_ETO_OPAQUE | WIN32_ETO_CLIPPED);
    BOOL valid =
        count <= UINT16_MAX && x >= INT16_MIN && x <= INT16_MAX && y >= INT16_MIN && y <= INT16_MAX;
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
        append_record(r, WIN32_META_EXTTEXTOUT, p, bytes);
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
        select_recorded(r, WIN32_META_SELECTCLIPREGION, WMF_STOCK_OBJECT_INDEX);
        return;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(region, LX_HANDLE_OPEN32_REGION, &borrow))
    {
        r->failed = TRUE;
        return;
    }
    PmRegion *source = borrow.resource;
    size_t bytes = WMF_REGION_SIZE + source->count * RECORDED_SCAN_SIZE;
    uint8 *p = r->objects < UINT16_MAX && source->count <= UINT16_MAX ? calloc(bytes, 1) : NULL;
    BOOL valid = p != NULL;
    if (p)
    {
        lx_guest_store_u16(p + WMF_REGION_OBJECT_TYPE, WMF_REGION_TYPE);
        lx_guest_store_u16(p + WMF_REGION_BYTES, (uint16)bytes);
        lx_guest_store_u16(p + WMF_REGION_SCANS, (uint16)source->count);
        lx_guest_store_u16(p + WMF_REGION_MAX_SCAN, RECORDED_SCAN_SIZE);
        for (size_t i = 0; i < source->count; i++)
        {
            const RECTL *q = &source->rects[i];
            uint8 *scan = p + WMF_REGION_SIZE + i * RECORDED_SCAN_SIZE;
            LONG values[] = {2, q->yBottom, q->yTop, q->xLeft, q->xRight, 2};
            for (unsigned k = 0; k < RECORDED_SCAN_SIZE / 2; k++)
            {
                valid = valid && values[k] >= INT16_MIN && values[k] <= INT16_MAX;
                lx_guest_store_u16(scan + k * 2, (uint16)values[k]);
            }
        }
    }
    lx_handle_release(&borrow);
    if (valid && append_record(r, WIN32_META_CREATEREGION, p, bytes))
        select_recorded(r, WIN32_META_SELECTCLIPREGION, r->objects++);
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
    uint8 *p = n && n <= UINT16_MAX && r->objects < UINT16_MAX ? malloc(4 + (size_t)n * 4) : NULL;
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u16(p, LOGPALETTE_VERSION);
    lx_guest_store_u16(p + 2, (uint16)n);
    GetPaletteEntries(palette, 0, n, p + 4);
    if (append_record(r, WIN32_META_CREATEPALETTE, p, 4 + (size_t)n * 4))
        select_recorded(r, WIN32_META_SELECTPALETTE, r->objects++);
    free(p);
}

void open32MetafileRealize(ULONG dc)
{
    Open32Recorder *r = recorder(dc);
    if (r)
        append_record(r, WIN32_META_REALIZEPALETTE, NULL, 0);
}

void open32MetafilePaletteRecord(ULONG dc, USHORT function, ULONG first, ULONG count, PVOID entries)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
        return;
    if (function == WIN32_META_RESIZEPALETTE)
    {
        select_recorded(r, WIN32_META_RESIZEPALETTE, first);
        return;
    }
    uint8 *p = first <= UINT16_MAX && count <= UINT16_MAX ? malloc(4 + (size_t)count * 4) : NULL;
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    r->dc = dc;
    if (name)
        r->name = strdup(name);
    if (name && !r->name)
    {
        free(r);
        DeleteDC(dc);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    r->size = WIN32_METAHEADER_SIZE;
    r->next = recorders;
    recorders = r;
    /* The DC's default objects open the recording, so playback starts from them. */
    open32MetafileObject(dc, GetStockObject(WHITE_BRUSH));
    open32MetafileObject(dc, GetStockObject(BLACK_PEN));
    open32MetafileObject(dc, GetStockObject(SYSTEM_FONT));
    return dc;
}

ULONG CloseMetaFile(ULONG dc)
{
    Open32Recorder *r = recorder(dc);
    if (!r)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    ULONG result = 0;
    if (append_record(r, 0, NULL, 0))
    {
        lx_guest_store_u16(r->data + WIN32_METAHEADER_TYPE, WIN32_MEMORYMETAFILE);
        lx_guest_store_u16(r->data + WIN32_METAHEADER_HEADER_WORDS, WIN32_METAHEADER_SIZE / 2);
        lx_guest_store_u16(r->data + WIN32_METAHEADER_VERSION, WIN32_METAVERSION300);
        lx_guest_store_u32(r->data + WIN32_METAHEADER_FILE_WORDS, (ULONG)(r->size / 2));
        lx_guest_store_u16(r->data + WIN32_METAHEADER_OBJECTS, (uint16)r->objects);
        lx_guest_store_u32(r->data + WIN32_METAHEADER_MAX_RECORD_WORDS, r->maximum);
        lx_guest_store_u16(r->data + WIN32_METAHEADER_PARAMETERS, 0);
        if (!r->name || metafile_save(r->name, r->data, (ULONG)r->size))
            result = SetMetaFileBitsEx((ULONG)r->size, r->data);
    }
    else
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        ib + pb > METAFILE_SIZE_MAX - STRETCHDIB_HEAD)
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
    uint8 *p = malloc(STRETCHDIB_HEAD + ib + pb);
    if (!p)
    {
        r->failed = TRUE;
        return;
    }
    lx_guest_store_u32(p, operation);
    lx_guest_store_u16(p + 4, (uint16)usage);
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u16(p + 6 + i * 2, (uint16)coords[i]);
    memcpy(p + STRETCHDIB_HEAD, information, ib);
    memcpy(p + STRETCHDIB_HEAD + ib, bits, pb);
    append_record(r, WIN32_META_STRETCHDIB, p, STRETCHDIB_HEAD + ib + pb);
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
    append_record(r, WIN32_META_PATBLT, p, 12);
}

BOOL open32MetafilePolyPolygon(ULONG dc, PVOID points, PVOID counts, LONG polygons)
{
    Open32Recorder *r = recorder(dc);
    if (!r || polygons < 1 || polygons > UINT16_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG total = 0;
    for (LONG i = 0; i < polygons; i++)
    {
        ULONG n = lx_guest_load_u32((uint8 *)counts + i * 4);
        if (n < 2 || n > UINT16_MAX - total)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        total += n;
    }
    size_t bytes = 2 + (size_t)polygons * 2 + (size_t)total * 4;
    uint8 *p = malloc(bytes);
    if (!p)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        ok = append_record(r, WIN32_META_POLYPOLYGON, p, bytes);
    free(p);
    if (!ok)
        open32SetError(ERROR_INVALID_PARAMETER);
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
    append_record(r, WIN32_META_SETPIXEL, p, 8);
}
