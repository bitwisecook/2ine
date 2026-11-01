/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard message boxes use resource-template controls and the PM modal loop. */
#include "os2libs/pmwin/internal.h"
#include "guest/abi.h"
ULONG WinMessageBox(HWND parent, HWND owner, PSZ text, PSZ caption, ULONG id, ULONG style)
{
    TRACE_NATIVE("WinMessageBox(%u, %u, '%s', '%s', %u, %u)", (uint)parent, (uint)owner, text,
                 caption, (uint)id, (uint)style);
    static const unsigned buttons[][3] = {{1, 0, 0}, {1, 2, 0}, {4, 2, 0}, {3, 4, 5},
                                          {6, 7, 0}, {6, 7, 2}, {2, 0, 0}, {9, 0, 0}};
    static const char *labels[] = {"",       "OK",  "Cancel", "Abort", "Retry",
                                   "Ignore", "Yes", "No",     "Help",  "Enter"};
    unsigned kind = style & 15;
    if (kind >= sizeof buttons / sizeof *buttons || (style & 0x8000))
        return 0xffff;
    if (!text)
        text = "";
    if (!caption)
        caption = "";
    if (strlen(text) > 8192 || strlen(caption) > 1024)
        return 0xffff;
    char lines[128][73];
    unsigned line_count = 0;
    const char *p = text;
    do
    {
        size_t length = strcspn(p, "\r\n");
        if (length > 72)
        {
            length = 72;
            while (length > 1 && p[length] != ' ')
                length--;
            if (length == 1)
                length = 72;
        }
        memcpy(lines[line_count], p, length);
        lines[line_count++][length] = 0;
        p += length;
        if (*p == '\r')
            p++;
        if (*p == '\n' || *p == ' ')
            p++;
    } while (*p && line_count < 128);
    if (*p)
        return 0xffff;
    unsigned button_count = 0;
    while (button_count < 3 && buttons[kind][button_count])
        button_count++;
    unsigned count = line_count + button_count;
    size_t offset = 14 + (count + 1) * 30;
    uint8 *bytes = calloc(1, 16384);
    if (!bytes)
        return 0xffff;
    DLGTEMPLATE *t = (void *)bytes;
    t->codepage = 850;
    t->offadlgti = 14;
    unsigned selected = (style >> 8) & 3;
    if (selected >= button_count)
        selected = 0;
    t->iItemFocus = 1 + line_count + selected;
    DLGTITEM *items = (void *)(bytes + 14);
    items[0].cChildren = count;
    items[0].offClassName = 1;
    items[0].id = id;
    items[0].x = 20;
    items[0].y = 30;
    items[0].cx = 330;
    items[0].cy = 40 + line_count * 12;
    items[0].flStyle = WS_VISIBLE;
    for (unsigned i = 0; i <= count; i++)
    {
        DLGTITEM *item = &items[i];
        item->offPresParams = item->offCtlData = 0xffff;
        const char *content = caption;
        if (i && i <= line_count)
        {
            item->offClassName = 5;
            item->id = 100 + i;
            item->x = 8;
            item->y = 26 + (line_count - i) * 12;
            item->cx = 314;
            item->cy = 12;
            item->flStyle = WS_VISIBLE | 1;
            content = lines[i - 1];
        }
        else if (i)
        {
            unsigned b = i - line_count - 1;
            item->offClassName = 3;
            item->id = buttons[kind][b];
            item->x = 165 - button_count * 28 + b * 56;
            item->y = 6;
            item->cx = 50;
            item->cy = 14;
            item->flStyle = WS_VISIBLE | 0x20000 | (b == selected ? 0x400 : 0);
            content = labels[item->id];
        }
        item->cchText = strlen(content);
        item->offText = offset;
        memcpy(bytes + offset, content, item->cchText + 1);
        offset += item->cchText + 1;
    }
    t->cbTemplate = offset;
    HWND window = WinCreateDlg(parent ? parent : HWND_DESKTOP, owner, WinDefDlgProc, t, NULL);
    free(bytes);
    if (!window)
        return 0xffff;
    ULONG result = WinProcessDlg(window);
    WinDestroyWindow(window);
    return result;
}

BOOL WinAlarm(HWND desktop, ULONG type)
{
    TRACE_NATIVE("WinAlarm(%u, %u)", (uint)desktop, (uint)type);
    return desktop == HWND_DESKTOP && type <= 2 && GLoaderState.beep &&
           GLoaderState.beep(type == 2 ? 220 : 440, 80) == NO_ERROR;
}
