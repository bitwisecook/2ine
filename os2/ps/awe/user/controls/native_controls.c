/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows control messages and subclass chains retain the underlying PM controls. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmwctlp.h"

enum
{
    /* PM controls hold text in the PC code page; Windows callers use Windows Latin 1. */
    CONTROL_CODEPAGE = 437,
    /* Bounds on text copied in from a caller. */
    CONTROL_TEXT_MAX = 65536,
    WINDOW_TEXT_BUFFER_MAX = 1048576,
    /* Each 32-bit bridge stub occupies one slot; the window procedure stub follows the
       default one. A window procedure takes four 32-bit arguments. */
    BRIDGE_SLOT_SIZE = 256,
    WINDOW_PROCEDURE_ARGUMENT_BYTES = 16,
    /* Font sizes travel in points; Windows heights are pixels at 96 dpi. */
    DISPLAY_DPI = 96,
    POINTS_PER_INCH = 72,
    DEFAULT_POINT_SIZE = 8,
    /* Windows static types SS_LEFT through SS_LEFTNOWORDWRAP. */
    WINDOWS_STATIC_TYPE_MASK = 0x0F
};

typedef struct NativeControl
{
    Open32Window window;
    ULONG original, result, dialog_user, font;
    char class_name[64];
    BOOL subclassed;
    struct NativeControl *next;
} NativeControl;

typedef struct NativeDispatch
{
    ULONG handle, message, first, second;
    ULONG native_message, native_first, native_second;
    struct NativeDispatch *previous;
} NativeDispatch;

static _Thread_local NativeControl *controls;
static _Thread_local NativeDispatch *dispatch;
static _Thread_local LxGuestMapping *bridges;

ULONG open32ControlNotification(ULONG handle, ULONG packed)
{
    char name[16];
    if (!WinQueryClassName(handle, sizeof name, name))
        return packed;
    unsigned kind = name[0] == '#' ? (unsigned)strtoul(name + 1, NULL, 10) : 0;
    ULONG code = packed >> 16;
    if (kind == PM_CLASS_BUTTON)
        code = code == BN_CLICKED      ? WIN32_BN_CLICKED
               : code == BN_DBLCLICKED ? WIN32_BN_DOUBLECLICKED
                                       : code;
    else if (kind == PM_CLASS_ENTRYFIELD || kind == PM_CLASS_MLE)
        code = code == EN_SETFOCUS    ? WIN32_EN_SETFOCUS
               : code == EN_KILLFOCUS ? WIN32_EN_KILLFOCUS
               : (kind == PM_CLASS_ENTRYFIELD && code == EN_CHANGE) ||
                       (kind == PM_CLASS_MLE && code == MLN_CHANGE)
                   ? WIN32_EN_CHANGE
                   : code;
    else if (kind == PM_CLASS_COMBOBOX)
        code = code == CBN_EFCHANGE   ? WIN32_CBN_EDITCHANGE
               : code == CBN_LBSELECT ? WIN32_CBN_SELCHANGE
               : code == CBN_ENTER    ? WIN32_CBN_SELENDOK
               : code == CBN_SHOWLIST
                   ? (WinSendMsg(handle, CBM_ISLISTSHOWING, 0, 0) ? WIN32_CBN_DROPDOWN
                                                                  : WIN32_CBN_CLOSEUP)
                   : code;
    else if (kind == PM_CLASS_LISTBOX)
        code = code == LN_SETFOCUS    ? WIN32_LBN_SETFOCUS
               : code == LN_KILLFOCUS ? WIN32_LBN_KILLFOCUS
               : code == LN_ENTER     ? WIN32_LBN_DBLCLK
                                      : code;
    return (packed & 0xffff) | code << 16;
}

static NativeControl *find_control(ULONG handle, BOOL create)
{
    if (!WinIsWindow(0, handle))
        return NULL;
    for (NativeControl *control = controls; control; control = control->next)
        if (control->window.handle == handle)
            return control;
    if (!create || !open32NativeDialogWindow(handle))
        return NULL;
    NativeControl *control = calloc(1, sizeof *control);
    if (!control)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    control->window.handle = handle;
    control->window.instance = GProcessState.main_module ? GProcessState.main_module->handle : 0;
    control->original = WinQueryWindowULong(handle, QWP_PFNWP);
    control->next = controls;
    controls = control;
    return control;
}

Open32Window *open32PropertyWindow(ULONG handle)
{
    Open32Window *window = open32FindWindow(handle);
    NativeControl *control = window ? NULL : find_control(handle, TRUE);
    return window ? window : control ? &control->window : NULL;
}

static ULONG original_message(NativeControl *control, ULONG message, ULONG first, ULONG second)
{
    return lx_execution_callback32_4(GProcessState.execution, control->original,
                                     control->window.handle, message, first, second);
}

static LONG pm_message(ULONG handle, ULONG message, ULONG first, ULONG second, BOOL original)
{
    NativeControl *control = find_control(handle, FALSE);
    if (original && control)
        return (LONG)original_message(control, message, first, second);
    return (LONG)(intptr_t)WinSendMsg(handle, message, (MPARAM)(uintptr_t)first,
                                      (MPARAM)(uintptr_t)second);
}

static char *ansi_text(ULONG address)
{
    char *text = lx_execution_copy_string(GProcessState.execution, address, CONTROL_TEXT_MAX);
    if (text)
        for (char *p = text; *p; p++)
            *p = (char)WinCpTranslateChar(0, WIN32_CP_WINANSI, (UCHAR)*p, CONTROL_CODEPAGE);
    return text;
}

/* An application subclass sees message parameters as 32-bit guest values, so text
   exchanged with a control must live at a guest address for the whole call. */
static LxGuestMapping *guest_text(size_t size, ULONG *address, char **host)
{
    LxGuestMapping *mapping = lx_execution_map(
        GProcessState.execution, 0, (size + OPEN32_PAGE_SIZE - 1) & ~(size_t)(OPEN32_PAGE_SIZE - 1),
        LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (mapping)
    {
        *address = lx_execution_mapping_address(mapping);
        *host = lx_execution_mapping_host(mapping);
    }
    return mapping;
}

static LONG send_text(ULONG handle, ULONG message, ULONG first, const char *text)
{
    ULONG address;
    char *host;
    size_t size = strlen(text) + 1;
    LxGuestMapping *mapping = guest_text(size, &address, &host);
    if (!mapping)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    memcpy(host, text, size);
    LONG result = (LONG)(intptr_t)WinSendMsg(handle, message, (MPARAM)(uintptr_t)first,
                                             (MPARAM)(uintptr_t)address);
    lx_execution_unmap(GProcessState.execution, mapping);
    return result;
}

/* Adds the files matching a pattern. Read-write files are listed unless DDL_EXCLUSIVE
   asks for only the requested attributes; directories appear as [name]. */
static LONG list_directory(ULONG handle, ULONG attributes, ULONG pattern)
{
    char *name = lx_execution_copy_string(GProcessState.execution, pattern, WIN32_MAX_PATH);
    if (!name)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    uint8 found[WIN32_FIND_DATA_SIZE];
    ULONG search = FindFirstFile(name, found);
    free(name);
    if (search == UINT32_MAX)
        return -1;
    LONG last = -1;
    do
    {
        ULONG mode = lx_guest_load_u32(found + WIN32_FIND_DATA_FILE_ATTRIBUTES);
        const char *file = (const char *)found + WIN32_FIND_DATA_FILE_NAME;
        BOOL is_directory = (mode & WIN32_FILE_ATTRIBUTE_DIRECTORY) != 0;
        BOOL plain = !(mode & (WIN32_FILE_ATTRIBUTE_READONLY | WIN32_FILE_ATTRIBUTE_HIDDEN |
                               WIN32_FILE_ATTRIBUTE_SYSTEM | WIN32_FILE_ATTRIBUTE_DIRECTORY));
        /* The DDL_* attribute bits equal the FILE_ATTRIBUTE_* bits they select. */
        BOOL wanted =
            (mode & attributes &
             (WIN32_DDL_READONLY | WIN32_DDL_HIDDEN | WIN32_DDL_SYSTEM | WIN32_DDL_ARCHIVE)) ||
            (is_directory && (attributes & WIN32_DDL_DIRECTORY)) ||
            (plain && !(attributes & WIN32_DDL_EXCLUSIVE));
        if (!wanted || !strcmp(file, ".") || (is_directory && !(attributes & WIN32_DDL_DIRECTORY)))
            continue;
        char label[272];
        snprintf(label, sizeof label, is_directory ? "[%s]" : "%s", file);
        LONG index = send_text(handle, LM_INSERTITEM, (ULONG)LIT_END, label);
        if (index >= 0)
            last = index;
    } while (FindNextFile(search, found));
    FindClose(search);
    return last;
}

/* A control's font is a PM "points.face" parameter, inherited from its dialog. */
static ULONG control_font(ULONG handle)
{
    NativeControl *control = find_control(handle, TRUE);
    if (control && control->font)
        return control->font;
    char name[64] = {0};
    if (!WinQueryPresParam(handle, PP_FONTNAMESIZE, 0, NULL, sizeof name - 1, name, 0))
        return 0;
    char *face = strchr(name, '.');
    LONG points = (LONG)strtol(name, NULL, 10);
    if (!face || points <= 0)
        return 0;
    ULONG font = CreateFont(-(points * DISPLAY_DPI / POINTS_PER_INCH), 0, 0, 0, WIN32_FW_NORMAL, 0,
                            0, 0, 0, 0, 0, 0, 0, face + 1);
    if (control)
        control->font = font;
    return font;
}

static void set_control_font(ULONG handle, ULONG font, BOOL redraw)
{
    NativeControl *control = find_control(handle, TRUE);
    uint8 record[WIN32_LOGFONT_SIZE];
    if (control)
        control->font = font;
    if (!font || open32FontObject(font, sizeof record, record) <= 0)
        return;
    LONG height = (LONG)lx_guest_load_u32(record + WIN32_LOGFONT_HEIGHT);
    LONG points = (height < 0 ? -height : height) * POINTS_PER_INCH / DISPLAY_DPI;
    char name[64];
    int length = snprintf(name, sizeof name, "%d.%s", (int)(points ? points : DEFAULT_POINT_SIZE),
                          (const char *)record + WIN32_LOGFONT_FACE_NAME);
    if (length > 0 && (size_t)length < sizeof name)
        WinSetPresParam(handle, PP_FONTNAMESIZE, (ULONG)length + 1, name);
    if (redraw)
        WinInvalidateRect(handle, NULL, TRUE);
}

/* Copies one item into the caller's buffer, which Windows sizes from the text length. */
static LONG list_text(ULONG handle, ULONG index, ULONG address, BOOL original)
{
    LONG length = pm_message(handle, LM_QUERYITEMTEXTLENGTH, index, 0, original);
    /* The buffer size, NUL included, travels in the high word of LM_QUERYITEMTEXT's mp1. */
    if (length < 0 || length >= UINT16_MAX)
        return -1;
    if (!lx_execution_memory(GProcessState.execution, address, (size_t)length + 1, LX_GUEST_WRITE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    ULONG buffer;
    char *text;
    LxGuestMapping *mapping = guest_text((size_t)length + 1, &buffer, &text);
    if (!mapping)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    ULONG request = (index & 0xFFFF) | (ULONG)(length + 1) << 16;
    WinSendMsg(handle, LM_QUERYITEMTEXT, (MPARAM)(uintptr_t)request, (MPARAM)(uintptr_t)buffer);
    for (char *p = text; *p; p++)
        *p = (char)WinCpTranslateChar(0, CONTROL_CODEPAGE, (UCHAR)*p, WIN32_CP_WINANSI);
    lx_execution_write(GProcessState.execution, address, text, (size_t)length + 1);
    lx_execution_unmap(GProcessState.execution, mapping);
    return length;
}

LONG open32NativeControlMessage(ULONG handle, ULONG message, ULONG first, ULONG second,
                                BOOL original)
{
    TRACE_NATIVE("NativeControlMessage(%u, %x, %x, %x, %u)", (uint)handle, (uint)message,
                 (uint)first, (uint)second, (uint)original);
    NativeControl *control = find_control(handle, FALSE);
    if (original && control)
        for (NativeDispatch *packet = dispatch; packet; packet = packet->previous)
            if (packet->handle == handle && packet->message == message && packet->first == first &&
                packet->second == second)
            {
                ULONG flags = packet->native_first;
                if (packet->native_message == WM_CHAR)
                {
                    if (message == WIN32_WM_CHAR || message == WIN32_WM_SYSCHAR)
                        flags &= ~(ULONG)KC_VIRTUALKEY; /* Character delivery must not repeat
                                                           virtual-key actions. */
                    else
                        flags &= ~(ULONG)KC_CHAR; /* Keydown precedes the distinct Windows
                                                     character message. */
                }
                return (LONG)original_message(control, packet->native_message, flags,
                                              packet->native_second);
            }
    if (!original && control && control->subclassed)
        return (LONG)lx_execution_callback32_4(GProcessState.execution, control->window.procedure,
                                               handle, message, first, second);
    char name[16];
    if (!WinQueryClassName(handle, sizeof name, name))
        return 0;
    unsigned kind = name[0] == '#' ? (unsigned)strtoul(name + 1, NULL, 10) : 0;
    ULONG native = 0, a = first, b = second;
    if (message == WIN32_WM_SETTEXT)
    {
        char *text = ansi_text(second);
        BOOL result = text && WinSetWindowText(handle, text);
        free(text);
        return result;
    }
    if (message == WIN32_WM_GETTEXT)
    {
        if (!first || first > WINDOW_TEXT_BUFFER_MAX)
            return 0;
        char *text = calloc(1, first);
        if (!text)
            return 0;
        LONG result = WinQueryWindowText(handle, (LONG)first, text);
        for (LONG i = 0; i < result; i++)
            text[i] =
                (char)WinCpTranslateChar(0, CONTROL_CODEPAGE, (UCHAR)text[i], WIN32_CP_WINANSI);
        if (!lx_execution_write(GProcessState.execution, second, text, (size_t)result + 1))
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            result = 0;
        }
        free(text);
        return result;
    }
    if (message == WIN32_WM_GETTEXTLENGTH)
        return WinQueryWindowTextLength(handle);
    if (message == WIN32_WM_GETDLGCODE)
        native = WM_QUERYDLGCODE;
    else if (message == WIN32_WM_NCDESTROY)
        return 0;
    else if (message == WIN32_WM_CHAR || message == WIN32_WM_SYSCHAR)
    {
        native = WM_CHAR;
        /* One character with a repeat count of one. */
        a = KC_CHAR | 1u << 16 | (message == WIN32_WM_SYSCHAR ? KC_ALT : 0);
        b = (UCHAR)first;
        if (b >= ' ' && b != 0x7F) /* control characters need no code page translation */
            b = WinCpTranslateChar(0, WIN32_CP_WINANSI, (UCHAR)b, CONTROL_CODEPAGE);
    }
    else if (message == WIN32_WM_DESTROY)
        native = WM_DESTROY;
    else if (message == WIN32_WM_PAINT)
        native = WM_PAINT;
    else if (message == WIN32_WM_CLOSE)
        native = WM_CLOSE;
    else if (message == WIN32_WM_COMMAND)
    {
        native = second ? WM_CONTROL : WM_COMMAND;
        b = second;
    }
    else if ((kind == PM_CLASS_ENTRYFIELD || kind == PM_CLASS_MLE) && message == WIN32_EM_LIMITTEXT)
    {
        native = kind == PM_CLASS_MLE ? MLM_SETTEXTLIMIT : EM_SETTEXTLIMIT;
        a = first ? first : CONTROL_TEXT_LIMIT_MAX;
    }
    else if ((kind == PM_CLASS_ENTRYFIELD || kind == PM_CLASS_MLE) && message == WIN32_EM_SETSEL)
    {
        if (kind == PM_CLASS_MLE)
            native = MLM_QUERYTEXTLENGTH;
        else
        {
            native = EM_SETSEL;
            if (first == UINT32_MAX)
                a = b = WinQueryWindowTextLength(handle);
            else
            {
                a = (USHORT)first | (ULONG)(USHORT)second << 16;
                b = 0;
            }
        }
    }
    else if (kind == PM_CLASS_BUTTON && message >= WIN32_BM_GETCHECK && message <= WIN32_BM_CLICK)
    {
        if (message == WIN32_BM_GETCHECK)
            native = BM_QUERYCHECK;
        else if (message == WIN32_BM_SETCHECK)
            native = BM_SETCHECK;
        else if (message == WIN32_BM_GETSTATE)
            return (pm_message(handle, BM_QUERYCHECK, 0, 0, original) &
                    (WIN32_BST_CHECKED | WIN32_BST_INDETERMINATE)) |
                   (pm_message(handle, BM_QUERYHILITE, 0, 0, original) ? WIN32_BST_PUSHED : 0) |
                   (WinQueryFocus(HWND_DESKTOP) == handle ? WIN32_BST_FOCUS : 0);
        else if (message == WIN32_BM_SETSTATE)
            native = BM_SETHILITE;
        else if (message == WIN32_BM_CLICK)
            native = BM_CLICK;
        else
        {
            /* BM_SETSTYLE: the PM button style for each Windows BS_* type, or -1. */
            static const LONG styles[] = {BS_PUSHBUTTON,
                                          BS_PUSHBUTTON,
                                          BS_CHECKBOX,
                                          BS_AUTOCHECKBOX,
                                          BS_RADIOBUTTON,
                                          BS_3STATE,
                                          BS_AUTO3STATE,
                                          -1,
                                          -1,
                                          BS_AUTORADIOBUTTON,
                                          -1,
                                          BS_USERBUTTON};
            if (first >= sizeof styles / sizeof *styles || styles[first] < 0)
            {
                open32SetError(ERROR_NOT_SUPPORTED);
                return 0;
            }
            ULONG style = (ULONG)styles[first] | (first == WIN32_BS_DEFPUSHBUTTON ? BS_DEFAULT : 0);
            WinSetWindowULong(
                handle, QWL_STYLE,
                (WinQueryWindowULong(handle, QWL_STYLE) & ~(ULONG)(BS_PRIMARYSTYLES | BS_DEFAULT)) |
                    style);
            if (second)
                WinInvalidateRect(handle, NULL, FALSE);
            return 0;
        }
    }
    else if ((kind == PM_CLASS_COMBOBOX &&
              (message == WIN32_CB_ADDSTRING || message == WIN32_CB_INSERTSTRING)) ||
             (kind == PM_CLASS_LISTBOX &&
              (message == WIN32_LB_ADDSTRING || message == WIN32_LB_INSERTSTRING)))
    {
        char *text = ansi_text(second);
        if (!text)
            return -1;
        ULONG index =
            message == WIN32_CB_ADDSTRING || message == WIN32_LB_ADDSTRING ? (ULONG)LIT_END : first;
        LONG result = send_text(handle, LM_INSERTITEM, index, text);
        free(text);
        return result;
    }
    else if ((kind == PM_CLASS_COMBOBOX &&
              (message == WIN32_CB_FINDSTRING || message == WIN32_CB_FINDSTRINGEXACT ||
               message == WIN32_CB_SELECTSTRING)) ||
             (kind == PM_CLASS_LISTBOX &&
              (message == WIN32_LB_FINDSTRING || message == WIN32_LB_FINDSTRINGEXACT ||
               message == WIN32_LB_SELECTSTRING)))
    {
        char *text = ansi_text(second);
        if (!text)
            return -1;
        ULONG search = (message == WIN32_CB_FINDSTRINGEXACT || message == WIN32_LB_FINDSTRINGEXACT
                            ? LSS_CASESENSITIVE
                            : LSS_CASESENSITIVE | LSS_PREFIX) |
                       (ULONG)(USHORT)first << 16;
        LONG result = send_text(handle, LM_SEARCHSTRING, search, text);
        free(text);
        if (result < 0)
            return -1;
        if (message == WIN32_CB_SELECTSTRING || message == WIN32_LB_SELECTSTRING)
            pm_message(handle, LM_SELECTITEM, (ULONG)result, TRUE, original);
        return result;
    }
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_SETCURSEL) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_SETCURSEL))
    {
        LONG result =
            pm_message(handle, LM_SELECTITEM, first, first == UINT32_MAX ? FALSE : TRUE, original);
        return result && first != UINT32_MAX ? (LONG)first : -1;
    }
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_GETCURSEL) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_GETCURSEL))
        return pm_message(handle, LM_QUERYSELECTION, UINT32_MAX, 0, original);
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_GETCOUNT) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_GETCOUNT))
        return pm_message(handle, LM_QUERYITEMCOUNT, 0, 0, original);
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_RESETCONTENT) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_RESETCONTENT))
    {
        pm_message(handle, LM_DELETEALL, 0, 0, original);
        return 0;
    }
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_GETITEMDATA) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_GETITEMDATA))
        return pm_message(handle, LM_QUERYITEMHANDLE, first, 0, original);
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_SETITEMDATA) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_SETITEMDATA))
        return pm_message(handle, LM_SETITEMHANDLE, first, second, original) ? 0 : -1;
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_GETLBTEXT) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_GETTEXT))
        return list_text(handle, first, second, original);
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_GETLBTEXTLEN) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_GETTEXTLEN))
    {
        LONG length = pm_message(handle, LM_QUERYITEMTEXTLENGTH, first, 0, original);
        return length < 0 ? -1 : length;
    }
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_DELETESTRING) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_DELETESTRING))
    {
        LONG count = pm_message(handle, LM_DELETEITEM, first, 0, original);
        return count < 0 ? -1 : count;
    }
    else if ((kind == PM_CLASS_COMBOBOX && message == WIN32_CB_DIR) ||
             (kind == PM_CLASS_LISTBOX && message == WIN32_LB_DIR))
        return list_directory(handle, first, second);
    else if (kind == PM_CLASS_LISTBOX && message == WIN32_LB_GETTOPINDEX)
        return pm_message(handle, LM_QUERYTOPINDEX, 0, 0, original);
    else if (kind == PM_CLASS_LISTBOX && message == WIN32_LB_SETTOPINDEX)
        return pm_message(handle, LM_SETTOPINDEX, first, 0, original) ? 0 : -1;
    else if (kind == PM_CLASS_LISTBOX && message == WIN32_LB_SETHORIZONTALEXTENT)
        return 0; /* PM list boxes size their own extent. */
    else if ((kind == PM_CLASS_ENTRYFIELD || kind == PM_CLASS_MLE) &&
             message == WIN32_EM_GETLINECOUNT)
        return kind == PM_CLASS_MLE ? pm_message(handle, MLM_QUERYLINECOUNT, 0, 0, original) : 1;
    else if ((kind == PM_CLASS_ENTRYFIELD || kind == PM_CLASS_MLE) &&
             message == WIN32_EM_SETTABSTOPS)
        return 1; /* PM controls keep their uniform tab stops. */
    else if (message == WIN32_WM_NCCREATE || message == WIN32_WM_CREATE)
        return message == WIN32_WM_NCCREATE; /* The PM control already exists. */
    else if (message == WIN32_WM_GETFONT)
        return (LONG)control_font(handle);
    else if (message == WIN32_WM_SETFONT)
    {
        set_control_font(handle, first, second != 0);
        return 0;
    }
    else if (kind == PM_CLASS_COMBOBOX && message == WIN32_CB_LIMITTEXT)
    {
        /* The limit belongs to the combo box's entry field. */
        WinSendMsg(WinWindowFromID(handle, CBID_EDIT), EM_SETTEXTLIMIT, (MPARAM)(uintptr_t)first,
                   0);
        return 1;
    }
    else if (message == WIN32_WM_SETREDRAW)
    {
        /* PM controls repaint each change as it happens. */
        if (first)
            WinInvalidateRect(handle, NULL, TRUE);
        return 0;
    }
    else if (message >= WIN32_WM_USER)
        native = message - WIN32_WM_USER + WM_USER;
    if (!native)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    LONG result = pm_message(handle, native, a, b, original);
    return kind == PM_CLASS_BUTTON && message != WIN32_BM_GETCHECK &&
                   message >= WIN32_BM_GETCHECK && message <= WIN32_BM_CLICK
               ? 0
               : result;
}

static uint32 windows_default(uint8 *arguments)
{
    return (uint32)open32NativeControlMessage(
        lx_guest_load_u32(arguments), lx_guest_load_u32(arguments + 4),
        lx_guest_load_u32(arguments + 8), lx_guest_load_u32(arguments + 12), TRUE);
}

static uint32 native_dispatch(uint8 *arguments)
{
    NativeDispatch packet = {.handle = lx_guest_load_u32(arguments),
                             .native_message = lx_guest_load_u32(arguments + 4),
                             .native_first = lx_guest_load_u32(arguments + 8),
                             .native_second = lx_guest_load_u32(arguments + 12),
                             .previous = dispatch};
    NativeControl *control = find_control(packet.handle, FALSE);
    Open32Key key = {0};
    if (!control)
        return 0;
    if (packet.native_message == WM_DESTROY)
        packet.message = WIN32_WM_DESTROY;
    else if (packet.native_message == WM_QUERYDLGCODE)
        packet.message = WIN32_WM_GETDLGCODE;
    else if (packet.native_message == WM_CHAR)
    {
        open32DecodeKey(packet.native_first, packet.native_second, &key);
        open32UpdateKey(&key);
        packet.message = key.message;
        packet.first = key.key;
        packet.second = key.parameter;
    }
    else
    {
        QMSG native = {.hwnd = packet.handle,
                       .msg = packet.native_message,
                       .mp1 = (MPARAM)(uintptr_t)packet.native_first,
                       .mp2 = (MPARAM)(uintptr_t)packet.native_second};
        if (!open32PmMessage(&native, &packet.message, &packet.first, &packet.second))
            return original_message(control, packet.native_message, packet.native_first,
                                    packet.native_second);
    }
    dispatch = &packet;
    ULONG result =
        lx_execution_callback32_4(GProcessState.execution, control->window.procedure, packet.handle,
                                  packet.message, packet.first, packet.second);
    if (key.text && WinIsWindow(0, packet.handle))
    {
        packet.message = key.message == WIN32_WM_SYSKEYDOWN ? WIN32_WM_SYSCHAR : WIN32_WM_CHAR;
        packet.first = key.character;
        result =
            lx_execution_callback32_4(GProcessState.execution, control->window.procedure,
                                      packet.handle, packet.message, packet.first, packet.second);
    }
    dispatch = packet.previous;
    return result;
}

static BOOL prepare_bridges(void)
{
    if (bridges)
        return TRUE;
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                               LX_GUEST_READ | LX_GUEST_WRITE, 0);
    const LxExecutionApi32 defaults = {windows_default, WINDOW_PROCEDURE_ARGUMENT_BYTES, 0,
                                       LX_ABI_STACK32};
    const LxExecutionApi32 subclass = {native_dispatch, WINDOW_PROCEDURE_ARGUMENT_BYTES, 0,
                                       LX_ABI_STACK32};
    if (!mapping)
        return FALSE;
    uint8 *bytes = lx_execution_mapping_host(mapping);
    if (!lx_execution_emit_bridge32(GProcessState.execution, bytes, BRIDGE_SLOT_SIZE, &defaults) ||
        !lx_execution_emit_bridge32(GProcessState.execution, bytes + BRIDGE_SLOT_SIZE,
                                    BRIDGE_SLOT_SIZE, &subclass) ||
        !lx_execution_protect(GProcessState.execution, mapping, LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        lx_execution_unmap(GProcessState.execution, mapping);
        return FALSE;
    }
    bridges = mapping;
    return TRUE;
}

static ULONG button_style(ULONG style, unsigned *kind)
{
    switch (style & WIN32_BS_TYPEMASK)
    {
        case WIN32_BS_DEFPUSHBUTTON:
            return BS_DEFAULT;
        case WIN32_BS_CHECKBOX:
            return BS_CHECKBOX;
        case WIN32_BS_AUTOCHECKBOX:
            return BS_AUTOCHECKBOX;
        case WIN32_BS_RADIOBUTTON:
            return BS_RADIOBUTTON;
        case WIN32_BS_3STATE:
            return BS_3STATE;
        case WIN32_BS_AUTO3STATE:
            return BS_AUTO3STATE;
        case WIN32_BS_GROUPBOX:
            /* PM draws group boxes as static controls. */
            *kind = PM_CLASS_STATIC;
            return SS_GROUPBOX;
        case WIN32_BS_USERBUTTON:
        case WIN32_BS_OWNERDRAW:
            return BS_USERBUTTON;
        case WIN32_BS_AUTORADIOBUTTON:
            return BS_AUTORADIOBUTTON;
        default:
            return BS_PUSHBUTTON;
    }
}

static ULONG static_style(ULONG style)
{
    /* The PM static type and text alignment for each Windows SS_* type. */
    static const ULONG types[] = {SS_TEXT,
                                  SS_TEXT | DT_CENTER,
                                  SS_TEXT | DT_RIGHT,
                                  SS_ICON,
                                  SS_FGNDRECT,
                                  SS_HALFTONERECT,
                                  SS_BKGNDRECT,
                                  SS_FGNDFRAME,
                                  SS_HALFTONEFRAME,
                                  SS_BKGNDFRAME,
                                  SS_TEXT,
                                  SS_TEXT,
                                  SS_TEXT};
    ULONG type = style & WINDOWS_STATIC_TYPE_MASK;
    ULONG value = type < sizeof types / sizeof *types ? types[type] : SS_TEXT;
    if ((value & 0xff) == SS_TEXT && type != WIN32_SS_SIMPLE && type != WIN32_SS_LEFTNOWORDWRAP)
        value |= DT_WORDBREAK;
    if ((value & 0xff) == SS_TEXT && !(style & WIN32_SS_NOPREFIX))
        value |= DT_MNEMONIC;
    return value;
}

static ULONG control_style(ULONG style, unsigned *kind)
{
    ULONG value = (style & WIN32_WS_VISIBLE ? WS_VISIBLE : 0) |
                  (style & WIN32_WS_DISABLED ? WS_DISABLED : 0) |
                  (style & WIN32_WS_TABSTOP ? WS_TABSTOP : 0) |
                  (style & WIN32_WS_GROUP ? WS_GROUP : 0);
    if (*kind == PM_CLASS_BUTTON)
        return value | button_style(style, kind);
    if (*kind == PM_CLASS_STATIC)
        return value | static_style(style);
    if (*kind == PM_CLASS_MLE)
        return value | MLS_WORDWRAP | (style & WIN32_WS_BORDER ? MLS_BORDER : 0) |
               (style & WIN32_WS_VSCROLL ? MLS_VSCROLL : 0) |
               (style & WIN32_WS_HSCROLL ? MLS_HSCROLL : 0) |
               (style & WIN32_ES_READONLY ? MLS_READONLY : 0);
    if (*kind == PM_CLASS_ENTRYFIELD)
        return value | (style & (WIN32_ES_CENTER | WIN32_ES_RIGHT)) |
               (style & WIN32_ES_AUTOHSCROLL ? ES_AUTOSCROLL : 0) |
               (style & WIN32_WS_BORDER ? ES_MARGIN : 0) |
               (style & WIN32_ES_READONLY ? ES_READONLY : 0) |
               (style & WIN32_ES_PASSWORD ? ES_UNREADABLE : 0);
    if (*kind == PM_CLASS_LISTBOX)
        return value | (style & WIN32_LBS_MULTIPLESEL ? LS_MULTIPLESEL : 0) |
               (style & (WIN32_LBS_OWNERDRAWFIXED | WIN32_LBS_OWNERDRAWVARIABLE) ? LS_OWNERDRAW
                                                                                 : 0) |
               (style & WIN32_LBS_NOINTEGRALHEIGHT ? LS_NOADJUSTPOS : 0) |
               (style & WIN32_WS_HSCROLL ? LS_HORZSCROLL : 0) |
               (style & WIN32_LBS_EXTENDEDSEL ? LS_EXTENDEDSEL : 0);
    /* Combo boxes: CBS_SIMPLE and CBS_DROPDOWN share their values with PM. */
    ULONG type = style & (WIN32_CBS_SIMPLE | WIN32_CBS_DROPDOWN);
    return value | (type == WIN32_CBS_DROPDOWNLIST ? CBS_DROPDOWNLIST : type);
}

ULONG open32ControlStyle(ULONG style, unsigned *kind)
{
    return control_style(style, kind);
}

static unsigned control_kind(const char *name, ULONG style)
{
    if ((uintptr_t)name <= UINT16_MAX)
        return 0;
    if (!strcasecmp(name, "BUTTON"))
        return PM_CLASS_BUTTON;
    if (!strcasecmp(name, "EDIT"))
        return style & WIN32_ES_MULTILINE ? PM_CLASS_MLE : PM_CLASS_ENTRYFIELD;
    if (!strcasecmp(name, "MLE"))
        return PM_CLASS_MLE;
    if (!strcasecmp(name, "STATIC"))
        return PM_CLASS_STATIC;
    if (!strcasecmp(name, "LISTBOX"))
        return PM_CLASS_LISTBOX;
    return !strcasecmp(name, "COMBOBOX") ? PM_CLASS_COMBOBOX : 0;
}

/* A superclass keeps its own name; otherwise the PM class gives the standard name. */
void open32SetNativeClassName(ULONG handle, const char *name)
{
    NativeControl *control = find_control(handle, TRUE);
    if (control)
        snprintf(control->class_name, sizeof control->class_name, "%s", name);
}

const char *open32NativeClassName(ULONG handle)
{
    NativeControl *control = find_control(handle, TRUE);
    if (control && control->class_name[0])
        return control->class_name;
    char name[16];
    if (!WinQueryClassName(handle, sizeof name, name) || name[0] != '#')
        return NULL;
    switch (strtoul(name + 1, NULL, 10))
    {
        case PM_CLASS_COMBOBOX:
            return "COMBOBOX";
        case PM_CLASS_BUTTON:
            return "BUTTON";
        case PM_CLASS_STATIC:
            return "STATIC";
        case PM_CLASS_ENTRYFIELD:
            return "EDIT";
        case PM_CLASS_LISTBOX:
            return "LISTBOX";
        case PM_CLASS_SCROLLBAR:
            return "SCROLLBAR";
        case PM_CLASS_MLE:
            return "MLE";
        default:
            return NULL;
    }
}

BOOL open32NativeControlOwned(ULONG handle)
{
    for (NativeControl *control = controls; control; control = control->next)
        if (control->window.handle == handle)
            return TRUE;
    return FALSE;
}

/* Creates a predefined Windows control class as its PM control. Returns FALSE
   when the class is not predefined, leaving the caller to report the failure. */
BOOL open32CreateNativeControl(const char *name, const char *title, ULONG style, LONG x, LONG y,
                               LONG width, LONG height, ULONG parent, ULONG id, ULONG *handle)
{
    unsigned kind = control_kind(name, style);
    *handle = 0;
    if (!kind || !(style & WIN32_WS_CHILD) || !parent)
        return FALSE;
    HWND owner = open32ClientWindow(parent);
    RECTL area;
    if (!WinQueryWindowRect(owner, &area))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return TRUE;
    }
    NativeControl *control = calloc(1, sizeof *control);
    char *text = strdup(title ? title : "");
    if (!control || !text)
    {
        free(control);
        free(text);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return TRUE;
    }
    for (char *p = text; *p; p++)
        *p = (char)WinCpTranslateChar(0, WIN32_CP_WINANSI, (UCHAR)*p, CONTROL_CODEPAGE);
    ULONG pm_style = control_style(style, &kind);
    char pm_class[8];
    snprintf(pm_class, sizeof pm_class, "#%u", kind);
    HWND created =
        WinCreateWindow(owner, pm_class, text, pm_style, x, area.yTop - area.yBottom - y - height,
                        width, height, owner, HWND_TOP, id, NULL, NULL);
    free(text);
    if (!created)
    {
        free(control);
        open32SetError(ERROR_CANNOT_FIND_WND_CLASS);
        return TRUE;
    }
    control->window.handle = created;
    control->window.instance = GProcessState.main_module ? GProcessState.main_module->handle : 0;
    control->original = WinQueryWindowULong(created, QWP_PFNWP);
    control->next = controls;
    controls = control;
    *handle = created;
    return TRUE;
}

LONG open32NativeWindowLong(ULONG handle, LONG index, LONG value, BOOL set)
{
    NativeControl *control = find_control(handle, TRUE);
    if (!control)
        return 0;
    ULONG *field;
    switch (index)
    {
        case WIN32_GWL_WNDPROC:
        case WIN32_DWL_DLGPROC:
        {
            if (!prepare_bridges() || (set && !open32ValidProcedure((ULONG)value)))
            {
                open32SetError(ERROR_INVALID_PARAMETER);
                return 0;
            }
            ULONG previous = control->subclassed ? control->window.procedure
                                                 : lx_execution_mapping_address(bridges);
            if (set)
            {
                if (!control->subclassed)
                    WinSubclassWindow(handle,
                                      (PFNWP)(uintptr_t)(lx_execution_mapping_address(bridges) +
                                                         BRIDGE_SLOT_SIZE));
                control->window.procedure = (ULONG)value;
                control->subclassed = TRUE;
            }
            return (LONG)previous;
        }
        case WIN32_GWL_USERDATA:
            field = &control->window.user_data;
            break;
        case WIN32_DWL_MSGRESULT:
            field = &control->result;
            break;
        case WIN32_DWL_USER:
            field = &control->dialog_user;
            break;
        case WIN32_GWL_HINSTANCE:
            field = &control->window.instance;
            break;
        case WIN32_GWL_ID:
        {
            ULONG previous = WinQueryWindowUShort(handle, QWS_ID);
            if (set)
                WinSetWindowUShort(handle, QWS_ID, (USHORT)value);
            return (LONG)previous;
        }
        default:
            open32SetError(ERROR_INVALID_INDEX);
            return 0;
    }
    ULONG previous = *field;
    if (set)
        *field = (ULONG)value;
    return (LONG)previous;
}

void open32NativeControlsPrune(void)
{
    for (NativeControl **link = &controls; *link;)
    {
        NativeControl *control = *link;
        BOOL active = FALSE;
        for (NativeDispatch *packet = dispatch; packet; packet = packet->previous)
            if (packet->handle == control->window.handle)
                active = TRUE;
        if (active || WinIsWindow(0, control->window.handle))
        {
            link = &control->next;
            continue;
        }
        *link = control->next;
        open32PropertiesCleanup(&control->window);
        free(control);
    }
    if (!controls && bridges && !dispatch)
    {
        lx_execution_unmap(GProcessState.execution, bridges);
        bridges = NULL;
    }
}
