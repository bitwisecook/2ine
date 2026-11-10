/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard message boxes use resource-template controls and the PM modal loop. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"

enum
{
    /* DLGTEMPLATE and DLGTITEM are packed to the resource layout. */
    DLGTEMPLATE_ITEMS_OFFSET = offsetof(DLGTEMPLATE, adlgti),
    DLGTITEM_SIZE = sizeof(DLGTITEM),
    DLGTITEM_NO_DATA = 0xffff,
    TEMPLATE_BUFFER_SIZE = 16384,
    MESSAGE_TEXT_LIMIT = 8192,
    CAPTION_TEXT_LIMIT = 1024,
    MESSAGE_LINE_CHARS = 72,
    MESSAGE_MAX_LINES = 128,
    MESSAGE_LINE_HEIGHT = 12, /* dialog units */
    ICON_ID = 99,
    TEXT_ID_BASE = 100
};

/* A box without a close action dismisses only through one of its buttons. */
static MRESULT OS2API no_close_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    if (message == WM_CLOSE || (message == WM_COMMAND && (USHORT)(uintptr_t)first == DID_CANCEL &&
                                !WinWindowFromID(window, MBID_CANCEL)))
        return 0;
    return WinDefDlgProc(window, message, first, second);
}

static ULONG message_box(HWND parent, HWND owner, PSZ text, PSZ caption, ULONG id, ULONG style,
                         BOOL closable)
{
    /* Indexed by the MB_OK..MB_ENTER type in the low style bits. */
    static const unsigned buttons[][3] = {{MBID_OK, 0, 0},
                                          {MBID_OK, MBID_CANCEL, 0},
                                          {MBID_RETRY, MBID_CANCEL, 0},
                                          {MBID_ABORT, MBID_RETRY, MBID_IGNORE},
                                          {MBID_YES, MBID_NO, 0},
                                          {MBID_YES, MBID_NO, MBID_CANCEL},
                                          {MBID_CANCEL, 0, 0},
                                          {MBID_ENTER, 0, 0}};
    /* Indexed by MBID_ value. */
    static const char *labels[] = {"",       "OK",  "Cancel", "Abort", "Retry",
                                   "Ignore", "Yes", "No",     "Help",  "Enter"};
    unsigned kind = style & 15;
    /* MB_ICONQUESTION, MB_ICONEXCLAMATION, MB_ICONASTERISK and MB_ICONHAND become 1 to 4. */
    unsigned icon = (style >> 4) & 7;
    if (kind >= sizeof buttons / sizeof *buttons || (style & MB_NONMODAL))
        return MBID_ERROR;
    if (!text)
        text = "";
    if (!caption)
        caption = "";
    if (strlen(text) > MESSAGE_TEXT_LIMIT || strlen(caption) > CAPTION_TEXT_LIMIT)
        return MBID_ERROR;
    char lines[MESSAGE_MAX_LINES][MESSAGE_LINE_CHARS + 1];
    unsigned line_count = 0;
    const char *p = text;
    do
    {
        size_t length = strcspn(p, "\r\n");
        if (length > MESSAGE_LINE_CHARS)
        {
            length = MESSAGE_LINE_CHARS;
            while (length > 1 && p[length] != ' ')
                length--;
            if (length == 1)
                length = MESSAGE_LINE_CHARS;
        }
        memcpy(lines[line_count], p, length);
        lines[line_count++][length] = 0;
        p += length;
        if (*p == '\r')
            p++;
        if (*p == '\n' || *p == ' ')
            p++;
    } while (*p && line_count < MESSAGE_MAX_LINES);
    if (*p)
        return MBID_ERROR;
    unsigned button_count = 0;
    while (button_count < 3 && buttons[kind][button_count])
        button_count++;
    unsigned count = line_count + button_count + (icon >= 1 && icon <= 4);
    size_t offset = DLGTEMPLATE_ITEMS_OFFSET + (count + 1) * DLGTITEM_SIZE;
    uint8 *bytes = calloc(1, TEMPLATE_BUFFER_SIZE);
    if (!bytes)
        return MBID_ERROR;
    DLGTEMPLATE *t = (void *)bytes;
    t->codepage = 850;
    t->offadlgti = DLGTEMPLATE_ITEMS_OFFSET;
    /* MB_DEFBUTTON1..MB_DEFBUTTON3 */
    unsigned selected = (style >> 8) & 3;
    if (selected >= button_count)
        selected = 0;
    t->iItemFocus = 1 + line_count + selected;
    DLGTITEM *items = (void *)(bytes + DLGTEMPLATE_ITEMS_OFFSET);
    /* A zero-length class name makes offClassName the predefined class index. */
    items[0].cChildren = count;
    items[0].offClassName = (USHORT)WC_FRAME;
    items[0].id = id;
    items[0].x = 20;
    items[0].y = 30;
    items[0].cx = 330;
    items[0].cy = 40 + line_count * MESSAGE_LINE_HEIGHT;
    items[0].flStyle = WS_VISIBLE;
    for (unsigned i = 0; i <= count; i++)
    {
        DLGTITEM *item = &items[i];
        item->offPresParams = item->offCtlData = DLGTITEM_NO_DATA;
        const char *content = caption;
        if (i && i <= line_count)
        {
            item->offClassName = (USHORT)WC_STATIC;
            item->id = TEXT_ID_BASE + i;
            item->x = icon >= 1 && icon <= 4 ? 32 : 8;
            item->y = 26 + (line_count - i) * MESSAGE_LINE_HEIGHT;
            item->cx = 322 - item->x;
            item->cy = MESSAGE_LINE_HEIGHT;
            item->flStyle = WS_VISIBLE | SS_TEXT;
            content = lines[i - 1];
        }
        else if (i && i <= line_count + button_count)
        {
            unsigned b = i - line_count - 1;
            item->offClassName = (USHORT)WC_BUTTON;
            item->id = buttons[kind][b];
            item->x = 165 - button_count * 28 + b * 56;
            item->y = 6;
            item->cx = 50;
            item->cy = 14;
            item->flStyle = WS_VISIBLE | WS_TABSTOP | (b == selected ? BS_DEFAULT : 0);
            content = labels[item->id];
        }
        else if (i)
        {
            item->offClassName = (USHORT)WC_STATIC;
            item->id = ICON_ID;
            item->x = 8;
            item->y = 26;
            item->cx = item->cy = 16;
            item->flStyle = WS_VISIBLE | SS_SYSICON;
            content = "";
        }
        item->cchText = strlen(content);
        item->offText = offset;
        memcpy(bytes + offset, content, item->cchText + 1);
        offset += item->cchText + 1;
    }
    t->cbTemplate = offset;
    HWND window = WinCreateDlg(parent ? parent : HWND_DESKTOP, owner,
                               closable ? WinDefDlgProc : no_close_procedure, t, NULL);
    free(bytes);
    if (!window)
        return MBID_ERROR;
    if (icon >= 1 && icon <= 4)
    {
        static const unsigned system[] = {0, SPTR_ICONQUESTION, SPTR_ICONWARNING,
                                          SPTR_ICONINFORMATION, SPTR_ICONERROR};
        controlResourcePointer(WinWindowFromID(window, ICON_ID), 0, system[icon]);
    }
    ULONG result = WinProcessDlg(window);
    WinDestroyWindow(window);
    return result;
}

ULONG WinMessageBox(HWND parent, HWND owner, PSZ text, PSZ caption, ULONG id, ULONG style)
{
    TRACE_NATIVE("WinMessageBox(%u, %u, '%s', '%s', %u, %u)", (uint)parent, (uint)owner, text,
                 caption, (uint)id, (uint)style);
    return message_box(parent, owner, text, caption, id, style, TRUE);
}

ULONG lx_pm_message_box_no_close(HWND owner, PSZ text, PSZ caption, ULONG style)
{
    return message_box(HWND_DESKTOP, owner, text, caption, 0, style, FALSE);
}

BOOL WinAlarm(HWND desktop, ULONG type)
{
    TRACE_NATIVE("WinAlarm(%u, %u)", (uint)desktop, (uint)type);
    /* SV_ALARM silences the sound without failing the call. */
    if (desktop == HWND_DESKTOP && type <= WA_ERROR && !WinQuerySysValue(desktop, SV_ALARM))
        return TRUE;
    /* The frequency and duration values follow the WA_ order. */
    return desktop == HWND_DESKTOP && type <= WA_ERROR && GProcessState.beep &&
           GProcessState.beep(WinQuerySysValue(desktop, SV_WARNINGFREQ + (LONG)type),
                              WinQuerySysValue(desktop, SV_WARNINGDURATION + (LONG)type)) ==
               NO_ERROR;
}
