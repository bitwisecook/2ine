/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_CONTROLS_H
#define TWOINE_PM_CONTROLS_H
#include "os2libs/pmwin/internal.h"
typedef struct
{
    char *text;
    ULONG handle;
    BOOL selected;
} ListItem;
typedef struct PmControl
{
    unsigned kind;
    BOOL pressed, changed, insert;
    unsigned check, limit, first, last, view;
    LONG lower, upper, value;
    LONG item_height;
    LONG drag_offset;
    HWND edit, list;
    BOOL dropped, updating;
    MPARAM transfer;
    unsigned transfer_size, transfer_format;
    LONG popup_height;
    ListItem *items;
    size_t count;
    int selected, top;
} PmControl;

void controlRepaint(Window *window);
void controlNotify(Window *window, USHORT code);
BOOL controlRead(MPARAM address, void *output, size_t size);
BOOL controlWrite(MPARAM address, const void *input, size_t size);
char *controlCopyText(MPARAM address);
BOOL controlTextOutput(MPARAM address, const char *text, size_t capacity);
MRESULT controlReply(LONG value);
MRESULT buttonMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
void controlReplaceSelection(Window *window, const char *text, size_t bytes);
MRESULT mleMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
MRESULT entryMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
MRESULT spinMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
MRESULT comboMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
BOOL initializeCombo(Window *window);
BOOL initializeScrollbar(Window *window, MPARAM data);
MRESULT scrollbarMessage(Window *window, ULONG message, MPARAM first, MPARAM second);
void controlTextChanged(Window *window);
#endif
