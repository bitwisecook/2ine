/* Public domain. No rights reserved. Wholly AI-authored. */
/* The standard font chooser enumerates actual host catalogue faces and writes
   the OS/2 FONTDLG/FATTRS records through checked execution spans. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmp.h"
#include "os2/include/pmstddlg.h"
#include "emulator/abi.h"
#include <strings.h>

enum
{
    /* The Toolkit assigns no identities to the colour lists and their labels. */
    FOREGROUND_LIST = 330,
    BACKGROUND_LIST = 331,
    FOREGROUND_TEXT = 332,
    BACKGROUND_TEXT = 333,
    /* Longest guest string the dialog copies. */
    GUEST_STRING_MAX = 65536,
    /* Faces beyond this many in the host catalogue are not listed. */
    FONT_CATALOGUE_LIMIT = 65536,
    /* Pixel size at which catalogue faces are opened to read their metrics. */
    METRICS_PIXEL_SIZE = 16,
    /* lx_execution_map allocates whole guest pages. */
    GUEST_PAGE_SIZE = 4096,
    DEFAULT_DPI = 96,
    POINTS_PER_INCH = 72,
    DECIPOINTS_PER_POINT = 10,
    DEFAULT_POINT_SIZE = 10,
    /* FIXED is 16.16; point sizes keep its integer part. */
    FIXED_ONE = 65536,
    POINT_SIZE_MAX = 32767,
    FONT_CODEPAGE = 437,
    EMPHASIS_MASK = FATTR_SEL_UNDERSCORE | FATTR_SEL_OUTLINE | FATTR_SEL_STRIKEOUT,
    /* STYLECHANGE in guest memory, the FNTM_STYLECHANGED parameter. */
    STYLECHANGE_USWEIGHT_OFFSET = 0,
    STYLECHANGE_USWEIGHTOLD_OFFSET = 2,
    STYLECHANGE_USWIDTH_OFFSET = 4,
    STYLECHANGE_USWIDTHOLD_OFFSET = 6,
    STYLECHANGE_FLTYPE_OFFSET = 8,
    STYLECHANGE_FLTYPEOLD_OFFSET = 12,
    STYLECHANGE_FLTYPEMASK_OFFSET = 16,
    STYLECHANGE_FLTYPEMASKOLD_OFFSET = 20,
    STYLECHANGE_FLSTYLE_OFFSET = 24,
    STYLECHANGE_FLSTYLEOLD_OFFSET = 28,
    STYLECHANGE_FLSTYLEMASK_OFFSET = 32,
    STYLECHANGE_FLSTYLEMASKOLD_OFFSET = 36,
    /* Room for the built-in template and its strings. */
    TEMPLATE_BUFFER_SIZE = 2048,
    TEMPLATE_CODEPAGE = 437,
    /* A DLGTITEM offset with no presentation parameters or control data. */
    TEMPLATE_NO_DATA = 0xffff,
    TEMPLATE_ITEMS_OFFSET = offsetof(DLGTEMPLATE, adlgti),
    TEMPLATE_ITEM_SIZE = sizeof(DLGTITEM),
    /* adlgti index of the family list, which takes the focus. */
    TEMPLATE_FOCUS_ITEM = 2,
    FONT_DIALOG_WIDTH = 332,
    FONT_DIALOG_HEIGHT = 220,
    /* Text, list, combination box, check box and push button styles of the built-in template. */
    TEXT_STYLE = SS_TEXT,
    LIST_STYLE = WS_TABSTOP,
    SIZE_STYLE = WS_TABSTOP | CBS_DROPDOWN,
    CHECK_STYLE = WS_TABSTOP | BS_AUTOCHECKBOX,
    DEFAULT_BUTTON_STYLE = WS_TABSTOP | BS_PUSHBUTTON | BS_DEFAULT,
    BUTTON_STYLE = WS_TABSTOP | BS_PUSHBUTTON
};

typedef struct FontEntry
{
    unsigned index, bold, italic;
    char family[FACESIZE], face[FACESIZE];
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
                   {CLR_RED, "Red"},
                   {CLR_PINK, "Pink"},
                   {CLR_GREEN, "Green"},
                   {CLR_CYAN, "Cyan"},
                   {CLR_YELLOW, "Yellow"},
                   {CLR_DARKGRAY, "Dark gray"},
                   {CLR_DARKBLUE, "Dark blue"},
                   {CLR_DARKRED, "Dark red"},
                   {CLR_DARKPINK, "Dark pink"},
                   {CLR_DARKGREEN, "Dark green"},
                   {CLR_DARKCYAN, "Dark cyan"},
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

static void destroyStandardFont(Window *w)
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
    return address ? lx_execution_copy_string(GProcessState.execution, address, GUEST_STRING_MAX)
                   : NULL;
}

static LONG selected_item(HWND window, ULONG id)
{
    return (SHORT)(uintptr_t)WinSendDlgItemMsg(window, id, LM_QUERYSELECTION,
                                               (MPARAM)(uintptr_t)(USHORT)LIT_FIRST, 0);
}

static void insert_item(HWND window, ULONG id, const char *text)
{
    WinSendDlgItemMsg(window, id, LM_INSERTITEM, (MPARAM)(intptr_t)LIT_END,
                      (MPARAM)(uintptr_t)text);
}

static BOOL checked(PmFontDialog *d, ULONG id)
{
    return (USHORT)(uintptr_t)WinSendDlgItemMsg(d->window, id, BM_QUERYCHECK, 0, 0) != 0;
}

/* The emphasis styles come from the check boxes. */
static ULONG emphasis(PmFontDialog *d)
{
    return (checked(d, DID_OUTLINE) ? FATTR_SEL_OUTLINE : 0) |
           (checked(d, DID_UNDERSCORE) ? FATTR_SEL_UNDERSCORE : 0) |
           (checked(d, DID_STRIKEOUT) ? FATTR_SEL_STRIKEOUT : 0);
}

static double selected_points(PmFontDialog *d)
{
    char text[32] = {0};
    WinQueryDlgItemText(d->window, DID_SIZE, sizeof text, text);
    char *end;
    double points = strtod(text, &end);
    return *end || points < 1 || points > POINT_SIZE_MAX ? 0 : points;
}

static BOOL commit_font(PmFontDialog *d)
{
    double points = selected_points(d);
    if (d->current >= d->count || !points)
        return FALSE;
    unsigned pixels =
        (unsigned)(points * (GProcessState.display_dpi ? GProcessState.display_dpi : DEFAULT_DPI) /
                       (double)POINTS_PER_INCH +
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
        char family[FACESIZE];
        memcpy(family, m.family, n);
        family[n] = 0;
        if (!lx_execution_write(GProcessState.execution, d->data.pszFamilyname, family, n + 1))
            return FALSE;
    }
    FATTRS *f = &d->data.fAttrs;
    memset(f, 0, sizeof *f);
    f->usRecordLength = sizeof *f;
    d->data.flStyle = emphasis(d);
    f->fsSelection =
        (m.italic ? FATTR_SEL_ITALIC : 0) | (m.bold ? FATTR_SEL_BOLD : 0) | d->data.flStyle;
    d->data.flType = m.italic ? 1 : 0; /* FTYPE_ITALIC */
    memcpy(f->szFacename, m.face, sizeof f->szFacename);
    f->szFacename[sizeof f->szFacename - 1] = 0;
    f->usCodePage = FONT_CODEPAGE;
    f->lMaxBaselineExt = m.ascent + m.descent;
    f->lAveCharWidth = m.average_width;
    f->fsFontUse = m.sfnt ? 3 : 0;
    d->data.fxPointSize = (LONG)(points * FIXED_ONE + 0.5);
    d->data.sNominalPointSize = (SHORT)(points * DECIPOINTS_PER_POINT + 0.5);
    d->data.usWeight = m.bold ? FWEIGHT_BOLD : FWEIGHT_NORMAL;
    d->data.usWidth = FWIDTH_NORMAL;
    d->data.lEmHeight = m.em;
    d->data.lXHeight = m.x_height;
    d->data.lExternalLeading = m.leading;
    d->data.lReturn = DID_OK;
    d->data.lSRC = FNTS_SUCCESSFUL;
    return write_font(d);
}

/* Dialog procedures may be guest code, so strings and records travel in a guest mapping. */
static LxGuestMapping *font_mapping(void)
{
    return lx_execution_map(GProcessState.execution, 0, GUEST_PAGE_SIZE,
                            LX_GUEST_READ | LX_GUEST_WRITE, 0);
}

static MRESULT font_notify(PmFontDialog *d, LxGuestMapping *mapping, ULONG message,
                           const char *text, MPARAM second)
{
    char *guest = lx_execution_mapping_host(mapping);
    snprintf(guest, GUEST_PAGE_SIZE, "%s", text);
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
    snprintf(attribute, sizeof attribute, "%ld.%s",
             points >= 1 ? (long)points : (long)DEFAULT_POINT_SIZE, d->fonts[d->current].face);
    WinSetPresParam(preview, PP_FONTNAMESIZE, (ULONG)strlen(attribute) + 1, attribute);
    store_color(preview, PP_FOREGROUNDCOLORINDEX, d->data.clrFore);
    store_color(preview, PP_BACKGROUNDCOLORINDEX, d->data.clrBack);
    char *text = font_string(d->data.pszPreview);
    if (text)
        WinSetWindowText(preview, text);
    free(text);
}

static void refresh_preview(PmFontDialog *d)
{
    if (!d->filling)
        WinSendMsg(d->window, FNTM_UPDATEPREVIEW,
                   (MPARAM)(uintptr_t)WinWindowFromID(d->window, DID_SAMPLE), 0);
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
    WinSendDlgItemMsg(d->window, DID_STYLE, LM_DELETEALL, 0, 0);
    LONG row = 0, chosen = 0;
    int best = -1;
    for (size_t i = 0; i < d->count; i++)
    {
        const FontEntry *e = &d->fonts[i];
        if (strcmp(e->family, family))
            continue;
        insert_item(d->window, DID_STYLE, style_name(e));
        int score = (!e->bold == !bold) * 2 + (!e->italic == !italic);
        if (score > best)
        {
            best = score;
            chosen = row;
            d->current = i;
        }
        row++;
    }
    WinSendDlgItemMsg(d->window, DID_STYLE, LM_SELECTITEM, (MPARAM)(uintptr_t)chosen,
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
    lx_guest_store_u16(record + STYLECHANGE_USWEIGHT_OFFSET,
                       e->bold ? FWEIGHT_BOLD : FWEIGHT_NORMAL);
    lx_guest_store_u16(record + STYLECHANGE_USWEIGHTOLD_OFFSET,
                       old && old->bold ? FWEIGHT_BOLD : FWEIGHT_NORMAL);
    lx_guest_store_u16(record + STYLECHANGE_USWIDTH_OFFSET, FWIDTH_NORMAL);
    lx_guest_store_u16(record + STYLECHANGE_USWIDTHOLD_OFFSET, FWIDTH_NORMAL);
    lx_guest_store_u32(record + STYLECHANGE_FLTYPE_OFFSET, e->italic ? 1 : 0);
    lx_guest_store_u32(record + STYLECHANGE_FLTYPEOLD_OFFSET, old && old->italic ? 1 : 0);
    lx_guest_store_u32(record + STYLECHANGE_FLTYPEMASK_OFFSET, 1);
    lx_guest_store_u32(record + STYLECHANGE_FLTYPEMASKOLD_OFFSET, 1);
    lx_guest_store_u32(record + STYLECHANGE_FLSTYLE_OFFSET, style);
    lx_guest_store_u32(record + STYLECHANGE_FLSTYLEOLD_OFFSET, old_style);
    lx_guest_store_u32(record + STYLECHANGE_FLSTYLEMASK_OFFSET, EMPHASIS_MASK);
    lx_guest_store_u32(record + STYLECHANGE_FLSTYLEMASKOLD_OFFSET, EMPHASIS_MASK);
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
    static const ULONG lists[] = {DID_NAME, DID_STYLE, FOREGROUND_LIST, BACKGROUND_LIST};
    for (unsigned i = 0; i < 4; i++)
        WinSendDlgItemMsg(d->window, lists[i], LM_DELETEALL, 0, 0);
    free(d->fonts);
    d->fonts = NULL;
    d->count = d->current = 0;
    unsigned total = lx_host_font_count();
    if (total > FONT_CATALOGUE_LIMIT)
        total = FONT_CATALOGUE_LIMIT;
    d->fonts = calloc(total ? total : 1, sizeof *d->fonts);
    if (!d->fonts)
    {
        d->filling = FALSE;
        return;
    }
    char *family = font_string(d->data.pszFamilyname);
    LxGuestMapping *mapping = font_mapping();
    BOOL by_attributes = (d->data.fl & FNTS_INITFROMFATTRS) != 0;
    for (unsigned i = 0; i < total; i++)
    {
        LxHostFont *f = lx_host_font_open(i, METRICS_PIXEL_SIZE);
        LxHostFontMetrics m;
        BOOL ok = f && lx_host_font_metrics(f, &m);
        lx_host_font_close(f);
        if (!ok)
            continue;
        ULONG kind = (m.sfnt ? FNTI_VECTORFONT : FNTI_BITMAPFONT) |
                     (m.fixed ? FNTI_FIXEDWIDTHFONT : FNTI_PROPORTIONALFONT);
        if (mapping && (!font_notify(d, mapping, FNTM_FILTERLIST, m.family,
                                     (MPARAM)(uintptr_t)(FNTI_FAMILYNAME | kind << 16)) ||
                        !font_notify(d, mapping, FNTM_FILTERLIST, m.face,
                                     (MPARAM)(uintptr_t)(FNTI_STYLENAME | kind << 16))))
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
            insert_item(d->window, DID_NAME, d->fonts[i].family);
        BOOL match = by_attributes ? !strncmp(d->data.fAttrs.szFacename, d->fonts[i].face, FACESIZE)
                                   : family && !strcasecmp(family, d->fonts[i].family);
        if (match && !wanted)
            wanted = i + 1;
    }
    free(family);
    if (d->count)
    {
        const FontEntry *start = &d->fonts[wanted ? wanted - 1 : 0];
        unsigned bold = d->data.usWeight >= FWEIGHT_BOLD, italic = (d->data.flType & 1) != 0;
        if (by_attributes)
        {
            bold = start->bold;
            italic = start->italic;
        }
        LONG chosen = 0;
        for (size_t i = 0; i < (size_t)(start - d->fonts); i++)
            if (first_of_family(d, i) && strcmp(d->fonts[i].family, start->family))
                chosen++;
        WinSendDlgItemMsg(d->window, DID_NAME, LM_SELECTITEM, (MPARAM)(uintptr_t)chosen,
                          (MPARAM)(uintptr_t)TRUE);
        fill_styles(d, start->family, bold, italic);
    }
    /* pszPtSizeList is a space-separated list of sizes. */
    char *sizes = font_string(d->data.pszPtSizeList);
    const char *list = sizes ? sizes : "8 9 10 11 12 14 18 24";
    WinSendDlgItemMsg(d->window, DID_SIZE, LM_DELETEALL, 0, 0);
    while (*list)
    {
        char size[16];
        size_t n = strcspn(list, " ");
        if (n && n < sizeof size)
        {
            memcpy(size, list, n);
            size[n] = 0;
            insert_item(d->window, DID_SIZE, size);
        }
        list += n;
        list += strspn(list, " ");
    }
    free(sizes);
    char size[32];
    double points =
        d->data.fxPointSize > 0 ? d->data.fxPointSize / (double)FIXED_ONE : DEFAULT_POINT_SIZE;
    snprintf(size, sizeof size, "%.2f", points);
    WinSetDlgItemText(d->window, DID_SIZE, size);
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
    } checks[] = {{DID_OUTLINE, FATTR_SEL_OUTLINE},
                  {DID_UNDERSCORE, FATTR_SEL_UNDERSCORE},
                  {DID_STRIKEOUT, FATTR_SEL_STRIKEOUT}};

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
    if (id == DID_NAME && code == LN_SELECT)
    {
        LONG row = selected_item(d->window, DID_NAME);
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
    else if (id == DID_STYLE && code == LN_SELECT)
    {
        if (d->current >= d->count)
            return;
        FontEntry old = d->fonts[d->current];
        select_style(d, selected_item(d->window, DID_STYLE));
        send_style_change(d, &old, d->data.flStyle);
    }
    else if (id == DID_SIZE && (code == CBN_EFCHANGE || code == CBN_LBSELECT))
    {
        char text[32] = {0};
        WinQueryDlgItemText(d->window, DID_SIZE, sizeof text, text);
        double points = selected_points(d);
        LxGuestMapping *mapping = points ? font_mapping() : NULL;
        if (mapping)
        {
            font_notify(d, mapping, FNTM_POINTSIZECHANGED, text,
                        (MPARAM)(uintptr_t)(LONG)(points * FIXED_ONE + 0.5));
            lx_execution_unmap(GProcessState.execution, mapping);
        }
    }
    else if ((id == FOREGROUND_LIST || id == BACKGROUND_LIST) && code == LN_SELECT)
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
                   (MPARAM)(uintptr_t)(id == FOREGROUND_LIST ? CLRC_FOREGROUND : CLRC_BACKGROUND));
    }
    else if (id >= DID_OUTLINE && id <= DID_STRIKEOUT && code == BN_CLICKED)
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
        BOOL technology = ((kinds & FNTI_BITMAPFONT) && !(fl & FNTS_VECTORONLY)) ||
                          ((kinds & FNTI_VECTORFONT) && !(fl & FNTS_BITMAPONLY));
        BOOL pitch = ((kinds & FNTI_FIXEDWIDTHFONT) && !(fl & FNTS_PROPORTIONALONLY)) ||
                     ((kinds & FNTI_PROPORTIONALFONT) && !(fl & FNTS_FIXEDWIDTHONLY));
        BOOL synthesized = (kinds & FNTI_SYNTHESIZED) && !(fl & FNTS_NOSYNTHESIZEDFONTS);
        return (MRESULT)(uintptr_t)((fields & FNTI_POINTSIZE) ||
                                    ((fields & (FNTI_FAMILYNAME | FNTI_STYLENAME)) &&
                                     ((technology && pitch) || synthesized)));
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
        if (id == DID_OK || id == DID_APPLY_BUTTON)
        {
            if (commit_font(d) && id == DID_OK)
                WinDismissDlg(handle, DID_OK);
            return 0;
        }
        if (id == DID_CANCEL)
        {
            d->data.lReturn = DID_CANCEL;
            d->data.lSRC = FNTS_SUCCESSFUL;
            write_font(d);
            WinDismissDlg(handle, DID_CANCEL);
            return 0;
        }
        if (id == DID_RESET_BUTTON)
        {
            d->data = d->initial;
            populate_font(d);
            return 0;
        }
    }
    if (message == WM_CLOSE)
    {
        d->data.lReturn = DID_CANCEL;
        write_font(d);
        WinDismissDlg(handle, DID_CANCEL);
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
        WinShowWindow(WinWindowFromID(handle, DID_APPLY_BUTTON),
                      (d->data.fl & FNTS_APPLYBUTTON) != 0);
        WinShowWindow(WinWindowFromID(handle, DID_RESET_BUTTON),
                      (d->data.fl & FNTS_RESETBUTTON) != 0);
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

    enum
    {
        TEXT = PM_CLASS_STATIC,
        LIST = PM_CLASS_LISTBOX,
        COMBO = PM_CLASS_COMBOBOX,
        BUTTON = PM_CLASS_BUTTON
    };

    static const Item controls[] = {
        {DID_NAME_PREFIX, TEXT, 8, 203, 130, 12, TEXT_STYLE, "Name:"},
        {DID_NAME, LIST, 8, 116, 130, 85, LIST_STYLE, ""},
        {DID_STYLE_PREFIX, TEXT, 146, 203, 100, 12, TEXT_STYLE, "Style:"},
        {DID_STYLE, LIST, 146, 116, 100, 85, LIST_STYLE, ""},
        {DID_SIZE_PREFIX, TEXT, 254, 203, 70, 12, TEXT_STYLE, "Size:"},
        {DID_SIZE, COMBO, 254, 116, 70, 85, SIZE_STYLE, ""},
        {FOREGROUND_TEXT, TEXT, 8, 100, 90, 12, TEXT_STYLE, "Color:"},
        {FOREGROUND_LIST, LIST, 8, 48, 90, 50, LIST_STYLE, ""},
        {BACKGROUND_TEXT, TEXT, 104, 100, 90, 12, TEXT_STYLE, "Background:"},
        {BACKGROUND_LIST, LIST, 104, 48, 90, 50, LIST_STYLE, ""},
        {DID_OUTLINE, BUTTON, 202, 84, 80, 14, CHECK_STYLE, "Outline"},
        {DID_UNDERSCORE, BUTTON, 202, 68, 80, 14, CHECK_STYLE, "Underscore"},
        {DID_STRIKEOUT, BUTTON, 202, 52, 80, 14, CHECK_STYLE, "Strikeout"},
        {DID_SAMPLE, TEXT, 8, 26, 316, 20, TEXT_STYLE, "abcdABCD"},
        {DID_OK, BUTTON, 8, 6, 60, 14, DEFAULT_BUTTON_STYLE, "OK"},
        {DID_CANCEL, BUTTON, 74, 6, 60, 14, BUTTON_STYLE, "Cancel"},
        {DID_APPLY_BUTTON, BUTTON, 140, 6, 60, 14, BUTTON_STYLE, "Apply"},
        {DID_RESET_BUTTON, BUTTON, 206, 6, 60, 14, BUTTON_STYLE, "Reset"}};
    unsigned count = sizeof controls / sizeof *controls;
    size_t offset = TEMPLATE_ITEMS_OFFSET + (count + 1) * TEMPLATE_ITEM_SIZE;
    if (capacity < offset)
        return NULL;
    memset(bytes, 0, capacity);
    DLGTEMPLATE *t = (void *)bytes;
    t->codepage = TEMPLATE_CODEPAGE;
    t->offadlgti = TEMPLATE_ITEMS_OFFSET;
    t->iItemFocus = TEMPLATE_FOCUS_ITEM;
    DLGTITEM *items = (void *)(bytes + TEMPLATE_ITEMS_OFFSET);
    items[0].cChildren = count;
    items[0].offClassName = PM_CLASS_FRAME;
    items[0].id = DID_FONT_DIALOG;
    items[0].cx = FONT_DIALOG_WIDTH;
    items[0].cy = FONT_DIALOG_HEIGHT;
    items[0].flStyle = WS_VISIBLE;
    for (unsigned i = 0; i <= count; i++)
    {
        DLGTITEM *item = &items[i];
        item->offPresParams = item->offCtlData = TEMPLATE_NO_DATA;
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
    uint8 resource[TEMPLATE_BUFFER_SIZE];
    HWND handle =
        custom_font_template
            ? WinCreateDlg(parent, owner, font_dispatch, custom_font_template, address)
        : (d->data.fl & FNTS_CUSTOM)
            ? WinLoadDlg(parent, owner, font_dispatch, d->data.hMod, d->data.usDlgId, address)
            : WinCreateDlg(parent, owner, font_dispatch, font_template(resource, sizeof resource),
                           address);
    pending_font = previous;
    if (!handle)
    {
        d->data.lSRC = FNTS_ERR_DIALOG_LOAD_ERROR;
        write_font(d);
        release_font(d);
        return 0;
    }
    Window *w = getWindowFromHWND(a, handle);
    if (w && !(d->data.fl & FNTS_CUSTOM))
    {
        LONG x = d->data.x, y = d->data.y;
        if ((d->data.fl & FNTS_CENTER) || (!x && !y))
        {
            x = (desktop_window.w - w->w) / 2;
            y = (desktop_window.h - w->h) / 2;
        }
        WinSetWindowPos(handle, HWND_TOP, x, y, w->w, w->h, SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    if (d->data.fl & FNTS_MODELESS)
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

HWND WinFontDlg(HWND parent, HWND owner, PVOID data)
{
    return lx_pm_font_dialog(parent, owner, data);
}

MRESULT WinDefFontDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    return lx_pm_font_procedure(window, message, first, second);
}

LX_NATIVE_CONSTRUCTOR(font_dialog)
{
    lx_pm_add_window_destroy_hook(destroyStandardFont);
}
