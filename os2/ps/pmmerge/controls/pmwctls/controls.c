/* Public domain. No rights reserved. Wholly AI-authored. */
/* The WC_* control framework: one PmControl record per control, creation,
   painting and message dispatch to the control kinds. */
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/common/pm_text.h"
#include "os2/include/pmstddlg.h"

enum
{
    /* RGB of text in a disabled control. */
    DISABLED_TEXT_RGB = 0x6e6e6e,
    /* Longest guest string a control copies. */
    CONTROL_TEXT_COPY_MAX = 65536,
    /* Left margin of entry field, list box and static text. */
    TEXT_INSET = 3,
    /* Left margin of check box and radio button text, beside the box. */
    CHECK_TEXT_INSET = 20,
    /* Check boxes are drawn at most this size, with a 2-pixel margin. */
    CHECK_BOX_SIZE = 13,
    CHECK_BOX_MARGIN = 2,
    /* Rows of the triangle on a combo box's drop button. */
    COMBO_ARROW_ROWS = 4,
    /* Horizontal space an entry field's text may not use: its margins, and for a spin button
       also the arrows on the right. */
    ENTRY_TEXT_MARGINS = 4,
    SPIN_ARROWS_WIDTH = 18,
    /* A new spin button's range is 0 to this. */
    SPIN_DEFAULT_UPPER = 100,
    /* WinDrawText flags a static text style may hold beside SS_TEXT. */
    STATIC_TEXT_DT_FLAGS =
        DT_CENTER | DT_RIGHT | DT_VCENTER | DT_BOTTOM | DT_MNEMONIC | DT_WORDBREAK
};

void destroyControl(Window *window)
{
    PmControl *control = window->control;
    if (!control)
        return;
    lx_handle_release(&control->image_lease);
    if (control->owns_image)
    {
        if ((window->style & STATIC_TYPE_MASK) == SS_BITMAP)
            lx_pm_bitmap_delete(control->image);
        else
            WinDestroyPointer(control->image);
    }
    for (size_t i = 0; i < control->count; i++)
        free(control->items[i].text);
    free(control->items);
    free(control);
    window->control = NULL;
}

void controlRepaint(Window *window)
{
    WinInvalidateRect(window->hwnd, NULL, FALSE);
}

void controlNotify(Window *window, USHORT code)
{
    if (window->owner)
        WinSendMsg(window->owner->hwnd, WM_CONTROL,
                   (MPARAM)(uintptr_t)((USHORT)window->id | (ULONG)code << 16),
                   (MPARAM)(uintptr_t)window->hwnd);
}

BOOL controlRead(MPARAM address, void *output, size_t size)
{
    if (!address)
        return FALSE;
    if (TWOINE_PORTABLE_LOADER && GProcessState.execution && (uintptr_t)address <= UINT32_MAX)
        return lx_execution_read(GProcessState.execution, (uint32)(uintptr_t)address, output, size);
    memcpy(output, address, size);
    return TRUE;
}

BOOL controlWrite(MPARAM address, const void *input, size_t size)
{
    if (!address)
        return FALSE;
    if (TWOINE_PORTABLE_LOADER && GProcessState.execution && (uintptr_t)address <= UINT32_MAX)
        return lx_execution_write(GProcessState.execution, (uint32)(uintptr_t)address, input, size);
    memcpy(address, input, size);
    return TRUE;
}

void controlTextChanged(Window *window)
{
    PmControl *c = window->control;
    if (!c)
        return;
    c->first = c->last = window->text ? (unsigned)strlen(window->text) : 0;
    c->changed = TRUE;
    if (c->kind == PM_CLASS_ENTRYFIELD)
        controlNotify(window, EN_CHANGE);
    else if (c->kind == PM_CLASS_COMBOBOX && c->edit && !c->updating)
    {
        c->updating = TRUE;
        WinSetWindowText(c->edit, window->text ? window->text : "");
        c->updating = FALSE;
    }
    else if (c->kind == PM_CLASS_SPINBUTTON)
        controlNotify(window, SPBN_CHANGE);
}

char *controlCopyText(MPARAM value)
{
    if (!value)
        return NULL;
    if (TWOINE_PORTABLE_LOADER && GProcessState.execution && (uintptr_t)value <= UINT32_MAX)
        return lx_execution_copy_string(GProcessState.execution, (uint32)(uintptr_t)value,
                                        CONTROL_TEXT_COPY_MAX);
    return strdup(value);
}

BOOL controlTextOutput(MPARAM value, const char *text, size_t capacity)
{
    size_t count = strlen(text);
    if (!value || !capacity)
        return FALSE;
    if (count >= capacity)
        count = capacity - 1;
    if (TWOINE_PORTABLE_LOADER && GProcessState.execution && (uintptr_t)value <= UINT32_MAX)
        return lx_execution_write(GProcessState.execution, (uint32)(uintptr_t)value, text, count) &&
               lx_execution_write(GProcessState.execution, (uint32)(uintptr_t)value + (uint32)count,
                                  "", 1);
    memcpy(value, text, count);
    ((char *)value)[count] = 0;
    return TRUE;
}

MRESULT controlReply(LONG value)
{
    return (MRESULT)(uintptr_t)(uint32)value;
}

static void draw_text(Window *window, HPS space, const char *text, int baseline, BOOL center,
                      BOOL mnemonics, BOOL selected, int inset)
{
    LxHostFont *font = lx_pm_font(space);
    if (!font || !text || !windowAcceptsUpdates(window))
        return;
    LxHostRect rect;
    calcHeavyweightRect(window, &rect);
    LxHostRect clip = rect;
    PmRegion visible = {0};
    if (!lx_pm_window_clip(space, NULL, &visible))
        return;
    for (Window *parent = window->parent; parent && parent != &desktop_window;
         parent = parent->parent)
    {
        LxHostRect limit;
        calcHeavyweightRect(parent, &limit);
        int right = clip.x + clip.w < limit.x + limit.w ? clip.x + clip.w : limit.x + limit.w;
        int bottom = clip.y + clip.h < limit.y + limit.h ? clip.y + clip.h : limit.y + limit.h;
        if (clip.x < limit.x)
            clip.x = limit.x;
        if (clip.y < limit.y)
            clip.y = limit.y;
        clip.w = right > clip.x ? right - clip.x : 0;
        clip.h = bottom > clip.y ? bottom - clip.y : 0;
    }
    int width = 0;
    for (const uint8 *p = (const uint8 *)text; *p; p++)
    {
        if (mnemonics && *p == '~' && p[1])
            p++;
        int advance = lx_host_font_advance(font, lx_pm_codepoint(*p));
        if (advance > 0)
            width += advance;
    }
    int x = rect.x + (center && width < rect.w ? (rect.w - width) / 2 : inset);
    LONG foreground =
        windowPresentationColor(window, TRUE, window->enabled ? 0 : DISABLED_TEXT_RGB);
    for (const uint8 *p = (const uint8 *)text; *p; p++)
    {
        if (mnemonics && *p == '~' && p[1])
            p++;
        LxHostGlyph glyph;
        if (!lx_host_font_glyph(font, lx_pm_codepoint(*p), &glyph))
            continue;
        unsigned index = (unsigned)(p - (const uint8 *)text) + window->control->view;
        unsigned low = window->control->first < window->control->last ? window->control->first
                                                                      : window->control->last;
        unsigned high = window->control->first > window->control->last ? window->control->first
                                                                       : window->control->last;
        BOOL highlight = selected || (window->control->kind == PM_CLASS_ENTRYFIELD &&
                                      index >= low && index < high);
        LONG color = highlight ? RGB_WHITE : foreground;
        for (size_t i = 0; i < visible.count; i++)
        {
            RECTL part = visible.rects[i];
            LxHostRect drawing = {rect.x + part.xLeft, rect.y + rect.h - part.yTop,
                                  part.xRight - part.xLeft, part.yTop - part.yBottom};
            int left = drawing.x > clip.x ? drawing.x : clip.x;
            int top = drawing.y > clip.y ? drawing.y : clip.y;
            int right =
                drawing.x + drawing.w < clip.x + clip.w ? drawing.x + drawing.w : clip.x + clip.w;
            int bottom =
                drawing.y + drawing.h < clip.y + clip.h ? drawing.y + drawing.h : clip.y + clip.h;
            drawing = (LxHostRect){left, top, right > left ? right - left : 0,
                                   bottom > top ? bottom - top : 0};
            lx_host_window_mask(window->heavy.window, &drawing, x + glyph.left,
                                rect.y + baseline - glyph.top, glyph.width, glyph.height,
                                glyph.coverage, (color >> 16) & 255, (color >> 8) & 255,
                                color & 255);
        }
        x += glyph.advance;
        lx_host_glyph_free(&glyph);
    }
    free(visible.rects);
}

static void paint(Window *window)
{
    RECTL rect;
    HPS space = WinBeginPaint(window->hwnd, 0, &rect);
    if (!space)
        return;
    PmControl *c = window->control;
    LxHostFontMetrics metrics;
    if (!lx_host_font_metrics(lx_pm_font(space), &metrics))
    {
        WinEndPaint(space);
        return;
    }
    BOOL frame = c->kind == PM_CLASS_STATIC && staticIsFrame(window);
    if (!frame)
    {
        LONG color = windowPresentationColor(
            window, FALSE,
            WinQuerySysColor(HWND_DESKTOP,
                             (c->kind == PM_CLASS_LISTBOX || c->kind == PM_CLASS_ENTRYFIELD ||
                              c->kind == PM_CLASS_SPINBUTTON)
                                 ? SYSCLR_ENTRYFIELD
                                 : SYSCLR_DIALOGBACKGROUND,
                             0));
        if (lx_pm_color_table(space, 0, LCOLF_CONSECRGB, CONTROL_RGB_COLOR_INDEX, 1, &color))
            WinFillRect(space, &rect, CONTROL_RGB_COLOR_INDEX);
    }
    if (c->kind == PM_CLASS_COMBOBOX)
    {
        /* The edit child owns the selected text. Repeating it in the parent
           paints through the drop button, particularly for long drive labels. */
        if ((window->style & COMBO_TYPE_MASK) != CBS_SIMPLE)
        {
            LONG left = window->w > window->h ? window->w - window->h : 0;
            RECTL button = {left, 0, window->w, window->h};
            WinFillRect(space, &button, CLR_PALEGRAY);
            RECTL line = {left, 0, left + 1, window->h};
            WinFillRect(space, &line, CLR_DARKGRAY);
            LONG middle = left + (window->w - left) / 2;
            LONG top = window->h / 2 + 2;
            for (LONG row = 0; row < COMBO_ARROW_ROWS; row++)
            {
                RECTL arrow = {middle - 3 + row, top - row, middle + 4 - row, top - row + 1};
                WinFillRect(space, &arrow, CLR_BLACK);
            }
        }
        WinEndPaint(space);
        return;
    }
    if (c->kind == PM_CLASS_STATIC && (window->style & STATIC_TYPE_MASK) >= SS_FGNDRECT &&
        (window->style & STATIC_TYPE_MASK) <= SS_BKGNDFRAME)
    {
        paintStaticShape(window, space);
        WinEndPaint(space);
        return;
    }
    if (c->kind == PM_CLASS_STATIC && ((window->style & STATIC_TYPE_MASK) == SS_ICON ||
                                       (window->style & STATIC_TYPE_MASK) == SS_SYSICON))
    {
        LONG width, height;
        if (c->image && lx_pm_pointer_dimensions(c->image, &width, &height))
            lx_pm_pointer_draw(space, (window->w - width) / 2, (window->h - height) / 2, c->image);
        WinEndPaint(space);
        return;
    }
    if (c->kind == PM_CLASS_STATIC && (window->style & STATIC_TYPE_MASK) == SS_BITMAP)
    {
        PmBitmap *bitmap = c->image_lease.resource;
        if (bitmap)
        {
            POINTL at = {(window->w - (LONG)bitmap->width) / 2,
                         (window->h - (LONG)bitmap->height) / 2};
            WinDrawBitmap(space, c->image, NULL, &at, CLR_BLACK, CLR_WHITE, DBM_NORMAL);
        }
        WinEndPaint(space);
        return;
    }
    if (c->kind == PM_CLASS_STATIC && (window->style & STATIC_TYPE_MASK) == SS_GROUPBOX)
        paintGroupBox(window, space, &metrics);
    else if ((c->kind == PM_CLASS_BUTTON && !(window->style & BS_PRIMARYSTYLES)) ||
             c->kind == PM_CLASS_COMBOBOX || c->kind == PM_CLASS_ENTRYFIELD ||
             c->kind == PM_CLASS_SPINBUTTON ||
             (c->kind == PM_CLASS_STATIC && (window->style & STATIC_TYPE_MASK) != SS_TEXT))
    {
        RECTL edge = {0, 0, window->w, 1};
        WinFillRect(space, &edge, CLR_DARKGRAY);
        edge = (RECTL){0, 0, 1, window->h};
        WinFillRect(space, &edge, CLR_DARKGRAY);
        edge = (RECTL){0, window->h - 1, window->w, window->h};
        WinFillRect(space, &edge, CLR_WHITE);
        edge = (RECTL){window->w - 1, 0, window->w, window->h};
        WinFillRect(space, &edge, CLR_WHITE);
    }
    if (c->kind == PM_CLASS_BUTTON && (window->style & BS_PRIMARYSTYLES) == BS_USERBUTTON)
    {
        paintUserButton(window, space);
        WinEndPaint(space);
        return;
    }
    if (c->kind == PM_CLASS_BUTTON && (window->style & BS_PRIMARYSTYLES))
    {
        int size = window->h > CHECK_BOX_SIZE + CHECK_BOX_MARGIN ? CHECK_BOX_SIZE
                                                                 : window->h - CHECK_BOX_MARGIN;
        if (size > 2)
        {
            int bottom = (window->h - size) / 2;
            RECTL box = {CHECK_BOX_MARGIN, bottom, CHECK_BOX_MARGIN + size, bottom + size};
            WinFillRect(space, &box, CLR_BLACK);
            box = (RECTL){3, bottom + 1, size + 1, bottom + size - 1};
            WinFillRect(space, &box, CLR_WHITE);
            if (c->check)
                for (int i = 2; i < size - 2; i++)
                {
                    int y = c->check == 2 ? size / 2 : i;
                    RECTL tick = {2 + i, bottom + y, 3 + i, bottom + y + 1};
                    WinFillRect(space, &tick, CLR_BLACK);
                    if (c->check == 1)
                    {
                        tick = (RECTL){2 + i, bottom + size - 1 - i, 3 + i, bottom + size - i};
                        WinFillRect(space, &tick, CLR_BLACK);
                    }
                }
        }
    }
    if (c->kind == PM_CLASS_ENTRYFIELD || c->kind == PM_CLASS_SPINBUTTON)
    {
        const char *text = window->text ? window->text : "";
        size_t length = strlen(text);
        if (c->view > length)
            c->view = (unsigned)length;
        if (!(window->style & ES_AUTOSCROLL) || c->last < c->view)
            c->view = c->last < c->view ? c->last : 0;
        int cursor = TEXT_INSET;
        for (unsigned i = c->view; i < c->last && i < length; i++)
            cursor += lx_host_font_advance(lx_pm_font(space), lx_pm_codepoint((uint8)text[i]));
        int available =
            window->w - (c->kind == PM_CLASS_SPINBUTTON ? SPIN_ARROWS_WIDTH : ENTRY_TEXT_MARGINS);
        while ((window->style & ES_AUTOSCROLL) && cursor > available && c->view < c->last &&
               c->view < length)
            cursor -=
                lx_host_font_advance(lx_pm_font(space), lx_pm_codepoint((uint8)text[c->view++]));
        unsigned low = c->first < c->last ? c->first : c->last,
                 high = c->first > c->last ? c->first : c->last;
        int left = TEXT_INSET, right = TEXT_INSET;
        for (unsigned i = c->view; i < high && i < length; i++)
        {
            int width = lx_host_font_advance(lx_pm_font(space), lx_pm_codepoint((uint8)text[i]));
            if (i < low)
                left += width;
            right += width;
        }
        if (low != high)
        {
            RECTL selection = {left, 2, right, window->h - 2};
            WinFillRect(space, &selection, CLR_BLUE);
        }
        char *hidden = NULL;
        if (c->kind == PM_CLASS_ENTRYFIELD && (window->style & ES_UNREADABLE))
        {
            hidden = malloc(length + 1);
            if (hidden)
            {
                memset(hidden, '*', length);
                hidden[length] = 0;
                text = hidden;
            }
            else
                text = "";
        }
        draw_text(window, space, text + (c->view <= strlen(text) ? c->view : 0),
                  (window->h + metrics.ascent - metrics.descent) / 2, FALSE, FALSE, FALSE,
                  TEXT_INSET);
        free(hidden);
        if (WinQueryFocus(HWND_DESKTOP) == window->hwnd && low == high)
        {
            RECTL caret = {cursor, 2, cursor + 1, window->h - 2};
            WinFillRect(space, &caret, CLR_BLACK);
        }
    }
    else if (c->kind == PM_CLASS_MLE)
    {
        RECTL bounds = {MLE_TEXT_INSET, MLE_TEXT_INSET, window->w - MLE_TEXT_INSET,
                        window->h - MLE_TEXT_INSET};
        WinFillRect(space, &bounds, CLR_WHITE);
        WinDrawText(space, -1, window->text ? window->text : "", &bounds, CLR_BLACK, CLR_WHITE,
                    (window->style & MLS_WORDWRAP) ? DT_WORDBREAK : 0);
    }
    else if (c->kind == PM_CLASS_LISTBOX)
    {
        int height = metrics.ascent + metrics.descent;
        if (c->item_height > height)
            height = c->item_height;
        for (size_t i = (size_t)c->top; i < c->count; i++)
        {
            int top = (int)(i - c->top) * height;
            if (top >= window->h)
                break;
            if ((window->style & LS_OWNERDRAW) && window->owner)
            {
                OWNERITEM item = {window->hwnd,
                                  space,
                                  c->items[i].selected,
                                  0,
                                  0,
                                  0,
                                  {0, window->h - top - height, window->w, window->h - top},
                                  (LONG)i,
                                  c->items[i].handle};
                if (sendOwnerItem(window->owner, window->id, &item))
                    continue;
            }
            if (c->items[i].selected)
            {
                RECTL row = {0, window->h - top - height, window->w, window->h - top};
                WinFillRect(space, &row, CLR_BLUE);
            }
            draw_text(window, space, c->items[i].text, top + metrics.ascent, FALSE, FALSE,
                      c->items[i].selected, TEXT_INSET);
        }
    }
    else if (c->kind == PM_CLASS_STATIC && (window->style & STATIC_TYPE_MASK) == SS_TEXT &&
             (window->style & DT_WORDBREAK))
    {
        RECTL bounds = {0, 0, window->w, window->h};
        LONG color = windowPresentationColor(window, TRUE, window->enabled ? 0 : DISABLED_TEXT_RGB);
        if (lx_pm_color_table(space, 0, LCOLF_CONSECRGB, CONTROL_RGB_COLOR_INDEX, 1, &color))
            WinDrawText(space, -1, window->text ? window->text : "", &bounds,
                        CONTROL_RGB_COLOR_INDEX, 0, window->style & STATIC_TEXT_DT_FLAGS);
    }
    else if (!(c->kind == PM_CLASS_STATIC && ((window->style & STATIC_TYPE_MASK) == SS_ICON ||
                                              (window->style & STATIC_TYPE_MASK) == SS_BITMAP)))
        draw_text(window, space, window->text,
                  c->kind == PM_CLASS_BUTTON ? (window->h + metrics.ascent - metrics.descent) / 2
                                             : metrics.ascent,
                  c->kind == PM_CLASS_BUTTON && !(window->style & BS_PRIMARYSTYLES), TRUE, FALSE,
                  c->kind == PM_CLASS_BUTTON && (window->style & BS_PRIMARYSTYLES)
                      ? CHECK_TEXT_INSET
                      : TEXT_INSET);
    WinEndPaint(space);
}

MRESULT controlProcedure(HWND handle, ULONG message, MPARAM first, MPARAM second)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return 0;
    if (message == WM_CREATE)
    {
        unsigned kind = (unsigned)strtoul(window->window_class_name + 1, NULL, 10);
        /* These static and button styles require a different control representation. */
        if ((kind == PM_CLASS_BUTTON && (window->style & BS_PRIMARYSTYLES) > BS_USERBUTTON) ||
            (kind == PM_CLASS_STATIC && ((window->style & STATIC_TYPE_MASK) < SS_TEXT ||
                                         (window->style & STATIC_TYPE_MASK) > SS_SYSICON)))
            return controlReply(TRUE);
        window->control = calloc(1, sizeof *window->control);
        if (!window->control)
            return controlReply(TRUE);
        window->control->kind = kind;
        window->control->selected = -1;
        window->control->limit = CONTROL_TEXT_LIMIT_MAX;
        window->control->insert = WinQuerySysValue(HWND_DESKTOP, SV_INSERTMODE) != 0;
        window->control->upper = SPIN_DEFAULT_UPPER;
        window->control->first = window->control->last =
            window->text ? (unsigned)strlen(window->text) : 0;
        if (kind == PM_CLASS_SPINBUTTON && (!window->text || !*window->text))
        {
            free(window->text);
            window->text = strdup("0");
            if (!window->text)
                return controlReply(TRUE);
        }
        if (kind == PM_CLASS_COMBOBOX && !initializeCombo(window))
            return controlReply(TRUE);
        if (kind == PM_CLASS_SCROLLBAR && !initializeScrollbar(window, first))
            return controlReply(TRUE);
        return 0;
    }
    PmControl *c = window->control;
    if (c && c->kind == PM_CLASS_STATIC &&
        ((window->style & STATIC_TYPE_MASK) == SS_ICON ||
         (window->style & STATIC_TYPE_MASK) == SS_BITMAP ||
         (window->style & STATIC_TYPE_MASK) == SS_SYSICON))
    {
        if (message == SM_QUERYHANDLE)
            return controlReply(c->image);
        if (message == SM_SETHANDLE)
        {
            ULONG next = (ULONG)(uintptr_t)first;
            LxHandleBorrow image = {0};
            LONG width, height;
            BOOL bitmap = (window->style & STATIC_TYPE_MASK) == SS_BITMAP;
            if (next)
            {
                if (bitmap)
                {
                    PmBitmap *pixels = lx_pm_bitmap(next, &image);
                    if (!pixels)
                        return 0;
                    width = (LONG)pixels->width;
                    height = (LONG)pixels->height;
                }
                else if (!lx_pm_pointer_dimensions(next, &width, &height) ||
                         !lx_handle_borrow(next, LX_HANDLE_PM_POINTER, &image))
                    return 0;
            }
            ULONG previous = c->image;
            BOOL owned = c->owns_image;
            lx_handle_release(&c->image_lease);
            c->image_lease = image;
            c->image = next;
            c->owns_image = owned && previous == next;
            if (owned && previous != next)
            {
                if (bitmap)
                    lx_pm_bitmap_delete(previous);
                else
                    WinDestroyPointer(previous);
            }
            if (next && ((window->style & SS_AUTOSIZE) || !window->w || !window->h))
                WinSetWindowPos(handle, 0, 0, 0, width, height, SWP_SIZE);
            controlRepaint(window);
            return controlReply(previous);
        }
    }
    if (message == WM_HITTEST && c)
        return controlReply(c->kind == PM_CLASS_STATIC && staticIsFrame(window) ? HT_TRANSPARENT
                                                                                : HT_NORMAL);
    if (!c)
        return 0;
    if (c->kind == PM_CLASS_SCROLLBAR)
        return scrollbarMessage(window, message, first, second);
    if (message == WM_BUTTON1DOWN &&
        (c->kind == PM_CLASS_BUTTON || c->kind == PM_CLASS_ENTRYFIELD ||
         c->kind == PM_CLASS_LISTBOX || c->kind == PM_CLASS_MLE || c->kind == PM_CLASS_SPINBUTTON))
        WinSetFocus(HWND_DESKTOP, handle);
    if (message == WM_PAINT)
    {
        paint(window);
        return 0;
    }
    if (c->kind == PM_CLASS_MLE &&
        ((message >= MLM_SETTEXTLIMIT && message <= MLM_QUERYIMPORTEXPORT) || message == WM_CHAR ||
         message == WM_BUTTON1DOWN || message == WM_SETFOCUS))
        return mleMessage(window, message, first, second);
    if (c->kind == PM_CLASS_BUTTON)
        return buttonMessage(window, message, first, second);
    if (c->kind == PM_CLASS_ENTRYFIELD &&
        ((message >= EM_QUERYCHANGED && message <= EM_SETINSERTMODE) || message == WM_CHAR ||
         message == WM_BUTTON1DOWN || message == WM_SETFOCUS))
        return entryMessage(window, message, first, second);
    if (c->kind == PM_CLASS_SPINBUTTON &&
        ((message >= SPBM_OVERRIDESETLIMITS && message <= SPBM_SETMASTER) || message == WM_CHAR ||
         message == WM_BUTTON1DOWN))
        return spinMessage(window, message, first, second);
    if (c->kind == PM_CLASS_COMBOBOX &&
        ((message >= LM_QUERYITEMCOUNT && message <= CBM_ISLISTSHOWING) || message == WM_CONTROL ||
         message == WM_BUTTON1CLICK || message == WM_SIZE || message == WM_ADJUSTWINDOWPOS))
        return comboMessage(window, message, first, second);
    if (c->kind == PM_CLASS_LISTBOX && message >= LM_QUERYITEMCOUNT && message <= LM_DELETEALL)
        return listMessage(window, message, first, second);
    if (c->kind == PM_CLASS_LISTBOX && message == WM_BUTTON1DOWN)
    {
        HPS space = WinGetPS(handle);
        LxHostFontMetrics metrics;
        BOOL valid = space && lx_host_font_metrics(lx_pm_font(space), &metrics);
        if (space)
            WinReleasePS(space);
        if (valid && metrics.ascent + metrics.descent > 0)
        {
            int y = (SHORT)((uintptr_t)first >> 16);
            int height = metrics.ascent + metrics.descent;
            if (c->item_height > height)
                height = c->item_height;
            int index = c->top + (window->h - 1 - y) / height;
            if (index >= 0 && (size_t)index < c->count)
            {
                c->selected = index;
                for (size_t i = 0; i < c->count; i++)
                    c->items[i].selected = (int)i == index;
                controlRepaint(window);
                controlNotify(window, LN_SELECT);
            }
        }
        return 0;
    }
    return WinDefWindowProc(handle, message, first, second);
}

BOOL controlResourcePointer(HWND handle, HMODULE module, ULONG identity)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || !window->control || window->control->kind != PM_CLASS_STATIC)
        return FALSE;
    unsigned kind = window->style & STATIC_TYPE_MASK;
    if (kind != SS_ICON && kind != SS_BITMAP && kind != SS_SYSICON)
        return FALSE;
    HPS ps = kind == SS_BITMAP ? WinGetPS(handle) : 0;
    ULONG image = kind == SS_BITMAP    ? (ps ? lx_pm_bitmap_load(ps, module, identity, 0, 0) : 0)
                  : kind == SS_SYSICON ? WinQuerySysPointer(HWND_DESKTOP, identity, FALSE)
                                       : WinLoadPointer(HWND_DESKTOP, module, identity);
    if (ps)
        WinReleasePS(ps);
    if (!image)
        return FALSE;
    WinSendMsg(handle, SM_SETHANDLE, (MPARAM)(uintptr_t)image, 0);
    if (window->control->image != image)
    {
        if (kind == SS_BITMAP)
            lx_pm_bitmap_delete(image);
        else if (kind != SS_SYSICON)
            WinDestroyPointer(image);
        return FALSE;
    }
    window->control->owns_image = kind != SS_SYSICON;
    return TRUE;
}
