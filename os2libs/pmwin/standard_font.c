/* Public domain. No rights reserved. Wholly AI-authored. */
/* The standard font chooser enumerates actual host catalogue faces and writes
   the OS/2 FONTDLG/FATTRS records through checked execution spans. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/standard_dialogs.h"
#include "os2libs/pmctls.h"
#include "emulator/abi.h"
#include <strings.h>

enum
{
    FNTM_FACENAMECHANGED = 0x1032, /* WM_USER + 50 */
    FNTM_POINTSIZECHANGED = 0x1033,
    FNTM_STYLECHANGED = 0x1034,
    FNTM_COLORCHANGED = 0x1035,
    FNTM_UPDATEPREVIEW = 0x1036,
    FNTM_FILTERLIST = 0x1037,
    FAMILY_LIST = 301,
    STYLE_LIST = 302,
    SIZE_COMBO = 305,
    PREVIEW_TEXT = 306, /* DID_SAMPLE */
    OUTLINE_CHECK = 307,
    UNDERSCORE_CHECK = 308,
    STRIKEOUT_CHECK = 309,
    FOREGROUND_LIST = 330, /* The toolkit assigns no identities to the color lists. */
    BACKGROUND_LIST = 331,
    LM_INSERTITEM = 0x161,
    LM_SELECTITEM = 0x164,
    LM_QUERYSELECTION = 0x165,
    LM_DELETEALL = 0x16e,
    BM_QUERYCHECK = 0x124,
    BM_SETCHECK = 0x125
};

typedef struct FontEntry
{
    unsigned index, bold, italic;
    char family[32], face[32];
} FontEntry;

typedef struct PmFontDialog
{
    unsigned references;
    uint32 address;
    LXFONTDLG data, initial;
    HWND window;
    FontEntry *fonts;
    size_t count, current;
    BOOL filling;
} PmFontDialog;

static const struct
{
    LONG color;
    const char *name;
} font_colors[] = {{CLR_BLACK, "Black"},
                   {CLR_WHITE, "White"},
                   {CLR_BLUE, "Blue"},
                   {2, "Red"},
                   {3, "Pink"},
                   {4, "Green"},
                   {5, "Cyan"},
                   {6, "Yellow"},
                   {CLR_DARKGRAY, "Dark gray"},
                   {9, "Dark blue"},
                   {10, "Dark red"},
                   {11, "Dark pink"},
                   {12, "Dark green"},
                   {13, "Dark cyan"},
                   {CLR_BROWN, "Brown"},
                   {CLR_PALEGRAY, "Pale gray"}};

static const unsigned font_color_count = sizeof font_colors / sizeof *font_colors;
static _Thread_local PmFontDialog *pending_font;

static void release_font(PmFontDialog *d)
{
    if (!--d->references)
    {
        free(d->fonts);
        free(d);
    }
}

void destroyStandardFont(Window *w)
{
    if (w->standard_font)
    {
        release_font(w->standard_font);
        w->standard_font = NULL;
    }
}

static BOOL write_font(PmFontDialog *d)
{
    return lx_execution_write(GProcessState.execution, d->address, &d->data, sizeof d->data);
}

static char *font_string(ULONG address)
{
    return address ? lx_execution_copy_string(GProcessState.execution, address, 65536) : NULL;
}

static LONG selected_item(HWND window, ULONG id)
{
    return (SHORT)(uintptr_t)WinSendDlgItemMsg(window, id, LM_QUERYSELECTION,
                                               (MPARAM)(uintptr_t)0xffff, 0);
}

static void insert_item(HWND window, ULONG id, const char *text)
{
    WinSendDlgItemMsg(window, id, LM_INSERTITEM, (MPARAM)(intptr_t)-1, (MPARAM)(uintptr_t)text);
}

static BOOL checked(PmFontDialog *d, ULONG id)
{
    return (USHORT)(uintptr_t)WinSendDlgItemMsg(d->window, id, BM_QUERYCHECK, 0, 0) != 0;
}

/* FATTR_SEL_OUTLINE, FATTR_SEL_UNDERSCORE and FATTR_SEL_STRIKEOUT come from the check boxes. */
static ULONG emphasis(PmFontDialog *d)
{
    return (checked(d, OUTLINE_CHECK) ? 8 : 0) | (checked(d, UNDERSCORE_CHECK) ? 2 : 0) |
           (checked(d, STRIKEOUT_CHECK) ? 0x10 : 0);
}

static double selected_points(PmFontDialog *d)
{
    char text[32] = {0};
    WinQueryDlgItemText(d->window, SIZE_COMBO, sizeof text, text);
    char *end;
    double points = strtod(text, &end);
    return *end || points < 1 || points > 32767 ? 0 : points;
}

static BOOL commit_font(PmFontDialog *d)
{
    double points = selected_points(d);
    if (d->current >= d->count || !points)
        return FALSE;
    unsigned pixels =
        (unsigned)(points * (GProcessState.display_dpi ? GProcessState.display_dpi : 96) / 72.0 +
                   0.5);
    if (!pixels)
        pixels = 1;
    LxHostFont *font = lx_host_font_open(d->fonts[d->current].index, pixels);
    LxHostFontMetrics m;
    BOOL ok = font && lx_host_font_metrics(font, &m);
    lx_host_font_close(font);
    if (!ok)
        return FALSE;
    if (d->data.pszFamilyname && d->data.usFamilyBufLen)
    {
        size_t n = strlen(m.family);
        if (n >= d->data.usFamilyBufLen)
            n = d->data.usFamilyBufLen - 1;
        char family[32];
        memcpy(family, m.family, n);
        family[n] = 0;
        if (!lx_execution_write(GProcessState.execution, d->data.pszFamilyname, family, n + 1))
            return FALSE;
    }
    FATTRS *f = &d->data.fAttrs;
    memset(f, 0, sizeof *f);
    f->usRecordLength = sizeof *f;
    d->data.flStyle = emphasis(d);
    f->fsSelection = (m.italic ? 1 : 0) | (m.bold ? 0x20 : 0) | d->data.flStyle;
    d->data.flType = m.italic ? 1 : 0; /* FTYPE_ITALIC */
    memcpy(f->szFacename, m.face, sizeof f->szFacename);
    f->szFacename[31] = 0;
    f->usCodePage = 437;
    f->lMaxBaselineExt = m.ascent + m.descent;
    f->lAveCharWidth = m.average_width;
    f->fsFontUse = m.sfnt ? 3 : 0;
    d->data.fxPointSize = (LONG)(points * 65536 + 0.5);
    d->data.sNominalPointSize = (SHORT)(points * 10 + 0.5);
    d->data.usWeight = m.bold ? 7 : 5;
    d->data.usWidth = 5;
    d->data.lEmHeight = m.em;
    d->data.lXHeight = m.x_height;
    d->data.lExternalLeading = m.leading;
    d->data.lReturn = 1;
    d->data.lSRC = 0;
    return write_font(d);
}

/* Dialog procedures may be guest code, so strings and records travel in a guest mapping. */
static LxGuestMapping *font_mapping(void)
{
    return lx_execution_map(GProcessState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
}

static MRESULT font_notify(PmFontDialog *d, LxGuestMapping *mapping, ULONG message,
                           const char *text, MPARAM second)
{
    char *guest = lx_execution_mapping_host(mapping);
    snprintf(guest, 4096, "%s", text);
    return WinSendMsg(d->window, message, (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping),
                      second);
}

static void store_color(HWND preview, ULONG parameter, LONG color)
{
    uint8 wire[4];
    lx_guest_store_u32(wire, (uint32)color);
    WinSetPresParam(preview, parameter, sizeof wire, wire);
}

/* The preview control shows the selection through presentation parameters. */
static void update_preview(PmFontDialog *d, HWND preview)
{
    if (!preview || d->current >= d->count)
        return;
    double points = selected_points(d);
    char attribute[64];
    snprintf(attribute, sizeof attribute, "%ld.%s", points >= 1 ? (long)points : 10L,
             d->fonts[d->current].face);
    WinSetPresParam(preview, 15, (ULONG)strlen(attribute) + 1, attribute); /* PP_FONTNAMESIZE */
    store_color(preview, 2, d->data.clrFore); /* PP_FOREGROUNDCOLORINDEX */
    store_color(preview, 4, d->data.clrBack); /* PP_BACKGROUNDCOLORINDEX */
    char *text = font_string(d->data.pszPreview);
    if (text)
        WinSetWindowText(preview, text);
    free(text);
}

static void refresh_preview(PmFontDialog *d)
{
    if (!d->filling)
        WinSendMsg(d->window, FNTM_UPDATEPREVIEW,
                   (MPARAM)(uintptr_t)WinWindowFromID(d->window, PREVIEW_TEXT), 0);
}

/* Family list rows follow each family's first appearance in the catalogue. */
static BOOL first_of_family(const PmFontDialog *d, size_t index)
{
    for (size_t i = 0; i < index; i++)
        if (!strcmp(d->fonts[i].family, d->fonts[index].family))
            return FALSE;
    return TRUE;
}

static const char *style_name(const FontEntry *e)
{
    return e->bold && e->italic ? "Bold Italic"
           : e->bold            ? "Bold"
           : e->italic          ? "Italic"
                                : "Normal";
}

/* Selecting a family lists its styles, keeping the nearest match to the wanted style. */
static void fill_styles(PmFontDialog *d, const char *family, unsigned bold, unsigned italic)
{
    WinSendDlgItemMsg(d->window, STYLE_LIST, LM_DELETEALL, 0, 0);
    LONG row = 0, chosen = 0;
    int best = -1;
    for (size_t i = 0; i < d->count; i++)
    {
        const FontEntry *e = &d->fonts[i];
        if (strcmp(e->family, family))
            continue;
        insert_item(d->window, STYLE_LIST, style_name(e));
        int score = (!e->bold == !bold) * 2 + (!e->italic == !italic);
        if (score > best)
        {
            best = score;
            chosen = row;
            d->current = i;
        }
        row++;
    }
    WinSendDlgItemMsg(d->window, STYLE_LIST, LM_SELECTITEM, (MPARAM)(uintptr_t)chosen,
                      (MPARAM)(uintptr_t)TRUE);
}

/* Row numbers count the entries of the current family in catalogue order. */
static void select_style(PmFontDialog *d, LONG row)
{
    if (row < 0 || d->current >= d->count)
        return;
    const char *family = d->fonts[d->current].family;
    for (size_t i = 0; i < d->count; i++)
        if (!strcmp(d->fonts[i].family, family) && !row--)
        {
            d->current = i;
            return;
        }
}

static void send_style_change(PmFontDialog *d, const FontEntry *old, ULONG old_style)
{
    if (d->filling || d->current >= d->count)
        return;
    LxGuestMapping *mapping = font_mapping();
    if (!mapping)
        return;
    const FontEntry *e = &d->fonts[d->current];
    uint8 *record = lx_execution_mapping_host(mapping);
    ULONG style = emphasis(d);
    lx_guest_store_u16(record, e->bold ? 7 : 5);
    lx_guest_store_u16(record + 2, old && old->bold ? 7 : 5);
    lx_guest_store_u16(record + 4, 5);
    lx_guest_store_u16(record + 6, 5);
    lx_guest_store_u32(record + 8, e->italic ? 1 : 0);
    lx_guest_store_u32(record + 12, old && old->italic ? 1 : 0);
    lx_guest_store_u32(record + 16, 1);
    lx_guest_store_u32(record + 20, 1);
    lx_guest_store_u32(record + 24, style);
    lx_guest_store_u32(record + 28, old_style);
    lx_guest_store_u32(record + 32, 0x1a);
    lx_guest_store_u32(record + 36, 0x1a);
    d->data.flStyle = style;
    WinSendMsg(d->window, FNTM_STYLECHANGED,
               (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping), 0);
    lx_execution_unmap(GProcessState.execution, mapping);
}

static void select_color(PmFontDialog *d, ULONG list, LONG color)
{
    for (unsigned i = 0; i < font_color_count; i++)
        if (font_colors[i].color == color)
            WinSendDlgItemMsg(d->window, list, LM_SELECTITEM, (MPARAM)(uintptr_t)i,
                              (MPARAM)(uintptr_t)TRUE);
}

static void populate_font(PmFontDialog *d)
{
    d->filling = TRUE;
    static const ULONG lists[] = {FAMILY_LIST, STYLE_LIST, FOREGROUND_LIST, BACKGROUND_LIST};
    for (unsigned i = 0; i < 4; i++)
        WinSendDlgItemMsg(d->window, lists[i], LM_DELETEALL, 0, 0);
    free(d->fonts);
    d->fonts = NULL;
    d->count = d->current = 0;
    unsigned total = lx_host_font_count();
    if (total > 65536)
        total = 65536;
    d->fonts = calloc(total ? total : 1, sizeof *d->fonts);
    if (!d->fonts)
    {
        d->filling = FALSE;
        return;
    }
    char *family = font_string(d->data.pszFamilyname);
    LxGuestMapping *mapping = font_mapping();
    BOOL by_attributes = (d->data.fl & 0x80) != 0; /* FNTS_INITFROMFATTRS */
    for (unsigned i = 0; i < total; i++)
    {
        LxHostFont *f = lx_host_font_open(i, 16);
        LxHostFontMetrics m;
        BOOL ok = f && lx_host_font_metrics(f, &m);
        lx_host_font_close(f);
        if (!ok)
            continue;
        /* FNTI_FAMILYNAME and FNTI_STYLENAME with outline/bit-map and fixed/proportional kinds. */
        ULONG kind = (m.sfnt ? 2 : 1) | (m.fixed ? 4 : 8);
        if (mapping && (!font_notify(d, mapping, FNTM_FILTERLIST, m.family,
                                     (MPARAM)(uintptr_t)(0x100 | kind << 16)) ||
                        !font_notify(d, mapping, FNTM_FILTERLIST, m.face,
                                     (MPARAM)(uintptr_t)(0x200 | kind << 16))))
            continue;
        FontEntry *e = &d->fonts[d->count++];
        e->index = i;
        e->bold = m.bold;
        e->italic = m.italic;
        snprintf(e->family, sizeof e->family, "%s", m.family);
        snprintf(e->face, sizeof e->face, "%s", m.face);
    }
    if (mapping)
        lx_execution_unmap(GProcessState.execution, mapping);
    size_t wanted = 0;
    for (size_t i = 0; i < d->count; i++)
    {
        if (first_of_family(d, i))
            insert_item(d->window, FAMILY_LIST, d->fonts[i].family);
        BOOL match = by_attributes ? !strncmp(d->data.fAttrs.szFacename, d->fonts[i].face, 32)
                                   : family && !strcasecmp(family, d->fonts[i].family);
        if (match && !wanted)
            wanted = i + 1;
    }
    free(family);
    if (d->count)
    {
        const FontEntry *start = &d->fonts[wanted ? wanted - 1 : 0];
        unsigned bold = d->data.usWeight >= 7, italic = (d->data.flType & 1) != 0;
        if (by_attributes)
        {
            bold = start->bold;
            italic = start->italic;
        }
        LONG chosen = 0;
        for (size_t i = 0; i < (size_t)(start - d->fonts); i++)
            if (first_of_family(d, i) && strcmp(d->fonts[i].family, start->family))
                chosen++;
        WinSendDlgItemMsg(d->window, FAMILY_LIST, LM_SELECTITEM, (MPARAM)(uintptr_t)chosen,
                          (MPARAM)(uintptr_t)TRUE);
        fill_styles(d, start->family, bold, italic);
    }
    /* pszPtSizeList is a space-separated list of sizes. */
    char *sizes = font_string(d->data.pszPtSizeList);
    const char *list = sizes ? sizes : "8 9 10 11 12 14 18 24";
    WinSendDlgItemMsg(d->window, SIZE_COMBO, LM_DELETEALL, 0, 0);
    while (*list)
    {
        char size[16];
        size_t n = strcspn(list, " ");
        if (n && n < sizeof size)
        {
            memcpy(size, list, n);
            size[n] = 0;
            insert_item(d->window, SIZE_COMBO, size);
        }
        list += n;
        list += strspn(list, " ");
    }
    free(sizes);
    char size[32];
    double points = d->data.fxPointSize > 0 ? d->data.fxPointSize / 65536.0 : 10;
    snprintf(size, sizeof size, "%.2f", points);
    WinSetDlgItemText(d->window, SIZE_COMBO, size);
    for (unsigned i = 0; i < font_color_count; i++)
    {
        insert_item(d->window, FOREGROUND_LIST, font_colors[i].name);
        insert_item(d->window, BACKGROUND_LIST, font_colors[i].name);
    }
    select_color(d, FOREGROUND_LIST, d->data.clrFore);
    select_color(d, BACKGROUND_LIST, d->data.clrBack);

    static const struct
    {
        ULONG id, bit;
    } checks[] = {{OUTLINE_CHECK, 8}, {UNDERSCORE_CHECK, 2}, {STRIKEOUT_CHECK, 0x10}};

    for (unsigned i = 0; i < 3; i++)
        WinSendDlgItemMsg(d->window, checks[i].id, BM_SETCHECK,
                          (MPARAM)(uintptr_t)((d->data.flStyle & checks[i].bit) != 0), 0);
    d->filling = FALSE;
    refresh_preview(d);
}

/* The guest sees control changes as the FNTM_* messages a custom procedure may subclass. */
static void font_control(PmFontDialog *d, USHORT id, USHORT code)
{
    if (d->filling)
        return;
    if (id == FAMILY_LIST && code == 1) /* LN_SELECT */
    {
        LONG row = selected_item(d->window, FAMILY_LIST);
        if (row < 0 || d->current >= d->count)
            return;
        /* Family rows follow first appearance in the catalogue. */
        const FontEntry *chosen = NULL;
        LONG seen = 0;
        for (size_t i = 0; i < d->count && !chosen; i++)
            if (first_of_family(d, i) && seen++ == row)
                chosen = &d->fonts[i];
        if (!chosen)
            return;
        FontEntry old = d->fonts[d->current];
        d->filling = TRUE;
        fill_styles(d, chosen->family, old.bold, old.italic);
        d->filling = FALSE;
        LxGuestMapping *mapping = font_mapping();
        if (mapping)
        {
            font_notify(d, mapping, FNTM_FACENAMECHANGED, chosen->family, 0);
            lx_execution_unmap(GProcessState.execution, mapping);
        }
        send_style_change(d, &old, d->data.flStyle);
    }
    else if (id == STYLE_LIST && code == 1)
    {
        if (d->current >= d->count)
            return;
        FontEntry old = d->fonts[d->current];
        select_style(d, selected_item(d->window, STYLE_LIST));
        send_style_change(d, &old, d->data.flStyle);
    }
    else if (id == SIZE_COMBO && (code == 1 || code == 4)) /* CBN_EFCHANGE, CBN_LBSELECT */
    {
        char text[32] = {0};
        WinQueryDlgItemText(d->window, SIZE_COMBO, sizeof text, text);
        double points = selected_points(d);
        LxGuestMapping *mapping = points ? font_mapping() : NULL;
        if (mapping)
        {
            font_notify(d, mapping, FNTM_POINTSIZECHANGED, text,
                        (MPARAM)(uintptr_t)(LONG)(points * 65536 + 0.5));
            lx_execution_unmap(GProcessState.execution, mapping);
        }
    }
    else if ((id == FOREGROUND_LIST || id == BACKGROUND_LIST) && code == 1)
    {
        LONG row = selected_item(d->window, id);
        if (row < 0 || (unsigned)row >= font_color_count)
            return;
        LONG color = font_colors[row].color;
        if (id == FOREGROUND_LIST)
            d->data.clrFore = color;
        else
            d->data.clrBack = color;
        WinSendMsg(d->window, FNTM_COLORCHANGED, (MPARAM)(intptr_t)color,
                   (MPARAM)(uintptr_t)(id == FOREGROUND_LIST ? 1 : 2)); /* CLRC_ */
    }
    else if (id >= OUTLINE_CHECK && id <= STRIKEOUT_CHECK && code == 1) /* BN_CLICKED */
        send_style_change(d, d->current < d->count ? &d->fonts[d->current] : NULL, d->data.flStyle);
    else
        return;
    refresh_preview(d);
}

MRESULT lx_pm_font_procedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    PmFontDialog *d = w ? w->standard_font : NULL;
    if (!d)
        return WinDefDlgProc(handle, message, first, second);
    if (message == FNTM_FILTERLIST)
    {
        ULONG kinds = (ULONG)(uintptr_t)second >> 16, fields = (ULONG)(uintptr_t)second & 0xffff;
        ULONG fl = d->data.fl;
        BOOL technology = ((kinds & 1) && !(fl & 0x200)) || ((kinds & 2) && !(fl & 0x100));
        BOOL pitch = ((kinds & 4) && !(fl & 0x800)) || ((kinds & 8) && !(fl & 0x400));
        BOOL synthesized = (kinds & 0x10) && !(fl & 0x1000);
        return (MRESULT)(uintptr_t)((fields & 0x400) ||
                                    ((fields & 0x300) && ((technology && pitch) || synthesized)));
    }
    if (message == FNTM_UPDATEPREVIEW)
    {
        update_preview(d, (HWND)(uintptr_t)first);
        return 0;
    }
    if (message >= FNTM_FACENAMECHANGED && message <= FNTM_COLORCHANGED)
        return 0;
    if (message == WM_CONTROL)
    {
        font_control(d, (USHORT)(uintptr_t)first, (USHORT)((uintptr_t)first >> 16));
        return 0;
    }
    if (message == WM_COMMAND)
    {
        USHORT id = (USHORT)(uintptr_t)first;
        if (id == 1 || id == 311)
        {
            if (commit_font(d) && id == 1)
                WinDismissDlg(handle, 1);
            return 0;
        }
        if (id == 2)
        {
            d->data.lReturn = 2;
            d->data.lSRC = 0;
            write_font(d);
            WinDismissDlg(handle, 2);
            return 0;
        }
        if (id == 312)
        {
            d->data = d->initial;
            populate_font(d);
            return 0;
        }
    }
    if (message == WM_CLOSE)
    {
        d->data.lReturn = 2;
        write_font(d);
        WinDismissDlg(handle, 2);
        return 0;
    }
    return WinDefDlgProc(handle, message, first, second);
}

static MRESULT OS2API font_dispatch(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w)
        return 0;
    if (message == WM_INITDLG && pending_font && !w->standard_font)
    {
        PmFontDialog *d = pending_font;
        w->standard_font = d;
        d->references++;
        d->window = handle;
        char *title = font_string(d->data.pszTitle);
        WinSetWindowText(handle, title ? title : "Font");
        free(title);
        WinShowWindow(WinWindowFromID(handle, 311), (d->data.fl & 16) != 0);
        WinShowWindow(WinWindowFromID(handle, 312), (d->data.fl & 32) != 0);
        /* Subclass first so a custom procedure sees FNTM_FILTERLIST for each face. */
        if (d->data.pfnDlgProc)
        {
            WinSubclassWindow(handle, (PFNWP)(uintptr_t)d->data.pfnDlgProc);
            populate_font(d);
            return WinSendMsg(handle, message, first, second);
        }
        populate_font(d);
    }
    return lx_pm_font_procedure(handle, message, first, second);
}

static PDLGTEMPLATE font_template(uint8 *bytes, size_t capacity)
{
    typedef struct
    {
        USHORT id, kind;
        SHORT x, y, w, h;
        ULONG style;
        const char *text;
    } Item;

    /* Control kinds: 2 combination box, 3 button, 5 static text, 7 list box. */
    static const Item controls[] = {{313, 5, 8, 203, 130, 12, 1, "Name:"},
                                    {FAMILY_LIST, 7, 8, 116, 130, 85, 0x20000, ""},
                                    {314, 5, 146, 203, 100, 12, 1, "Style:"},
                                    {STYLE_LIST, 7, 146, 116, 100, 85, 0x20000, ""},
                                    {315, 5, 254, 203, 70, 12, 1, "Size:"},
                                    {SIZE_COMBO, 2, 254, 116, 70, 85, 0x20002, ""},
                                    {332, 5, 8, 100, 90, 12, 1, "Color:"},
                                    {FOREGROUND_LIST, 7, 8, 48, 90, 50, 0x20000, ""},
                                    {333, 5, 104, 100, 90, 12, 1, "Background:"},
                                    {BACKGROUND_LIST, 7, 104, 48, 90, 50, 0x20000, ""},
                                    {OUTLINE_CHECK, 3, 202, 84, 80, 14, 0x20002, "Outline"},
                                    {UNDERSCORE_CHECK, 3, 202, 68, 80, 14, 0x20002, "Underscore"},
                                    {STRIKEOUT_CHECK, 3, 202, 52, 80, 14, 0x20002, "Strikeout"},
                                    {PREVIEW_TEXT, 5, 8, 26, 316, 20, 1, "abcdABCD"},
                                    {1, 3, 8, 6, 60, 14, 0x20400, "OK"},
                                    {2, 3, 74, 6, 60, 14, 0x20000, "Cancel"},
                                    {311, 3, 140, 6, 60, 14, 0x20000, "Apply"},
                                    {312, 3, 206, 6, 60, 14, 0x20000, "Reset"}};
    unsigned count = sizeof controls / sizeof *controls;
    size_t offset = 14 + (count + 1) * 30;
    if (capacity < offset)
        return NULL;
    memset(bytes, 0, capacity);
    DLGTEMPLATE *t = (void *)bytes;
    t->codepage = 437;
    t->offadlgti = 14;
    t->iItemFocus = 2;
    DLGTITEM *items = (void *)(bytes + 14);
    items[0].cChildren = count;
    items[0].offClassName = 1;
    items[0].id = 300;
    items[0].cx = 332;
    items[0].cy = 220;
    items[0].flStyle = WS_VISIBLE;
    for (unsigned i = 0; i <= count; i++)
    {
        DLGTITEM *item = &items[i];
        item->offPresParams = item->offCtlData = 0xffff;
        if (!i)
            continue;
        const Item *c = &controls[i - 1];
        item->offClassName = c->kind;
        item->id = c->id;
        item->x = c->x;
        item->y = c->y;
        item->cx = c->w;
        item->cy = c->h;
        item->flStyle = WS_VISIBLE | c->style;
        size_t n = strlen(c->text);
        if (offset + n + 1 > capacity)
            return NULL;
        item->cchText = n;
        item->offText = offset;
        memcpy(bytes + offset, c->text, n + 1);
        offset += n + 1;
    }
    t->cbTemplate = offset;
    return t;
}

static _Thread_local PDLGTEMPLATE custom_font_template;

HWND lx_pm_font_dialog_template(HWND parent, HWND owner, PVOID address, PVOID data)
{
    custom_font_template = data;
    HWND result = lx_pm_font_dialog(parent, owner, address);
    custom_font_template = NULL;
    return result;
}

HWND lx_pm_font_dialog(HWND parent, HWND owner, PVOID address)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || (uintptr_t)address > UINT32_MAX)
        return 0;
    PmFontDialog *d = calloc(1, sizeof *d);
    if (!d)
        return 0;
    d->references = 1;
    d->address = (uint32)(uintptr_t)address;
    if (!lx_execution_read(GProcessState.execution, d->address, &d->data, sizeof d->data) ||
        d->data.cbSize != sizeof d->data)
    {
        release_font(d);
        return 0;
    }
    d->initial = d->data;
    d->data.lReturn = 0;
    d->data.lSRC = 0;
    if (!write_font(d))
    {
        release_font(d);
        return 0;
    }
    PmFontDialog *previous = pending_font;
    pending_font = d;
    uint8 resource[2048];
    HWND handle =
        custom_font_template
            ? WinCreateDlg(parent, owner, font_dispatch, custom_font_template, address)
        : (d->data.fl & 2)
            ? WinLoadDlg(parent, owner, font_dispatch, d->data.hMod, d->data.usDlgId, address)
            : WinCreateDlg(parent, owner, font_dispatch, font_template(resource, sizeof resource),
                           address);
    pending_font = previous;
    if (!handle)
    {
        d->data.lSRC = 12;
        write_font(d);
        release_font(d);
        return 0;
    }
    Window *w = getWindowFromHWND(a, handle);
    if (w && !(d->data.fl & 2))
    {
        LONG x = d->data.x, y = d->data.y;
        if ((d->data.fl & 1) || (!x && !y))
        {
            x = (desktop_window.w - w->w) / 2;
            y = (desktop_window.h - w->h) / 2;
        }
        WinSetWindowPos(handle, HWND_TOP, x, y, w->w, w->h, SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    if (d->data.fl & 64)
    {
        release_font(d);
        return handle;
    }
    ULONG result = WinProcessDlg(handle);
    d->data.lReturn = result;
    write_font(d);
    if (WinIsWindow(a->hab, handle))
        WinDestroyWindow(handle);
    release_font(d);
    return TRUE;
}
