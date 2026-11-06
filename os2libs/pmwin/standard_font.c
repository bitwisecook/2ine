/* Public domain. No rights reserved. Wholly AI-authored. */
/* The standard font chooser enumerates actual host catalogue faces and writes
   the OS/2 FONTDLG/FATTRS records through checked execution spans. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/standard_dialogs.h"
#include "os2libs/pmctls.h"
#include "guest/abi.h"
#include <strings.h>
enum
{
    FNTM_FACENAMECHANGED = 0x1032, /* WM_USER + 50 */
    FNTM_UPDATEPREVIEW = 0x1036,
    FNTM_FILTERLIST = 0x1037
};
typedef struct PmFontDialog
{
    unsigned references;
    uint32 address;
    LXFONTDLG data, initial;
    HWND window;
    unsigned *fonts;
    size_t count;
} PmFontDialog;
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
    return lx_execution_write(GLoaderState.execution, d->address, &d->data, sizeof d->data);
}
static char *font_string(ULONG address)
{
    return address ? lx_execution_copy_string(GLoaderState.execution, address, 65536) : NULL;
}
static BOOL commit_font(PmFontDialog *d)
{
    LONG index = (SHORT)(uintptr_t)WinSendDlgItemMsg(d->window, 301, 0x165, 0, 0);
    if (index < 0 || (size_t)index >= d->count)
        return FALSE;
    char text[32];
    WinQueryDlgItemText(d->window, 305, sizeof text, text);
    char *end;
    double points = strtod(text, &end);
    if (*end || points < 1 || points > 32767)
        return FALSE;
    unsigned pixels =
        (unsigned)(points * (GLoaderState.display_dpi ? GLoaderState.display_dpi : 96) / 72.0 +
                   0.5);
    if (!pixels)
        pixels = 1;
    LxHostFont *font = lx_host_font_open(d->fonts[index], pixels);
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
        if (!lx_execution_write(GLoaderState.execution, d->data.pszFamilyname, family, n + 1))
            return FALSE;
    }
    FATTRS *f = &d->data.fAttrs;
    memset(f, 0, sizeof *f);
    f->usRecordLength = sizeof *f;
    f->fsSelection = (m.italic ? 1 : 0) | (m.bold ? 0x20 : 0);
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
/* Dialog procedures may be guest code, so strings travel in a guest mapping. */
static MRESULT font_notify(PmFontDialog *d, LxGuestMapping *mapping, ULONG message,
                           const char *text, MPARAM second)
{
    char *guest = lx_execution_mapping_host(mapping);
    snprintf(guest, 4096, "%s", text);
    return WinSendMsg(d->window, message,
                      (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping), second);
}
static void populate_font(PmFontDialog *d)
{
    WinSendDlgItemMsg(d->window, 301, 0x16e, 0, 0);
    free(d->fonts);
    d->fonts = NULL;
    d->count = 0;
    unsigned total = lx_host_font_count();
    if (total > 65536)
        total = 65536;
    d->fonts = malloc((total ? total : 1) * sizeof *d->fonts);
    if (!d->fonts)
        return;
    char *family = font_string(d->data.pszFamilyname);
    LONG selected = 0;
    LxGuestMapping *mapping =
        lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    for (unsigned i = 0; i < total; i++)
    {
        LxHostFont *f = lx_host_font_open(i, 16);
        LxHostFontMetrics m;
        BOOL ok = f && lx_host_font_metrics(f, &m);
        lx_host_font_close(f);
        if (!ok)
            continue;
        /* FNTI_FAMILYNAME with outline/bit-map and fixed/proportional kinds. */
        ULONG kind = (m.sfnt ? 2 : 1) | (m.fixed ? 4 : 8);
        if (mapping && !font_notify(d, mapping, FNTM_FILTERLIST, m.face,
                                    (MPARAM)(uintptr_t)(0x100 | kind << 16)))
            continue;
        if ((d->data.fl & 0x80) ? !strncmp(d->data.fAttrs.szFacename, m.face, 32)
                                : (family && !strcasecmp(family, m.family)))
            selected = (LONG)d->count;
        d->fonts[d->count++] = i;
        WinSendDlgItemMsg(d->window, 301, 0x161, (MPARAM)(intptr_t)-1, m.face);
    }
    free(family);
    if (mapping)
        lx_execution_unmap(GLoaderState.execution, mapping);
    WinSendDlgItemMsg(d->window, 301, 0x164, (MPARAM)(uintptr_t)selected, (MPARAM)(uintptr_t)TRUE);
    char size[32];
    double points = d->data.fxPointSize > 0 ? d->data.fxPointSize / 65536.0 : 10;
    snprintf(size, sizeof size, "%.2f", points);
    WinSetDlgItemText(d->window, 305, size);
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
    if (message == FNTM_FACENAMECHANGED || message == FNTM_UPDATEPREVIEW)
        return 0;
    if (message == WM_CONTROL && (USHORT)(uintptr_t)first == 301 &&
        (USHORT)((uintptr_t)first >> 16) == 1) /* LN_SELECT */
    {
        LONG index = (SHORT)(uintptr_t)WinSendDlgItemMsg(handle, 301, 0x165, 0, 0);
        LxHostFont *font =
            index >= 0 && (size_t)index < d->count ? lx_host_font_open(d->fonts[index], 16) : NULL;
        LxHostFontMetrics m;
        LxGuestMapping *mapping =
            font && lx_host_font_metrics(font, &m)
                ? lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0)
                : NULL;
        lx_host_font_close(font);
        if (mapping)
        {
            font_notify(d, mapping, FNTM_FACENAMECHANGED, m.face, 0);
            lx_execution_unmap(GLoaderState.execution, mapping);
        }
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
    static const Item controls[] = {{313, 5, 8, 163, 180, 12, 1, "Font and style:"},
                                    {301, 7, 8, 36, 200, 125, 0x20000, ""},
                                    {315, 5, 216, 163, 72, 12, 1, "Size:"},
                                    {305, 6, 216, 145, 72, 14, 0x20004, "10"},
                                    {1, 3, 216, 110, 72, 14, 0x20400, "OK"},
                                    {2, 3, 216, 90, 72, 14, 0x20000, "Cancel"},
                                    {311, 3, 216, 70, 72, 14, 0x20000, "Apply"},
                                    {312, 3, 216, 50, 72, 14, 0x20000, "Reset"}};
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
    items[0].cx = 300;
    items[0].cy = 188;
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
    if (!lx_execution_read(GLoaderState.execution, d->address, &d->data, sizeof d->data) ||
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
    uint8 resource[1024];
    HWND handle =
        (d->data.fl & 2)
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
