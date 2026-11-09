/* Public domain. No rights reserved. Wholly AI-authored. */
/* ChooseColor runs a PM colour dialog with the documented Windows hook and template paths and
   keeps Windows numbering at the guest edge. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/comdlg/common_dialogs.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/private/pmp.h"
#include <stdio.h>

/* Control numbers are those of the Windows colour dialog, so a guest template and a
   guest hook address the same controls as the built-in layout. */
enum
{
    COLOR_HUE = 703,
    COLOR_SAT = 704,
    COLOR_LUM = 705,
    COLOR_RED = 706,
    COLOR_GREEN = 707,
    COLOR_BLUE = 708,
    COLOR_SOLID = 709,
    COLOR_ADD = 712,
    COLOR_BOX1 = 720,
    COLOR_CUSTOM1 = 721,
    COLOR_HELP = 1038,
    COLOR_CELLS = 3
};

static const ULONG basic_colors[48] = {
    0xFF8080, 0xFFFF80, 0x80FF80, 0x00FF80, 0x80FFFF, 0x0080FF, 0xFF80C0, 0xFF80FF,
    0xFF0000, 0xFFFF00, 0x80FF00, 0x00FF40, 0x00FFFF, 0x0080C0, 0x8080C0, 0xFF00FF,
    0x804040, 0xFF8040, 0x00FF00, 0x008080, 0x004080, 0x8080FF, 0x800040, 0xFF0080,
    0x800000, 0xFF8000, 0x008000, 0x008040, 0x0000FF, 0x0000A0, 0x800080, 0x8000FF,
    0x400000, 0x804000, 0x004000, 0x004040, 0x000080, 0x000040, 0x400040, 0x400080,
    0x000000, 0x808000, 0x808040, 0x808080, 0x408080, 0xC0C0C0, 0x400040, 0xFFFFFF};

typedef struct ColorDialog
{
    ULONG address, hook, flags, owner, custom_address;
    ULONG color, ok_message, set_message, help_message, custom_slot;
    ULONG custom[16];
    PFNWP cells[COLOR_CELLS];
    ULONG window;
    BOOL accepted, updating;
    struct ColorDialog *previous;
} ColorDialog;

static _Thread_local ColorDialog *color_dialog;

/* COLORREF is 0x00BBGGRR, PM RGB is 0x00RRGGBB. */
static ULONG swap_rgb(ULONG color)
{
    return (color & 0xFF) << 16 | (color & 0xFF00) | ((color >> 16) & 0xFF);
}

/* The Windows dialog scales hue, saturation and luminosity to 0..240. */
static void rgb_to_hls(ULONG color, LONG *hue, LONG *luminosity, LONG *saturation)
{
    LONG r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
    LONG high = r > g ? (r > b ? r : b) : (g > b ? g : b);
    LONG low = r < g ? (r < b ? r : b) : (g < b ? g : b);
    LONG sum = high + low, range = high - low;
    *luminosity = (sum * 240 + 255) / 510;
    if (!range)
    {
        *hue = 160;
        *saturation = 0;
        return;
    }
    *saturation =
        sum <= 255 ? (range * 240 + sum / 2) / sum : (range * 240 + (510 - sum) / 2) / (510 - sum);
    LONG sector = high == r   ? (g - b) * 40 / range
                  : high == g ? 80 + (b - r) * 40 / range
                              : 160 + (r - g) * 40 / range;
    *hue = (sector + 240) % 240;
}

static LONG hue_channel(LONG low, LONG high, LONG hue)
{
    hue = (hue + 240) % 240;
    if (hue < 40)
        return low + (high - low) * hue / 40;
    if (hue < 120)
        return high;
    if (hue < 160)
        return low + (high - low) * (160 - hue) / 40;
    return low;
}

static ULONG hls_to_rgb(LONG hue, LONG luminosity, LONG saturation)
{
    LONG high = luminosity <= 120 ? luminosity * (240 + saturation) / 240
                                  : luminosity + saturation - luminosity * saturation / 240;
    LONG low = 2 * luminosity - high;
    LONG r = hue_channel(low, high, hue + 80), g = hue_channel(low, high, hue);
    LONG b = hue_channel(low, high, hue - 80);
    return (ULONG)(r * 255 / 240) | (ULONG)(g * 255 / 240) << 8 | (ULONG)(b * 255 / 240) << 16;
}

static void set_number(ULONG window, ULONG id, LONG value)
{
    char text[16];
    snprintf(text, sizeof text, "%d", value);
    WinSetDlgItemText(window, id, text);
}

static LONG get_number(ULONG window, ULONG id, LONG maximum)
{
    char text[16] = {0};
    WinQueryDlgItemText(window, id, sizeof text, text);
    long value = strtol(text, NULL, 10);
    return value < 0 ? 0 : value > maximum ? maximum : (LONG)value;
}

static void repaint_colors(ColorDialog *dialog)
{
    static const ULONG ids[] = {COLOR_BOX1, COLOR_CUSTOM1, COLOR_SOLID};
    for (unsigned i = 0; i < COLOR_CELLS; i++)
    {
        ULONG child = WinWindowFromID(dialog->window, ids[i]);
        if (child)
            WinInvalidateRect(child, NULL, FALSE);
    }
}

static void show_color(ColorDialog *dialog, ULONG changed_group)
{
    LONG hue, luminosity, saturation;
    ULONG color = dialog->color, window = dialog->window;
    rgb_to_hls(color, &hue, &luminosity, &saturation);
    dialog->updating = TRUE;
    if (changed_group != COLOR_HUE)
    {
        set_number(window, COLOR_HUE, hue);
        set_number(window, COLOR_SAT, saturation);
        set_number(window, COLOR_LUM, luminosity);
    }
    if (changed_group != COLOR_RED)
    {
        set_number(window, COLOR_RED, color & 0xFF);
        set_number(window, COLOR_GREEN, (color >> 8) & 0xFF);
        set_number(window, COLOR_BLUE, (color >> 16) & 0xFF);
    }
    dialog->updating = FALSE;
    repaint_colors(dialog);
}

static unsigned cell_index(ULONG id)
{
    return id == COLOR_BOX1 ? 0 : id == COLOR_CUSTOM1 ? 1 : 2;
}

static void paint_cells(ColorDialog *dialog, ULONG window, ULONG id)
{
    RECTL bounds;
    HPS space = WinBeginPaint(window, 0, NULL);
    if (!space)
        return;
    WinQueryWindowRect(window, &bounds);
    GpiCreateLogColorTable(space, 0, 2, 0, 0, NULL); /* LCOLF_RGB */
    if (id == COLOR_SOLID)
        WinFillRect(space, &bounds, (LONG)swap_rgb(dialog->color));
    else
    {
        ULONG rows = id == COLOR_BOX1 ? 6 : 2;
        LONG width = bounds.xRight / 8, height = bounds.yTop / (LONG)rows;
        for (ULONG cell = 0; cell < rows * 8; cell++)
        {
            ULONG color = id == COLOR_BOX1 ? basic_colors[cell] : swap_rgb(dialog->custom[cell]);
            LONG left = (LONG)(cell % 8) * width;
            LONG top = bounds.yTop - (LONG)(cell / 8) * height;
            RECTL frame = {left + 1, top - height + 1, left + width - 1, top - 1};
            WinFillRect(space, &frame, color == swap_rgb(dialog->color) ? 0 : 0x808080);
            RECTL inner = {frame.xLeft + 2, frame.yBottom + 2, frame.xRight - 2, frame.yTop - 2};
            WinFillRect(space, &inner, (LONG)color);
        }
    }
    WinEndPaint(space);
}

static MRESULT OS2API color_cells(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    ColorDialog *dialog = color_dialog;
    ULONG id = WinQueryWindowUShort(window, -1); /* QWS_ID */
    PFNWP original = dialog ? dialog->cells[cell_index(id)] : NULL;
    if (!dialog || !original)
        return WinDefWindowProc(window, message, first, second);
    if (message == WM_PAINT)
    {
        paint_cells(dialog, window, id);
        return 0;
    }
    if (message == WM_HITTEST)
        return 0; /* HT_NORMAL: a colour cell takes the click its static class would pass on. */
    if (message == WM_BUTTON1DOWN && id != COLOR_SOLID)
    {
        RECTL bounds;
        ULONG point = (ULONG)(uintptr_t)first, rows = id == COLOR_BOX1 ? 6 : 2;
        WinQueryWindowRect(window, &bounds);
        LONG x = (SHORT)(point & 0xFFFF), y = (SHORT)(point >> 16);
        if (bounds.xRight > 0 && bounds.yTop > 0 && x >= 0 && y >= 0 && x < bounds.xRight &&
            y < bounds.yTop)
        {
            ULONG column = (ULONG)x * 8 / (ULONG)bounds.xRight;
            ULONG row = (ULONG)(bounds.yTop - 1 - y) * rows / (ULONG)bounds.yTop;
            ULONG cell = row * 8 + column;
            if (id == COLOR_CUSTOM1)
                dialog->custom_slot = cell;
            dialog->color = id == COLOR_BOX1 ? swap_rgb(basic_colors[cell]) : dialog->custom[cell];
            show_color(dialog, 0);
        }
        return (MRESULT)(uintptr_t)TRUE;
    }
    return original(window, message, first, second);
}

static void store_custom_colors(ColorDialog *dialog)
{
    if (!dialog->custom_address)
        return;
    uint8 wire[64];
    for (unsigned i = 0; i < 16; i++)
        lx_guest_store_u32(wire + i * 4, dialog->custom[i]);
    lx_execution_write(GProcessState.execution, dialog->custom_address, wire, sizeof wire);
}

static void initialize_color(ColorDialog *dialog, ULONG window)
{
    static const ULONG ids[] = {COLOR_BOX1, COLOR_CUSTOM1, COLOR_SOLID};
    dialog->window = window;
    for (unsigned i = 0; i < COLOR_CELLS; i++)
    {
        ULONG child = WinWindowFromID(window, ids[i]);
        if (child)
            dialog->cells[i] = WinSubclassWindow(child, color_cells);
    }
    ULONG help = WinWindowFromID(window, COLOR_HELP);
    if (help && !(dialog->flags & CC_SHOWHELP))
        WinShowWindow(help, FALSE);
    if (dialog->flags & CC_PREVENTFULLOPEN)
    {
        static const ULONG editors[] = {COLOR_HUE,   COLOR_SAT,  COLOR_LUM, COLOR_RED,
                                        COLOR_GREEN, COLOR_BLUE, COLOR_ADD};
        for (unsigned i = 0; i < sizeof editors / sizeof *editors; i++)
        {
            ULONG child = WinWindowFromID(window, editors[i]);
            if (child)
                WinEnableWindow(child, FALSE);
        }
    }
    show_color(dialog, 0);
}

static MRESULT OS2API color_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    ColorDialog *dialog = color_dialog;
    ULONG a = (ULONG)(uintptr_t)first, b = (ULONG)(uintptr_t)second;
    if (!dialog)
        return WinDefDlgProc(window, message, first, second);
    if (message == WM_INITDLG)
    {
        initialize_color(dialog, window);
        if (dialog->hook)
            lx_execution_callback32_4(GProcessState.execution, dialog->hook, window, WIN_INITDIALOG,
                                      a, dialog->address);
        return 0;
    }
    if (message == dialog->set_message)
    {
        dialog->color = b & 0xFFFFFF;
        show_color(dialog, 0);
        return 0;
    }
    if (open32CommonDialogHook(dialog->hook, window, message, a, b))
        return 0;
    if (message == WM_CONTROL && (a >> 16) == EN_CHANGE_CODE && !dialog->updating)
    {
        ULONG id = a & 0xFFFF;
        if (id >= COLOR_RED && id <= COLOR_BLUE)
        {
            dialog->color = (ULONG)get_number(window, COLOR_RED, 255) |
                            (ULONG)get_number(window, COLOR_GREEN, 255) << 8 |
                            (ULONG)get_number(window, COLOR_BLUE, 255) << 16;
            show_color(dialog, COLOR_RED);
        }
        else if (id >= COLOR_HUE && id <= COLOR_LUM)
        {
            dialog->color =
                hls_to_rgb(get_number(window, COLOR_HUE, 239), get_number(window, COLOR_LUM, 240),
                           get_number(window, COLOR_SAT, 240));
            show_color(dialog, COLOR_HUE);
        }
        return 0;
    }
    if (message == WM_COMMAND)
    {
        ULONG id = a & 0xFFFF;
        if (id == 1)
        {
            /* The hook sees the record with the proposed colour and may refuse it. */
            uint8 proposed[4];
            lx_guest_store_u32(proposed, dialog->color);
            lx_execution_write(GProcessState.execution, dialog->address + 12, proposed, 4);
            if (dialog->hook &&
                lx_execution_callback32_4(GProcessState.execution, dialog->hook, window,
                                          dialog->ok_message, 0, dialog->address))
                return 0;
            dialog->accepted = TRUE;
            WinDismissDlg(window, 1);
            return 0;
        }
        if (id == COLOR_ADD)
        {
            dialog->custom[dialog->custom_slot] = dialog->color;
            dialog->custom_slot = (dialog->custom_slot + 1) % 16;
            store_custom_colors(dialog);
            repaint_colors(dialog);
            return 0;
        }
        if (id == COLOR_HELP)
        {
            if (dialog->owner)
                SendMessage(dialog->owner, dialog->help_message, window, dialog->address);
            return 0;
        }
    }
    return WinDefDlgProc(window, message, first, second);
}

typedef struct ColorControl
{
    USHORT kind, id;
    SHORT x, y, cx, cy;
    ULONG style;
    const char *text;
} ColorControl;

static PDLGTEMPLATE color_template(uint8 *bytes, size_t capacity)
{
    static const ColorControl controls[] = {
        {1, 100, 0, 0, 296, 190, WS_VISIBLE, "Color"},
        {5, 730, 6, 176, 140, 10, WS_VISIBLE | 1, "Basic colors:"},
        {5, COLOR_BOX1, 6, 86, 144, 90, WS_VISIBLE | 1, ""},
        {5, 731, 6, 72, 140, 10, WS_VISIBLE | 1, "Custom colors:"},
        {5, COLOR_CUSTOM1, 6, 40, 144, 32, WS_VISIBLE | 1, ""},
        {5, COLOR_SOLID, 160, 122, 128, 54, WS_VISIBLE | 1, ""},
        {5, 732, 160, 104, 30, 10, WS_VISIBLE | 1, "Hue:"},
        {6, COLOR_HUE, 192, 104, 28, 11, WS_VISIBLE | 0x20000 | 8, ""},
        {5, 733, 160, 90, 30, 10, WS_VISIBLE | 1, "Sat:"},
        {6, COLOR_SAT, 192, 90, 28, 11, WS_VISIBLE | 0x20000 | 8, ""},
        {5, 734, 160, 76, 30, 10, WS_VISIBLE | 1, "Lum:"},
        {6, COLOR_LUM, 192, 76, 28, 11, WS_VISIBLE | 0x20000 | 8, ""},
        {5, 735, 226, 104, 30, 10, WS_VISIBLE | 1, "Red:"},
        {6, COLOR_RED, 258, 104, 28, 11, WS_VISIBLE | 0x20000 | 8, ""},
        {5, 736, 226, 90, 30, 10, WS_VISIBLE | 1, "Green:"},
        {6, COLOR_GREEN, 258, 90, 28, 11, WS_VISIBLE | 0x20000 | 8, ""},
        {5, 737, 226, 76, 30, 10, WS_VISIBLE | 1, "Blue:"},
        {6, COLOR_BLUE, 258, 76, 28, 11, WS_VISIBLE | 0x20000 | 8, ""},
        {3, COLOR_ADD, 160, 48, 126, 14, WS_VISIBLE | 0x20000, "Add to Custom Colors"},
        {3, 1, 6, 8, 56, 14, WS_VISIBLE | 0x20000 | 0x400, "OK"},
        {3, 2, 68, 8, 56, 14, WS_VISIBLE | 0x20000, "Cancel"},
        {3, COLOR_HELP, 130, 8, 56, 14, WS_VISIBLE | 0x20000, "Help"}};

    enum
    {
        count = sizeof controls / sizeof *controls
    };

    DLGTEMPLATE *t = (void *)bytes;
    DLGTITEM *items = (void *)(bytes + 14);
    t->codepage = 437;
    t->offadlgti = 14;
    t->iItemFocus = 19;
    size_t offset = 14 + count * sizeof *items;
    for (unsigned i = 0; i < count; i++)
    {
        const ColorControl *c = controls + i;
        DLGTITEM *item = items + i;
        size_t length = strlen(c->text);
        if (offset + length + 1 > capacity)
            return NULL;
        item->offClassName = c->kind;
        item->id = c->id;
        item->x = c->x;
        item->y = c->y;
        item->cx = c->cx;
        item->cy = c->cy;
        item->flStyle = c->style;
        item->offPresParams = item->offCtlData = 0xFFFF;
        item->cchText = length;
        item->offText = offset;
        memcpy(bytes + offset, c->text, length + 1);
        offset += length + 1;
    }
    if (offset + 4 > capacity)
        return NULL;
    items[0].cChildren = count - 1;
    items[0].offCtlData = offset;
    lx_guest_store_u32(bytes + offset, 1 | 2); /* FCF_TITLEBAR, FCF_SYSMENU */
    t->cbTemplate = offset + 4;
    return t;
}

BOOL ChooseColor(PVOID record)
{
    if (!record || lx_guest_load_u32(record) != 36)
    {
        open32CommonDialogError(1);
        return FALSE;
    }
    uint8 *wire = record;
    ULONG flags = lx_guest_load_u32(wire + 20);
    if (flags & ~0x7Fu)
    {
        open32CommonDialogError(CDERR_INITIALIZATION);
        return FALSE;
    }
    ColorDialog dialog = {.address = open32GuestRecordAddress(record, 36),
                          .hook = flags & CC_ENABLEHOOK ? lx_guest_load_u32(wire + 28) : 0,
                          .flags = flags,
                          .owner = lx_guest_load_u32(wire + 4),
                          .custom_address = lx_guest_load_u32(wire + 16),
                          .color = flags & CC_RGBINIT ? lx_guest_load_u32(wire + 12) & 0xFFFFFF : 0,
                          .previous = color_dialog};
    if ((flags & CC_ENABLEHOOK) && !open32ValidProcedure(dialog.hook))
    {
        open32CommonDialogError(CDERR_NOHOOK);
        return FALSE;
    }
    if (dialog.custom_address)
    {
        uint8 custom[64];
        if (!lx_execution_read(GProcessState.execution, dialog.custom_address, custom,
                               sizeof custom))
        {
            open32CommonDialogError(CDERR_INITIALIZATION);
            return FALSE;
        }
        for (unsigned i = 0; i < 16; i++)
            dialog.custom[i] = lx_guest_load_u32(custom + i * 4) & 0xFFFFFF;
    }
    if (!open32Presentation())
    {
        open32CommonDialogError(0xFFFF);
        return FALSE;
    }
    dialog.ok_message = RegisterWindowMessage("commdlg_ColorOK");
    dialog.set_message = RegisterWindowMessage("commdlg_SetRGBColor");
    dialog.help_message = RegisterWindowMessage("commdlg_help");
    uint8 bytes[2048] = {0};
    PVOID custom = NULL;
    ULONG window = 0, failure = 0, owner = dialog.owner;
    color_dialog = &dialog;
    if (flags & CC_ENABLETEMPLATE)
    {
        ULONG module = open32DialogTemplateModule(lx_guest_load_u32(wire + 8));
        ULONG identity = open32DialogTemplateIdentity(module, lx_guest_load_u32(wire + 32));
        if (identity)
            window = WinLoadDlg(HWND_DESKTOP, owner, color_procedure, module, identity, NULL);
        failure = CDERR_FINDRESFAILURE;
    }
    else if (flags & CC_ENABLETEMPLATEHANDLE)
    {
        ULONG memory = lx_guest_load_u32(wire + 8), locked = GlobalLock(memory);
        custom = locked ? open32ConvertDialog(locked) : NULL;
        if (locked)
            GlobalUnlock(memory);
        if (custom)
            window = WinCreateDlg(HWND_DESKTOP, owner, color_procedure, custom, NULL);
        failure = CDERR_NOTEMPLATE;
    }
    else
    {
        PDLGTEMPLATE built = color_template(bytes, sizeof bytes);
        if (built)
            window = WinCreateDlg(HWND_DESKTOP, owner, color_procedure, built, NULL);
        failure = 0xFFFF;
    }
    free(custom);
    if (window)
    {
        failure = 0;
        WinProcessDlg(window);
        WinDestroyWindow(window);
    }
    color_dialog = dialog.previous;
    if (dialog.accepted)
    {
        lx_guest_store_u32(wire + 12, dialog.color);
        store_custom_colors(&dialog);
    }
    open32CommonDialogError(failure);
    return dialog.accepted;
}
