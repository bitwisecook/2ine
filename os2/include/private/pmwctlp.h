/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private interfaces of the PM window controls (WC_*): the shared control state that PMCTLS's
   dialogs and PMMERGE's controls use. */
#ifndef _INCL_PMWCTLP_H_
#define _INCL_PMWCTLP_H_
#include "os2/include/private/pmwinp.h"

/* Ordinals of the built-in WC_* classes: a control's kind, the number in its "#n" class name and
   the class index of a dialog template item. */
enum
{
    PM_CLASS_FRAME = WC_FRAME & 0xffff,
    PM_CLASS_COMBOBOX = WC_COMBOBOX & 0xffff,
    PM_CLASS_BUTTON = WC_BUTTON & 0xffff,
    PM_CLASS_STATIC = WC_STATIC & 0xffff,
    PM_CLASS_ENTRYFIELD = WC_ENTRYFIELD & 0xffff,
    PM_CLASS_LISTBOX = WC_LISTBOX & 0xffff,
    PM_CLASS_SCROLLBAR = WC_SCROLLBAR & 0xffff,
    PM_CLASS_MLE = WC_MLE & 0xffff,
    PM_CLASS_SPINBUTTON = WC_SPINBUTTON & 0xffff
};

enum
{
    /* The static type is the style below SS_AUTOSIZE. */
    STATIC_TYPE_MASK = SS_AUTOSIZE - 1,
    COMBO_TYPE_MASK = CBS_SIMPLE | CBS_DROPDOWN | CBS_DROPDOWNLIST,
    /* Entry field and spin button text limits travel in a SHORT. */
    CONTROL_TEXT_LIMIT_MAX = INT16_MAX,
    /* WinDrawText and WinFillRect take colour indices, so an RGB colour is loaded into this
       logical colour table slot first. */
    CONTROL_RGB_COLOR_INDEX = 254,
    /* Pixels between an MLE's edge and its text; painting and line counting must agree. */
    MLE_TEXT_INSET = 2
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
    HPOINTER image;
    BOOL owns_image;
    LxHandleBorrow image_lease;
    BOOL pressed, changed, insert;
    unsigned check, limit, first, last, view;
    LONG lower, upper, value;
    LONG item_height;
    LONG drag_offset;
    ULONG button_draw_state;
    HWND edit, list;
    BOOL dropped, updating;
    MPARAM transfer;
    unsigned transfer_size, transfer_format;
    LONG popup_height;
    ListItem *items;
    size_t count;
    int selected, top;
} PmControl;

BOOL controlResourcePointer(HWND window, HMODULE module, ULONG identity);
void controlRepaint(Window *window);
void controlNotify(Window *window, USHORT code);
BOOL controlRead(MPARAM address, void *output, size_t size);
BOOL controlWrite(MPARAM address, const void *input, size_t size);
char *controlCopyText(MPARAM address);
BOOL controlTextOutput(MPARAM address, const char *text, size_t capacity);
MRESULT controlReply(LONG value);
MRESULT buttonMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
void paintUserButton(Window *window, HPS space);
void controlReplaceSelection(Window *window, const char *text, size_t bytes);
MRESULT mleMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
MRESULT entryMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
MRESULT spinMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
MRESULT comboMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
BOOL initializeCombo(Window *window);
BOOL initializeScrollbar(Window *window, MPARAM data);
MRESULT scrollbarMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
void controlTextChanged(Window *window);
MRESULT listMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
BOOL staticIsFrame(const Window *window);
void paintStaticShape(Window *window, HPS space);
void paintGroupBox(Window *window, HPS space, const LxHostFontMetrics *metrics);
#endif
