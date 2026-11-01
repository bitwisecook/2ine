/* Public domain. No rights reserved. Wholly AI-authored. */
/* Push buttons, text/frame statics and text lists with stored selection state. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pm_text.h"

enum
{
    BM_CLICK = 0x120,
    BM_QUERYCHECK = 0x124,
    LM_QUERYITEMCOUNT = 0x160,
    LM_INSERTITEM = 0x161,
    LM_SETTOPINDEX = 0x162,
    LM_DELETEITEM = 0x163,
    LM_SELECTITEM = 0x164,
    LM_QUERYSELECTION = 0x165,
    LM_SETITEMTEXT = 0x166,
    LM_QUERYITEMTEXTLENGTH = 0x167,
    LM_QUERYITEMTEXT = 0x168,
    LM_SETITEMHANDLE = 0x169,
    LM_QUERYITEMHANDLE = 0x16a,
    LM_QUERYTOPINDEX = 0x16d,
    LM_DELETEALL = 0x16e,
};
typedef struct
{
    char *text;
    ULONG handle;
    BOOL selected;
} ListItem;
typedef struct PmControl
{
    unsigned kind;
    BOOL pressed;
    ListItem *items;
    size_t count;
    int selected, top;
} PmControl;
void destroyControl(Window *window)
{
    PmControl *control = window->control;
    if (!control)
        return;
    for (size_t i = 0; i < control->count; i++)
        free(control->items[i].text);
    free(control->items);
    free(control);
    window->control = NULL;
}
static void repaint(Window *window)
{
    postTimestampedMsg(getAnchorBlockNoHAB(), window->hwnd, WM_PAINT, 0, 0, currentSystemTicks());
}
static char *copy_text(MPARAM value)
{
    if (!value)
        return NULL;
    if (GLoaderState.execution)
        return lx_execution_copy_string(GLoaderState.execution, (uint32)(uintptr_t)value, 65536);
    return strdup(value);
}
static BOOL text_output(MPARAM value, const char *text, size_t capacity)
{
    size_t count = strlen(text);
    if (!value || !capacity)
        return FALSE;
    if (count >= capacity)
        count = capacity - 1;
    if (GLoaderState.execution)
        return lx_execution_write(GLoaderState.execution, (uint32)(uintptr_t)value, text, count) &&
               lx_execution_write(GLoaderState.execution, (uint32)(uintptr_t)value + (uint32)count,
                                  "", 1);
    memcpy(value, text, count);
    ((char *)value)[count] = 0;
    return TRUE;
}
static MRESULT reply(LONG value)
{
    return (MRESULT)(uintptr_t)(uint32)value;
}
static MRESULT list_message(Window *window, ULONG message, MPARAM first, MPARAM second)
{
    PmControl *c = window->control;
    int index = (SHORT)(uintptr_t)first;
    if (message == LM_QUERYITEMCOUNT) /* LM_QUERYITEMCOUNT */
        return reply((LONG)c->count);
    if (message == LM_DELETEALL) /* LM_DELETEALL */
    {
        for (size_t i = 0; i < c->count; i++)
            free(c->items[i].text);
        c->count = 0;
        c->selected = -1;
        c->top = 0;
        repaint(window);
        return reply(TRUE);
    }
    if (message == LM_INSERTITEM) /* LM_INSERTITEM: END=-1, SORTASCENDING=-2, SORTDESCENDING=-3 */
    {
        char *text = copy_text(second);
        if (!text)
            return reply(-2); /* LIT_MEMERROR */
        if (index == -1)
            index = (int)c->count;
        else if (index == -2 || index == -3)
        {
            BOOL ascending = index == -2;
            for (index = 0; (size_t)index < c->count; index++)
            {
                int comparison = strcmp(text, c->items[index].text);
                if (ascending ? comparison < 0 : comparison > 0)
                    break;
            }
        }
        if (index < 0 || (size_t)index > c->count || c->count >= INT16_MAX)
        {
            free(text);
            return reply(-3); /* LIT_ERROR */
        }
        ListItem *items = realloc(c->items, (c->count + 1) * sizeof *items);
        if (!items)
        {
            free(text);
            return reply(-2);
        }
        c->items = items;
        memmove(items + index + 1, items + index, (c->count - index) * sizeof *items);
        items[index] = (ListItem){text, 0, FALSE};
        c->count++;
        if (c->selected >= index)
            c->selected++;
        repaint(window);
        return reply(index);
    }
    if (message == LM_QUERYSELECTION) /* LM_QUERYSELECTION */
    {
        for (size_t i = index < 0 ? 0 : (size_t)index + 1; i < c->count; i++)
            if (c->items[i].selected)
                return reply((LONG)i);
        return reply(-1);
    }
    if (message == LM_SELECTITEM && index == -1)
    {
        for (size_t i = 0; i < c->count; i++)
            c->items[i].selected = FALSE;
        c->selected = -1;
        repaint(window);
        return reply(TRUE);
    }
    if (message == LM_QUERYTOPINDEX) /* LM_QUERYTOPINDEX */
        return reply(c->count ? c->top : -1);
    if (index < 0 || (size_t)index >= c->count)
        return reply(message == LM_SELECTITEM || message == LM_SETTOPINDEX ||
                             message == LM_SETITEMTEXT || message == LM_SETITEMHANDLE ||
                             message == LM_QUERYITEMHANDLE || message == LM_QUERYITEMTEXT
                         ? 0
                         : -3);
    switch (message)
    {
        case LM_SETTOPINDEX:
            c->top = index;
            break;
        case LM_DELETEITEM:
            free(c->items[index].text);
            memmove(c->items + index, c->items + index + 1,
                    (c->count - index - 1) * sizeof *c->items);
            c->count--;
            if (c->selected == index)
                c->selected = -1;
            else if (c->selected > index)
                c->selected--;
            if ((size_t)c->top >= c->count)
                c->top = 0;
            repaint(window);
            return reply((LONG)c->count);
        case LM_SELECTITEM:
            if (!(USHORT)(uintptr_t)second && !c->items[index].selected)
                return reply(FALSE);
            if (!(window->style & 0x11) && (uintptr_t)second)
                for (size_t i = 0; i < c->count; i++)
                    c->items[i].selected = FALSE;
            c->items[index].selected = (uintptr_t)second != 0;
            c->selected = (uintptr_t)second ? index : -1;
            break;
        case LM_SETITEMTEXT:
        {
            char *text = copy_text(second);
            if (!text)
                return reply(FALSE);
            free(c->items[index].text);
            c->items[index].text = text;
            break;
        }
        case LM_QUERYITEMTEXTLENGTH:
            return reply((LONG)strlen(c->items[index].text));
        case LM_QUERYITEMTEXT:
        {
            size_t capacity = ((uintptr_t)first >> 16) & 0xffff;
            size_t length = strlen(c->items[index].text);
            return reply(text_output(second, c->items[index].text, capacity)
                             ? (LONG)(length < capacity ? length : capacity - 1)
                             : 0);
        }
        case LM_SETITEMHANDLE:
            c->items[index].handle = (ULONG)(uintptr_t)second;
            return reply(TRUE);
        case LM_QUERYITEMHANDLE:
            return reply((LONG)c->items[index].handle);
        default:
            return reply(-3);
    }
    repaint(window);
    return reply(TRUE);
}
static void draw_text(Window *window, HPS space, const char *text, int baseline, BOOL center,
                      BOOL mnemonics, BOOL selected)
{
    LxHostFont *font = lx_pm_font(space);
    if (!font || !text)
        return;
    LxHostRect rect;
    calcHeavyweightRect(window, &rect);
    LxHostRect clip = rect;
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
    int x = rect.x + (center && width < rect.w ? (rect.w - width) / 2 : 3);
    uint8 color = selected ? 255 : window->enabled ? 0 : 110;
    for (const uint8 *p = (const uint8 *)text; *p; p++)
    {
        if (mnemonics && *p == '~' && p[1])
            p++;
        LxHostGlyph glyph;
        if (!lx_host_font_glyph(font, lx_pm_codepoint(*p), &glyph))
            continue;
        lx_host_window_mask(window->heavy.window, &clip, x + glyph.left,
                            rect.y + baseline - glyph.top, glyph.width, glyph.height,
                            glyph.coverage, color, color, color);
        x += glyph.advance;
        lx_host_glyph_free(&glyph);
    }
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
    WinFillRect(space, &rect, c->kind == 7 ? CLR_WHITE : CLR_PALEGRAY);
    if (c->kind == 3 || (c->kind == 5 && (window->style & 0x7f) != 1))
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
    if (c->kind == 7)
    {
        int height = metrics.ascent + metrics.descent;
        for (size_t i = (size_t)c->top; i < c->count; i++)
        {
            int top = (int)(i - c->top) * height;
            if (top >= window->h)
                break;
            if (c->items[i].selected)
            {
                RECTL row = {0, window->h - top - height, window->w, window->h - top};
                WinFillRect(space, &row, CLR_BLUE);
            }
            draw_text(window, space, c->items[i].text, top + metrics.ascent, FALSE, FALSE,
                      c->items[i].selected);
        }
    }
    else
        draw_text(window, space, window->text,
                  c->kind == 3 ? (window->h + metrics.ascent - metrics.descent) / 2
                               : metrics.ascent,
                  c->kind == 3, TRUE, FALSE);
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
        /* Owner drawing, bitmap statics and non-push-button classes are unavailable. */
        if ((kind == 3 && (window->style & 15)) || (kind == 7 && (window->style & 2)) ||
            (kind == 5 && (window->style & 0x7f) != 1 && (window->style & 0x7f) != 2 &&
             (window->style & 0x7f) != 8))
            return reply(TRUE);
        window->control = calloc(1, sizeof *window->control);
        if (!window->control)
            return reply(TRUE);
        window->control->kind = kind;
        window->control->selected = -1;
        return 0;
    }
    PmControl *c = window->control;
    if (!c)
        return 0;
    if (message == WM_PAINT)
    {
        paint(window);
        return 0;
    }
    if (c->kind == 7 && message >= LM_QUERYITEMCOUNT && message <= LM_DELETEALL)
        return list_message(window, message, first, second);
    if (c->kind == 3 && (message == WM_BUTTON1CLICK || message == BM_CLICK)) /* BM_CLICK */
    {
        if (window->enabled && window->owner)
            WinPostMsg(window->owner->hwnd, WM_COMMAND, (MPARAM)(uintptr_t)window->id,
                       (MPARAM)(uintptr_t)1); /* CMDSRC_PUSHBUTTON */
        return 0;
    }
    if (c->kind == 3 && message == BM_QUERYCHECK)
        return 0; /* BM_QUERYCHECK for push buttons */
    if (c->kind == 7 && message == WM_BUTTON1CLICK)
    {
        HPS space = WinGetPS(handle);
        LxHostFontMetrics metrics;
        BOOL valid = space && lx_host_font_metrics(lx_pm_font(space), &metrics);
        if (space)
            WinReleasePS(space);
        if (valid && metrics.ascent + metrics.descent > 0)
        {
            int y = (SHORT)((uintptr_t)first >> 16);
            int index = c->top + (window->h - 1 - y) / (metrics.ascent + metrics.descent);
            if (index >= 0 && (size_t)index < c->count)
            {
                c->selected = index;
                for (size_t i = 0; i < c->count; i++)
                    c->items[i].selected = (int)i == index;
                repaint(window);
                if (window->owner)
                    WinPostMsg(window->owner->hwnd, WM_CONTROL,
                               (MPARAM)(uintptr_t)(window->id | 1u << 16), 0); /* LN_SELECT */
            }
        }
        return 0;
    }
    return WinDefWindowProc(handle, message, first, second);
}
