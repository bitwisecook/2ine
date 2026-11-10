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

enum
{
    /* The dialog's colour grids: 6 rows of basic colours and 2 of custom colours, 8 a row. */
    COLOR_COLUMNS = 8,
    BASIC_COLOR_ROWS = 6,
    CUSTOM_COLOR_ROWS = 2,
    CUSTOM_COLORS = CUSTOM_COLOR_ROWS * COLOR_COLUMNS,
    /* CC_RGBINIT through CC_ENABLETEMPLATEHANDLE. */
    CHOOSECOLOR_FLAGS = 0x7F,
    /* COLORREF is 0x00BBGGRR; the high byte must be clear. */
    COLORREF_RGB_MASK = 0xFFFFFF,
    RGB_MAX = 255,
    /* Hue, saturation and luminosity run 0..HLS_MAX as in the Windows dialog; a grey has
       no hue and shows HUE_UNDEFINED. Hue sectors are a sixth of the circle. */
    HLS_MAX = 240,
    HUE_SIXTH = HLS_MAX / 6,
    HUE_UNDEFINED = HLS_MAX * 2 / 3,
    /* Frame colours of a grid cell: black around the current colour, grey elsewhere. */
    SELECTED_CELL_FRAME = 0x000000,
    CELL_FRAME = 0x808080,
    TEMPLATE_BUFFER_SIZE = 2048,
    COLORREF_BYTES = 4
};

static const ULONG basic_colors[BASIC_COLOR_ROWS * COLOR_COLUMNS] = {
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
    ULONG custom[CUSTOM_COLORS];
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
    *luminosity = (sum * HLS_MAX + RGB_MAX) / (2 * RGB_MAX);
    if (!range)
    {
        *hue = HUE_UNDEFINED;
        *saturation = 0;
        return;
    }
    *saturation = sum <= RGB_MAX
                      ? (range * HLS_MAX + sum / 2) / sum
                      : (range * HLS_MAX + (2 * RGB_MAX - sum) / 2) / (2 * RGB_MAX - sum);
    LONG sector = high == r   ? (g - b) * HUE_SIXTH / range
                  : high == g ? 2 * HUE_SIXTH + (b - r) * HUE_SIXTH / range
                              : 4 * HUE_SIXTH + (r - g) * HUE_SIXTH / range;
    *hue = (sector + HLS_MAX) % HLS_MAX;
}

static LONG hue_channel(LONG low, LONG high, LONG hue)
{
    hue = (hue + HLS_MAX) % HLS_MAX;
    if (hue < HUE_SIXTH)
        return low + (high - low) * hue / HUE_SIXTH;
    if (hue < 3 * HUE_SIXTH)
        return high;
    if (hue < 4 * HUE_SIXTH)
        return low + (high - low) * (4 * HUE_SIXTH - hue) / HUE_SIXTH;
    return low;
}

static ULONG hls_to_rgb(LONG hue, LONG luminosity, LONG saturation)
{
    LONG high = luminosity <= HLS_MAX / 2
                    ? luminosity * (HLS_MAX + saturation) / HLS_MAX
                    : luminosity + saturation - luminosity * saturation / HLS_MAX;
    LONG low = 2 * luminosity - high;
    LONG r = hue_channel(low, high, hue + 2 * HUE_SIXTH), g = hue_channel(low, high, hue);
    LONG b = hue_channel(low, high, hue - 2 * HUE_SIXTH);
    return (ULONG)(r * RGB_MAX / HLS_MAX) | (ULONG)(g * RGB_MAX / HLS_MAX) << 8 |
           (ULONG)(b * RGB_MAX / HLS_MAX) << 16;
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
    GpiCreateLogColorTable(space, 0, LCOLF_CONSECRGB, 0, 0, NULL);
    if (id == COLOR_SOLID)
        WinFillRect(space, &bounds, (LONG)swap_rgb(dialog->color));
    else
    {
        ULONG rows = id == COLOR_BOX1 ? BASIC_COLOR_ROWS : CUSTOM_COLOR_ROWS;
        LONG width = bounds.xRight / COLOR_COLUMNS, height = bounds.yTop / (LONG)rows;
        for (ULONG cell = 0; cell < rows * COLOR_COLUMNS; cell++)
        {
            ULONG color = id == COLOR_BOX1 ? basic_colors[cell] : swap_rgb(dialog->custom[cell]);
            LONG left = (LONG)(cell % COLOR_COLUMNS) * width;
            LONG top = bounds.yTop - (LONG)(cell / COLOR_COLUMNS) * height;
            RECTL frame = {left + 1, top - height + 1, left + width - 1, top - 1};
            WinFillRect(space, &frame,
                        color == swap_rgb(dialog->color) ? SELECTED_CELL_FRAME : CELL_FRAME);
            RECTL inner = {frame.xLeft + 2, frame.yBottom + 2, frame.xRight - 2, frame.yTop - 2};
            WinFillRect(space, &inner, (LONG)color);
        }
    }
    WinEndPaint(space);
}

static MRESULT OS2API color_cells(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    ColorDialog *dialog = color_dialog;
    ULONG id = WinQueryWindowUShort(window, QWS_ID);
    PFNWP original = dialog ? dialog->cells[cell_index(id)] : NULL;
    if (!dialog || !original)
        return WinDefWindowProc(window, message, first, second);
    if (message == WM_PAINT)
    {
        paint_cells(dialog, window, id);
        return 0;
    }
    if (message == WM_HITTEST)
        return (MRESULT)HT_NORMAL; /* A colour cell takes the click its static class passes on. */
    if (message == WM_BUTTON1DOWN && id != COLOR_SOLID)
    {
        RECTL bounds;
        ULONG point = (ULONG)(uintptr_t)first,
              rows = id == COLOR_BOX1 ? BASIC_COLOR_ROWS : CUSTOM_COLOR_ROWS;
        WinQueryWindowRect(window, &bounds);
        LONG x = (SHORT)(point & 0xFFFF), y = (SHORT)(point >> 16);
        if (bounds.xRight > 0 && bounds.yTop > 0 && x >= 0 && y >= 0 && x < bounds.xRight &&
            y < bounds.yTop)
        {
            ULONG column = (ULONG)x * COLOR_COLUMNS / (ULONG)bounds.xRight;
            ULONG row = (ULONG)(bounds.yTop - 1 - y) * rows / (ULONG)bounds.yTop;
            ULONG cell = row * COLOR_COLUMNS + column;
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
    uint8 wire[CUSTOM_COLORS * COLORREF_BYTES];
    for (unsigned i = 0; i < CUSTOM_COLORS; i++)
        lx_guest_store_u32(wire + i * COLORREF_BYTES, dialog->custom[i]);
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
    if (help && !(dialog->flags & WIN32_CC_SHOWHELP))
        WinShowWindow(help, FALSE);
    if (dialog->flags & WIN32_CC_PREVENTFULLOPEN)
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
            lx_execution_callback32_4(GProcessState.execution, dialog->hook, window,
                                      WIN32_WM_INITDIALOG, a, dialog->address);
        return 0;
    }
    if (message == dialog->set_message)
    {
        dialog->color = b & COLORREF_RGB_MASK;
        show_color(dialog, 0);
        return 0;
    }
    if (open32CommonDialogHook(dialog->hook, window, message, a, b))
        return 0;
    if (message == WM_CONTROL && (a >> 16) == EN_CHANGE && !dialog->updating)
    {
        ULONG id = a & 0xFFFF;
        if (id >= COLOR_RED && id <= COLOR_BLUE)
        {
            dialog->color = (ULONG)get_number(window, COLOR_RED, RGB_MAX) |
                            (ULONG)get_number(window, COLOR_GREEN, RGB_MAX) << 8 |
                            (ULONG)get_number(window, COLOR_BLUE, RGB_MAX) << 16;
            show_color(dialog, COLOR_RED);
        }
        else if (id >= COLOR_HUE && id <= COLOR_LUM)
        {
            dialog->color = hls_to_rgb(get_number(window, COLOR_HUE, HLS_MAX - 1),
                                       get_number(window, COLOR_LUM, HLS_MAX),
                                       get_number(window, COLOR_SAT, HLS_MAX));
            show_color(dialog, COLOR_HUE);
        }
        return 0;
    }
    if (message == WM_COMMAND)
    {
        ULONG id = a & 0xFFFF;
        if (id == WIN32_IDOK)
        {
            /* The hook sees the record with the proposed colour and may refuse it. */
            uint8 proposed[COLORREF_BYTES];
            lx_guest_store_u32(proposed, dialog->color);
            lx_execution_write(GProcessState.execution, dialog->address + WIN32_CHOOSECOLOR_RESULT,
                               proposed, sizeof proposed);
            if (dialog->hook &&
                lx_execution_callback32_4(GProcessState.execution, dialog->hook, window,
                                          dialog->ok_message, 0, dialog->address))
                return 0;
            dialog->accepted = TRUE;
            WinDismissDlg(window, DID_OK);
            return 0;
        }
        if (id == COLOR_ADD)
        {
            dialog->custom[dialog->custom_slot] = dialog->color;
            dialog->custom_slot = (dialog->custom_slot + 1) % CUSTOM_COLORS;
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

/* The built-in PM dialog template. DLGTITEM.offClassName holds the low word of a
   predefined WC_* class. */
enum
{
    DLGTEMPLATE_HEADER_SIZE = 14,
    TEMPLATE_CODEPAGE = 437,
    TEMPLATE_NO_OFFSET = 0xFFFF,
    CLASS_FRAME = WC_FRAME & 0xFFFF,
    CLASS_BUTTON = WC_BUTTON & 0xFFFF,
    CLASS_STATIC = WC_STATIC & 0xFFFF,
    CLASS_ENTRY = WC_ENTRYFIELD & 0xFFFF
};

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
        {CLASS_FRAME, 100, 0, 0, 296, 190, WS_VISIBLE, "Color"},
        {CLASS_STATIC, 730, 6, 176, 140, 10, WS_VISIBLE | SS_TEXT, "Basic colors:"},
        {CLASS_STATIC, COLOR_BOX1, 6, 86, 144, 90, WS_VISIBLE | SS_TEXT, ""},
        {CLASS_STATIC, 731, 6, 72, 140, 10, WS_VISIBLE | SS_TEXT, "Custom colors:"},
        {CLASS_STATIC, COLOR_CUSTOM1, 6, 40, 144, 32, WS_VISIBLE | SS_TEXT, ""},
        {CLASS_STATIC, COLOR_SOLID, 160, 122, 128, 54, WS_VISIBLE | SS_TEXT, ""},
        {CLASS_STATIC, 732, 160, 104, 30, 10, WS_VISIBLE | SS_TEXT, "Hue:"},
        {CLASS_ENTRY, COLOR_HUE, 192, 104, 28, 11, WS_VISIBLE | WS_TABSTOP | ES_MARGIN, ""},
        {CLASS_STATIC, 733, 160, 90, 30, 10, WS_VISIBLE | SS_TEXT, "Sat:"},
        {CLASS_ENTRY, COLOR_SAT, 192, 90, 28, 11, WS_VISIBLE | WS_TABSTOP | ES_MARGIN, ""},
        {CLASS_STATIC, 734, 160, 76, 30, 10, WS_VISIBLE | SS_TEXT, "Lum:"},
        {CLASS_ENTRY, COLOR_LUM, 192, 76, 28, 11, WS_VISIBLE | WS_TABSTOP | ES_MARGIN, ""},
        {CLASS_STATIC, 735, 226, 104, 30, 10, WS_VISIBLE | SS_TEXT, "Red:"},
        {CLASS_ENTRY, COLOR_RED, 258, 104, 28, 11, WS_VISIBLE | WS_TABSTOP | ES_MARGIN, ""},
        {CLASS_STATIC, 736, 226, 90, 30, 10, WS_VISIBLE | SS_TEXT, "Green:"},
        {CLASS_ENTRY, COLOR_GREEN, 258, 90, 28, 11, WS_VISIBLE | WS_TABSTOP | ES_MARGIN, ""},
        {CLASS_STATIC, 737, 226, 76, 30, 10, WS_VISIBLE | SS_TEXT, "Blue:"},
        {CLASS_ENTRY, COLOR_BLUE, 258, 76, 28, 11, WS_VISIBLE | WS_TABSTOP | ES_MARGIN, ""},
        {CLASS_BUTTON, COLOR_ADD, 160, 48, 126, 14, WS_VISIBLE | WS_TABSTOP,
         "Add to Custom Colors"},
        {CLASS_BUTTON, WIN32_IDOK, 6, 8, 56, 14, WS_VISIBLE | WS_TABSTOP | BS_DEFAULT, "OK"},
        {CLASS_BUTTON, WIN32_IDCANCEL, 68, 8, 56, 14, WS_VISIBLE | WS_TABSTOP, "Cancel"},
        {CLASS_BUTTON, COLOR_HELP, 130, 8, 56, 14, WS_VISIBLE | WS_TABSTOP, "Help"}};

    enum
    {
        count = sizeof controls / sizeof *controls,
        /* The OK button takes the initial focus. */
        focus_item = 19
    };

    DLGTEMPLATE *t = (void *)bytes;
    DLGTITEM *items = (void *)(bytes + DLGTEMPLATE_HEADER_SIZE);
    t->codepage = TEMPLATE_CODEPAGE;
    t->offadlgti = DLGTEMPLATE_HEADER_SIZE;
    t->iItemFocus = focus_item;
    size_t offset = DLGTEMPLATE_HEADER_SIZE + count * sizeof *items;
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
        item->offPresParams = item->offCtlData = TEMPLATE_NO_OFFSET;
        item->cchText = length;
        item->offText = offset;
        memcpy(bytes + offset, c->text, length + 1);
        offset += length + 1;
    }
    /* The frame's control data is its FCF_* flags. */
    if (offset + sizeof(ULONG) > capacity)
        return NULL;
    items[0].cChildren = count - 1;
    items[0].offCtlData = offset;
    lx_guest_store_u32(bytes + offset, FCF_TITLEBAR | FCF_SYSMENU);
    t->cbTemplate = offset + sizeof(ULONG);
    return t;
}

BOOL ChooseColor(PVOID record)
{
    if (!record || lx_guest_load_u32(record) != WIN32_CHOOSECOLOR_SIZE)
    {
        open32CommonDialogError(WIN32_CDERR_STRUCTSIZE);
        return FALSE;
    }
    uint8 *wire = record;
    ULONG flags = lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_FLAGS);
    if (flags & ~(ULONG)CHOOSECOLOR_FLAGS)
    {
        open32CommonDialogError(WIN32_CDERR_INITIALIZATION);
        return FALSE;
    }
    ColorDialog dialog = {
        .address = open32GuestRecordAddress(record, WIN32_CHOOSECOLOR_SIZE),
        .hook = flags & WIN32_CC_ENABLEHOOK ? lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_HOOK) : 0,
        .flags = flags,
        .owner = lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_OWNER),
        .custom_address = lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_CUSTOM_COLORS),
        .color = flags & WIN32_CC_RGBINIT
                     ? lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_RESULT) & COLORREF_RGB_MASK
                     : 0,
        .previous = color_dialog};
    if ((flags & WIN32_CC_ENABLEHOOK) && !open32ValidProcedure(dialog.hook))
    {
        open32CommonDialogError(WIN32_CDERR_NOHOOK);
        return FALSE;
    }
    if (dialog.custom_address)
    {
        uint8 custom[CUSTOM_COLORS * COLORREF_BYTES];
        if (!lx_execution_read(GProcessState.execution, dialog.custom_address, custom,
                               sizeof custom))
        {
            open32CommonDialogError(WIN32_CDERR_INITIALIZATION);
            return FALSE;
        }
        for (unsigned i = 0; i < CUSTOM_COLORS; i++)
            dialog.custom[i] = lx_guest_load_u32(custom + i * COLORREF_BYTES) & COLORREF_RGB_MASK;
    }
    if (!open32Presentation())
    {
        open32CommonDialogError(WIN32_CDERR_DIALOGFAILURE);
        return FALSE;
    }
    dialog.ok_message = RegisterWindowMessage("commdlg_ColorOK");
    dialog.set_message = RegisterWindowMessage("commdlg_SetRGBColor");
    dialog.help_message = RegisterWindowMessage("commdlg_help");
    uint8 bytes[TEMPLATE_BUFFER_SIZE] = {0};
    PVOID custom = NULL;
    ULONG window = 0, failure = 0, owner = dialog.owner;
    color_dialog = &dialog;
    if (flags & WIN32_CC_ENABLETEMPLATE)
    {
        ULONG module =
            open32DialogTemplateModule(lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_INSTANCE));
        ULONG identity = open32DialogTemplateIdentity(
            module, lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_TEMPLATE_NAME));
        if (identity)
            window = WinLoadDlg(HWND_DESKTOP, owner, color_procedure, module, identity, NULL);
        failure = WIN32_CDERR_FINDRESFAILURE;
    }
    else if (flags & WIN32_CC_ENABLETEMPLATEHANDLE)
    {
        ULONG memory = lx_guest_load_u32(wire + WIN32_CHOOSECOLOR_INSTANCE),
              locked = GlobalLock(memory);
        custom = locked ? open32ConvertDialog(locked) : NULL;
        if (locked)
            GlobalUnlock(memory);
        if (custom)
            window = WinCreateDlg(HWND_DESKTOP, owner, color_procedure, custom, NULL);
        failure = WIN32_CDERR_NOTEMPLATE;
    }
    else
    {
        PDLGTEMPLATE built = color_template(bytes, sizeof bytes);
        if (built)
            window = WinCreateDlg(HWND_DESKTOP, owner, color_procedure, built, NULL);
        failure = WIN32_CDERR_DIALOGFAILURE;
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
        lx_guest_store_u32(wire + WIN32_CHOOSECOLOR_RESULT, dialog.color);
        store_custom_colors(&dialog);
    }
    open32CommonDialogError(failure);
    return dialog.accepted;
}
