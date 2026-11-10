/* Public domain. No rights reserved. Wholly AI-authored. */
/* LOGFONT32 records retain requested names while GPI selects available outline faces. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmgpi.h"
#include "host/font.h"

enum
{
    /* Font heights and widths accepted, in pels; 0 asks for the default height. */
    FONT_SIZE_MAX = 512,
    FONT_DEFAULT_PELS = 16,
    /* Host fonts are numbered family * 4 + bold + 2 * italic. */
    HOST_FONT_FAMILY_HELV = 0,
    HOST_FONT_FAMILY_TIMES = 1,
    HOST_FONT_FAMILY_COURIER = 2,
    HOST_FONT_STYLES = 4,
    HOST_FONT_BOLD = 1,
    HOST_FONT_ITALIC = 2,
    /* The local identifier GPI holds a selected font under. */
    SELECTED_FONT_LCID = 1,
    LONG_BYTES = 4
};

typedef struct Open32Font
{
    uint8 record[WIN32_LOGFONT_SIZE];
    FATTRS attributes;
    BOOL stock;
} Open32Font;

ULONG CreateFontIndirect(PVOID record)
{
    if (!record || !memchr((uint8 *)record + WIN32_LOGFONT_FACE_NAME, 0, WIN32_LF_FACESIZE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const uint8 *bytes = record;
    LONG height = (LONG)lx_guest_load_u32(bytes + WIN32_LOGFONT_HEIGHT);
    LONG width = (LONG)lx_guest_load_u32(bytes + WIN32_LOGFONT_WIDTH);
    /* Escapement and orientation (rotated text) are not supported. */
    if (height < -FONT_SIZE_MAX || height > FONT_SIZE_MAX || width < 0 || width > FONT_SIZE_MAX ||
        lx_guest_load_u32(bytes + WIN32_LOGFONT_ESCAPEMENT) ||
        lx_guest_load_u32(bytes + WIN32_LOGFONT_ORIENTATION))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Open32Font *font = calloc(1, sizeof *font);
    if (!font)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    memcpy(font->record, bytes, WIN32_LOGFONT_SIZE);
    unsigned family = HOST_FONT_FAMILY_HELV;
    const char *face = (const char *)bytes + WIN32_LOGFONT_FACE_NAME;
    if (strcasestr(face, "courier") || strcasestr(face, "mono"))
        family = HOST_FONT_FAMILY_COURIER;
    else if (strcasestr(face, "times") || strcasestr(face, "tms") || strcasestr(face, "roman"))
        family = HOST_FONT_FAMILY_TIMES;
    BOOL bold = lx_guest_load_u32(bytes + WIN32_LOGFONT_WEIGHT) >= WIN32_FW_SEMIBOLD;
    BOOL italic = bytes[WIN32_LOGFONT_ITALIC] != 0;
    unsigned index =
        family * HOST_FONT_STYLES + (bold ? HOST_FONT_BOLD : 0) + (italic ? HOST_FONT_ITALIC : 0);
    unsigned pixels = height < 0 ? (unsigned)-height
                      : height   ? (unsigned)height
                                 : FONT_DEFAULT_PELS;
    /* A positive height is the cell height: pick the size whose ascent plus descent is nearest. */
    if (height > 0)
    {
        unsigned difference = UINT32_MAX, selected = pixels;
        for (unsigned n = 1; n <= (unsigned)height; n++)
        {
            LxHostFont *host = lx_host_font_open(index, n);
            LxHostFontMetrics metrics;
            if (host && lx_host_font_metrics(host, &metrics))
            {
                LONG delta = metrics.ascent + metrics.descent - height;
                unsigned distance = delta < 0 ? (unsigned)-delta : (unsigned)delta;
                if (distance < difference)
                {
                    selected = n;
                    difference = distance;
                }
            }
            lx_host_font_close(host);
        }
        pixels = selected;
    }
    font->attributes.usRecordLength = sizeof(FATTRS);
    const char *families[] = {"Helv", "Tms Rmn", "Courier"};
    strcpy(font->attributes.szFacename, families[family]);
    font->attributes.fsSelection = (bold ? FATTR_SEL_BOLD : 0) | (italic ? FATTR_SEL_ITALIC : 0) |
                                   (bytes[WIN32_LOGFONT_UNDERLINE] ? FATTR_SEL_UNDERSCORE : 0) |
                                   (bytes[WIN32_LOGFONT_STRIKEOUT] ? FATTR_SEL_OUTLINE : 0);
    font->attributes.lMaxBaselineExt = (LONG)pixels;
    font->attributes.lAveCharWidth = width;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_FONT, font, free);
    if (!handle)
    {
        free(font);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return handle;
}

ULONG open32SelectFont(Open32DC *dc, ULONG handle)
{
    LxHandleBorrow next = {0};
    if (!dc || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &next))
        return 0;
    Open32Font *font = next.resource;
    ULONG previous = dc->font_handle;
    if (!GpiSetCharSet(dc->ps, LCID_DEFAULT) || !GpiDeleteSetId(dc->ps, SELECTED_FONT_LCID) ||
        !GpiCreateLogFont(dc->ps, NULL, SELECTED_FONT_LCID, &font->attributes) ||
        !GpiSetCharSet(dc->ps, SELECTED_FONT_LCID))
    {
        lx_handle_release(&next);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    lx_handle_release(&dc->font);
    dc->font = next;
    dc->font_handle = handle;
    return previous;
}

BOOL open32DeleteFont(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &borrow))
        return FALSE;
    Open32Font *font = borrow.resource;
    BOOL result =
        !font->stock && lx_handle_close_guarded(handle, LX_HANDLE_OPEN32_FONT, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}

LONG open32FontObject(ULONG handle, LONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &borrow))
        return 0;
    Open32Font *font = borrow.resource;
    LONG result = !output                          ? WIN32_LOGFONT_SIZE
                  : capacity >= WIN32_LOGFONT_SIZE ? WIN32_LOGFONT_SIZE
                                                   : 0;
    if (output && result)
        memcpy(output, font->record, WIN32_LOGFONT_SIZE);
    lx_handle_release(&borrow);
    return result;
}

ULONG CreateFont(LONG height, LONG width, LONG escapement, LONG orientation, LONG weight,
                 ULONG italic, ULONG underline, ULONG strikeout, ULONG charset, ULONG out_precision,
                 ULONG clip_precision, ULONG quality, ULONG pitch, PSZ face)
{
    uint8 record[WIN32_LOGFONT_SIZE] = {0};
    /* LOGFONT starts with five LONGs and continues with eight BYTEs, in argument order. */
    const LONG dimensions[] = {height, width, escapement, orientation, weight};
    for (unsigned i = 0; i < 5; i++)
        lx_guest_store_u32(record + WIN32_LOGFONT_HEIGHT + i * LONG_BYTES, (ULONG)dimensions[i]);
    const ULONG options[] = {italic,        underline,      strikeout, charset,
                             out_precision, clip_precision, quality,   pitch};
    for (unsigned i = 0; i < 8; i++)
        record[WIN32_LOGFONT_ITALIC + i] = (uint8)options[i];
    if (face)
        memcpy(record + WIN32_LOGFONT_FACE_NAME, face, strnlen(face, WIN32_LF_FACESIZE - 1));
    return CreateFontIndirect(record);
}

BOOL AddFontResource(PSZ path)
{
    if (GpiLoadPublicFonts(0, path))
        return TRUE;
    open32SetError(ERROR_PATH_NOT_FOUND);
    return FALSE;
}

BOOL RemoveFontResource(PSZ path)
{
    if (GpiUnloadPublicFonts(0, path))
        return TRUE;
    open32SetError(ERROR_PATH_NOT_FOUND);
    return FALSE;
}

/* The stock fonts, created on first use for each thread. */
ULONG open32StockFont(LONG index)
{
    static _Thread_local ULONG stocks[DEFAULT_GUI_FONT + 1];
    if (index < OEM_FIXED_FONT || index == DEFAULT_PALETTE || index > DEFAULT_GUI_FONT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!stocks[index])
    {
        uint8 record[WIN32_LOGFONT_SIZE] = {0};
        lx_guest_store_u32(record + WIN32_LOGFONT_HEIGHT, FONT_DEFAULT_PELS);
        lx_guest_store_u32(record + WIN32_LOGFONT_WEIGHT, WIN32_FW_NORMAL);
        strcpy((char *)record + WIN32_LOGFONT_FACE_NAME,
               index == OEM_FIXED_FONT || index == ANSI_FIXED_FONT || index == SYSTEM_FIXED_FONT
                   ? "Courier"
                   : "Helv");
        ULONG handle = CreateFontIndirect(record);
        LxHandleBorrow borrow = {0};
        if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_FONT, &borrow))
            return 0;
        ((Open32Font *)borrow.resource)->stock = TRUE;
        lx_handle_release(&borrow);
        stocks[index] = handle;
    }
    return stocks[index];
}
