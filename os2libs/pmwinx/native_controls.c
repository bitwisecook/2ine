/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows control messages and subclass chains retain the underlying PM controls. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwinx/drawing.h"
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
    if (kind == 3)
        code = code == 1 ? 0 : code == 2 ? 5 : code;
    else if (kind == 6 || kind == 10)
        code = code == 1                                               ? 0x100
               : code == 2                                             ? 0x200
               : (kind == 6 && code == 4) || (kind == 10 && code == 7) ? 0x300
                                                                       : code;
    else if (kind == 2)
        code = code == 1   ? 5
               : code == 4 ? 1
               : code == 7 ? 9
               : code == 6 ? (WinSendMsg(handle, 0x172, 0, 0) ? 7 : 8)
                           : code;
    else if (kind == 7)
        code = code == 2 ? 4 : code == 3 ? 5 : code == 5 ? 2 : code;
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
        open32SetError(8);
        return NULL;
    }
    control->window.handle = handle;
    control->window.instance = GLoaderState.main_module ? GLoaderState.main_module->handle : 0;
    control->original = WinQueryWindowULong(handle, -3);
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
    return lx_execution_callback32_4(GLoaderState.execution, control->original,
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
    char *text = lx_execution_copy_string(GLoaderState.execution, address, 65536);
    if (text)
        for (char *p = text; *p; p++)
            *p = (char)WinCpTranslateChar(0, 1004, (UCHAR)*p, 437);
    return text;
}
/* An application subclass sees message parameters as 32-bit guest values, so text
   exchanged with a control must live at a guest address for the whole call. */
static LxGuestMapping *guest_text(size_t size, ULONG *address, char **host)
{
    LxGuestMapping *mapping =
        lx_execution_map(GLoaderState.execution, 0, (size + 4095) & ~(size_t)4095, 3, 0);
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
        open32SetError(8);
        return -1;
    }
    memcpy(host, text, size);
    LONG result = (LONG)(intptr_t)WinSendMsg(handle, message, (MPARAM)(uintptr_t)first,
                                             (MPARAM)(uintptr_t)address);
    lx_execution_unmap(GLoaderState.execution, mapping);
    return result;
}
/* Adds the files matching a pattern. Read-write files are listed unless DDL_EXCLUSIVE
   asks for only the requested attributes; directories appear as [name]. */
static LONG list_directory(ULONG handle, ULONG attributes, ULONG pattern)
{
    enum
    {
        readonly = 1,
        hidden = 2,
        system_file = 4,
        directory = 0x10,
        archive = 0x20,
        exclusive = 0x8000,
        name_offset = 44
    };
    char *name = lx_execution_copy_string(GLoaderState.execution, pattern, 260);
    if (!name)
    {
        open32SetError(87);
        return -1;
    }
    uint8 found[320];
    ULONG search = FindFirstFile(name, found);
    free(name);
    if (search == UINT32_MAX)
        return -1;
    LONG last = -1;
    do
    {
        ULONG mode = lx_guest_load_u32(found);
        const char *file = (const char *)found + name_offset;
        BOOL is_directory = (mode & directory) != 0;
        BOOL plain = !(mode & (readonly | hidden | system_file | directory));
        BOOL wanted = (mode & attributes & (readonly | hidden | system_file | archive)) ||
                      (is_directory && (attributes & directory)) ||
                      (plain && !(attributes & exclusive));
        if (!wanted || !strcmp(file, ".") || (is_directory && !(attributes & directory)))
            continue;
        char label[272];
        snprintf(label, sizeof label, is_directory ? "[%s]" : "%s", file);
        LONG index = send_text(handle, 0x161, (ULONG)-1, label);
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
    if (!WinQueryPresParam(handle, 15, 0, NULL, sizeof name - 1, name, 0))
        return 0;
    char *face = strchr(name, '.');
    LONG points = (LONG)strtol(name, NULL, 10);
    if (!face || points <= 0)
        return 0;
    ULONG font = CreateFont(-(points * 96 / 72), 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, face + 1);
    if (control)
        control->font = font;
    return font;
}
static void set_control_font(ULONG handle, ULONG font, BOOL redraw)
{
    NativeControl *control = find_control(handle, TRUE);
    uint8 record[60];
    if (control)
        control->font = font;
    if (!font || open32FontObject(font, sizeof record, record) <= 0)
        return;
    LONG height = (LONG)lx_guest_load_u32(record);
    LONG points = (height < 0 ? -height : height) * 72 / 96;
    char name[64];
    int length = snprintf(name, sizeof name, "%d.%s", (int)(points ? points : 8),
                          (const char *)record + 28);
    if (length > 0 && (size_t)length < sizeof name)
        WinSetPresParam(handle, 15, (ULONG)length + 1, name);
    if (redraw)
        WinInvalidateRect(handle, NULL, TRUE);
}
/* Copies one item into the caller's buffer, which Windows sizes from the text length. */
static LONG list_text(ULONG handle, ULONG index, ULONG address, BOOL original)
{
    LONG length = pm_message(handle, 0x167, index, 0, original);
    if (length < 0 || length >= 0xFFFF)
        return -1;
    if (!lx_execution_memory(GLoaderState.execution, address, (size_t)length + 1, LX_GUEST_WRITE))
    {
        open32SetError(87);
        return -1;
    }
    ULONG buffer;
    char *text;
    LxGuestMapping *mapping = guest_text((size_t)length + 1, &buffer, &text);
    if (!mapping)
    {
        open32SetError(8);
        return -1;
    }
    ULONG request = (index & 0xFFFF) | (ULONG)(length + 1) << 16;
    WinSendMsg(handle, 0x168, (MPARAM)(uintptr_t)request, (MPARAM)(uintptr_t)buffer);
    for (char *p = text; *p; p++)
        *p = (char)WinCpTranslateChar(0, 437, (UCHAR)*p, 1004);
    lx_execution_write(GLoaderState.execution, address, text, (size_t)length + 1);
    lx_execution_unmap(GLoaderState.execution, mapping);
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
                    if (message == 0x102 || message == 0x106)
                        flags &= ~2u; /* Character delivery must not repeat virtual-key actions. */
                    else
                        flags &= ~1u; /* Keydown precedes the distinct Windows character message. */
                }
                return (LONG)original_message(control, packet->native_message, flags,
                                              packet->native_second);
            }
    if (!original && control && control->subclassed)
        return (LONG)lx_execution_callback32_4(GLoaderState.execution, control->window.procedure,
                                               handle, message, first, second);
    char name[16];
    if (!WinQueryClassName(handle, sizeof name, name))
        return 0;
    unsigned kind = name[0] == '#' ? (unsigned)strtoul(name + 1, NULL, 10) : 0;
    ULONG native = 0, a = first, b = second;
    if (message == 12)
    {
        char *text = ansi_text(second);
        BOOL result = text && WinSetWindowText(handle, text);
        free(text);
        return result;
    }
    if (message == 13)
    {
        if (!first || first > 1048576)
            return 0;
        char *text = calloc(1, first);
        if (!text)
            return 0;
        LONG result = WinQueryWindowText(handle, (LONG)first, text);
        for (LONG i = 0; i < result; i++)
            text[i] = (char)WinCpTranslateChar(0, 437, (UCHAR)text[i], 1004);
        if (!lx_execution_write(GLoaderState.execution, second, text, (size_t)result + 1))
        {
            open32SetError(87);
            result = 0;
        }
        free(text);
        return result;
    }
    if (message == 14)
        return WinQueryWindowTextLength(handle);
    if (message == 0x87)
        native = WM_QUERYDLGCODE;
    else if (message == 0x82)
        return 0;
    else if (message == 0x102 || message == 0x106)
    {
        native = WM_CHAR;
        a = 1 | 1u << 16 | (message == 0x106 ? 0x20 : 0);
        b = (UCHAR)first;
        if (b >= 32 && b != 127)
            b = WinCpTranslateChar(0, 1004, (UCHAR)b, 437);
    }
    else if (message == 2)
        native = WM_DESTROY;
    else if (message == 15)
        native = WM_PAINT;
    else if (message == 16)
        native = WM_CLOSE;
    else if (message == 0x111)
    {
        native = second ? WM_CONTROL : WM_COMMAND;
        b = second;
    }
    else if ((kind == 6 || kind == 10) && message == 0xc5) /* EM_LIMITTEXT */
    {
        native = kind == 10 ? 0x1b0 : 0x143;
        a = first ? first : 32767;
    }
    else if ((kind == 6 || kind == 10) && message == 0xb1) /* EM_SETSEL */
    {
        if (kind == 10)
            native = 0x1c0;
        else
        {
            native = 0x142;
            if (first == UINT32_MAX)
                a = b = WinQueryWindowTextLength(handle);
            else
            {
                a = (USHORT)first | (ULONG)(USHORT)second << 16;
                b = 0;
            }
        }
    }
    else if (kind == 3 && message >= 0xf0 && message <= 0xf5)
    {
        if (message == 0xf0)
            native = 0x124;
        else if (message == 0xf1)
            native = 0x125;
        else if (message == 0xf2)
            return (pm_message(handle, 0x124, 0, 0, original) & 3) |
                   (pm_message(handle, 0x122, 0, 0, original) ? 4 : 0) |
                   (WinQueryFocus(HWND_DESKTOP) == handle ? 8 : 0);
        else if (message == 0xf3)
            native = 0x123;
        else if (message == 0xf5)
            native = 0x120;
        else
        {
            static const LONG styles[] = {0, 0, 1, 2, 3, 5, 6, -1, -1, 4, -1, 7};
            if (first >= sizeof styles / sizeof *styles || styles[first] < 0)
            {
                open32SetError(50);
                return 0;
            }
            ULONG style = (ULONG)styles[first] | (first == 1 ? 0x400 : 0);
            WinSetWindowULong(handle, -2, (WinQueryWindowULong(handle, -2) & ~0x40fu) | style);
            if (second)
                WinInvalidateRect(handle, NULL, FALSE);
            return 0;
        }
    }
    else if ((kind == 2 && (message == 0x143 || message == 0x14a)) ||
             (kind == 7 && (message == 0x180 || message == 0x181)))
    {
        char *text = ansi_text(second);
        if (!text)
            return -1;
        ULONG index = message == 0x143 || message == 0x180 ? (ULONG)-1 : first;
        LONG result = send_text(handle, 0x161, index, text);
        free(text);
        return result;
    }
    else if ((kind == 2 && (message == 0x14c || message == 0x158 || message == 0x14d)) ||
             (kind == 7 && (message == 0x18f || message == 0x1a2 || message == 0x18c)))
    {
        char *text = ansi_text(second);
        if (!text)
            return -1;
        ULONG search = (message == 0x158 || message == 0x1a2 ? 4 : 6) | (ULONG)(USHORT)first << 16;
        LONG result = send_text(handle, 0x16b, search, text);
        free(text);
        if (result < 0)
            return -1;
        if (message == 0x14d || message == 0x18c)
            pm_message(handle, 0x164, (ULONG)result, TRUE, original);
        return result;
    }
    else if ((kind == 2 && message == 0x14e) || (kind == 7 && message == 0x186))
    {
        LONG result =
            pm_message(handle, 0x164, first, first == UINT32_MAX ? FALSE : TRUE, original);
        return result && first != UINT32_MAX ? (LONG)first : -1;
    }
    else if ((kind == 2 && message == 0x147) || (kind == 7 && message == 0x188))
        return pm_message(handle, 0x165, UINT32_MAX, 0, original);
    else if ((kind == 2 && message == 0x146) || (kind == 7 && message == 0x18b))
        return pm_message(handle, 0x160, 0, 0, original);
    else if ((kind == 2 && message == 0x14b) || (kind == 7 && message == 0x184))
    {
        pm_message(handle, 0x16e, 0, 0, original);
        return 0;
    }
    else if ((kind == 2 && message == 0x150) || (kind == 7 && message == 0x199))
        return pm_message(handle, 0x16a, first, 0, original);
    else if ((kind == 2 && message == 0x151) || (kind == 7 && message == 0x19a))
        return pm_message(handle, 0x169, first, second, original) ? 0 : -1;
    else if ((kind == 2 && message == 0x148) || (kind == 7 && message == 0x189))
        return list_text(handle, first, second, original); /* CB_GETLBTEXT, LB_GETTEXT */
    else if ((kind == 2 && message == 0x149) || (kind == 7 && message == 0x18a))
    {
        LONG length = pm_message(handle, 0x167, first, 0, original);
        return length < 0 ? -1 : length; /* CB_GETLBTEXTLEN, LB_GETTEXTLEN */
    }
    else if ((kind == 2 && message == 0x144) || (kind == 7 && message == 0x182))
    {
        LONG count = pm_message(handle, 0x163, first, 0, original);
        return count < 0 ? -1 : count; /* CB_DELETESTRING, LB_DELETESTRING */
    }
    else if ((kind == 2 && message == 0x145) || (kind == 7 && message == 0x18d))
        return list_directory(handle, first, second); /* CB_DIR, LB_DIR */
    else if (kind == 7 && message == 0x18e)
        return pm_message(handle, 0x16d, 0, 0, original); /* LB_GETTOPINDEX */
    else if (kind == 7 && message == 0x197)
        return pm_message(handle, 0x162, first, 0, original) ? 0 : -1; /* LB_SETTOPINDEX */
    else if (kind == 7 && message == 0x194)
        return 0; /* LB_SETHORIZONTALEXTENT: PM list boxes size their own extent. */
    else if ((kind == 6 || kind == 10) && message == 0xba)
        return kind == 10 ? pm_message(handle, 0x1bc, 0, 0, original) : 1; /* EM_GETLINECOUNT */
    else if ((kind == 6 || kind == 10) && message == 0xcb)
        return 1; /* EM_SETTABSTOPS: PM controls keep their uniform tab stops. */
    else if (message == 0x81 || message == 1)
        return message == 0x81; /* WM_NCCREATE and WM_CREATE: the PM control already exists. */
    else if (message == 0x31)
        return (LONG)control_font(handle); /* WM_GETFONT */
    else if (message == 0x30)
    {
        set_control_font(handle, first, second != 0); /* WM_SETFONT */
        return 0;
    }
    else if (kind == 2 && message == 0x141)
    {
        /* CB_LIMITTEXT: the limit belongs to the combo box's entry field. */
        WinSendMsg(WinWindowFromID(handle, 0x29b), 0x143, (MPARAM)(uintptr_t)first, 0);
        return 1;
    }
    else if (message == 0xb)
    {
        /* WM_SETREDRAW: PM controls repaint each change as it happens. */
        if (first)
            WinInvalidateRect(handle, NULL, TRUE);
        return 0;
    }
    else if (message >= 0x400)
        native = message - 0x400 + WM_USER;
    if (!native)
    {
        open32SetError(50);
        return 0;
    }
    LONG result = pm_message(handle, native, a, b, original);
    return kind == 3 && message != 0xf0 && message >= 0xf0 && message <= 0xf5 ? 0 : result;
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
        packet.message = 2;
    else if (packet.native_message == WM_QUERYDLGCODE)
        packet.message = 0x87;
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
        lx_execution_callback32_4(GLoaderState.execution, control->window.procedure, packet.handle,
                                  packet.message, packet.first, packet.second);
    if (key.text && WinIsWindow(0, packet.handle))
    {
        packet.message = key.message == 0x104 ? 0x106 : 0x102;
        packet.first = key.character;
        result =
            lx_execution_callback32_4(GLoaderState.execution, control->window.procedure,
                                      packet.handle, packet.message, packet.first, packet.second);
    }
    dispatch = packet.previous;
    return result;
}
static BOOL prepare_bridges(void)
{
    if (bridges)
        return TRUE;
    LxGuestMapping *mapping =
        lx_execution_map(GLoaderState.execution, 0, 4096, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    const LxExecutionApi32 defaults = {windows_default, 16, 0, LX_ABI_STACK32};
    const LxExecutionApi32 subclass = {native_dispatch, 16, 0, LX_ABI_STACK32};
    if (!mapping)
        return FALSE;
    uint8 *bytes = lx_execution_mapping_host(mapping);
    if (!lx_execution_emit_bridge32(GLoaderState.execution, bytes, 256, &defaults) ||
        !lx_execution_emit_bridge32(GLoaderState.execution, bytes + 256, 256, &subclass) ||
        !lx_execution_protect(GLoaderState.execution, mapping, LX_GUEST_READ | LX_GUEST_EXECUTE))
    {
        lx_execution_unmap(GLoaderState.execution, mapping);
        return FALSE;
    }
    bridges = mapping;
    return TRUE;
}
enum
{
    win_style_visible = 0x10000000,
    win_style_disabled = 0x08000000,
    win_style_border = 0x00800000,
    win_style_vscroll = 0x00200000,
    win_style_hscroll = 0x00100000,
    win_style_group = 0x00020000,
    win_style_tabstop = 0x00010000,
    win_style_child = 0x40000000
};
static ULONG button_style(ULONG style, unsigned *kind)
{
    switch (style & 15)
    {
    case 1:
        return 0x400; /* BS_DEFPUSHBUTTON to BS_DEFAULT */
    case 2:
        return 1; /* BS_CHECKBOX */
    case 3:
        return 2; /* BS_AUTOCHECKBOX */
    case 4:
        return 3; /* BS_RADIOBUTTON */
    case 5:
        return 5; /* BS_3STATE */
    case 6:
        return 6; /* BS_AUTO3STATE */
    case 7:
        *kind = 5;
        return 2; /* BS_GROUPBOX to SS_GROUPBOX */
    case 8:
    case 11:
        return 7; /* user and owner-drawn buttons to BS_USERBUTTON */
    case 9:
        return 4; /* BS_AUTORADIOBUTTON */
    default:
        return 0;
    }
}
static ULONG static_style(ULONG style)
{
    static const ULONG types[] = {1,     0x101, 0x201, 3,  5,     6,
                                  7,     8,     9,     10, 1,     1,
                                  1};
    ULONG type = style & 15;
    ULONG value = type < sizeof types / sizeof *types ? types[type] : 1;
    if ((value & 0xff) == 1 && type != 11 && type != 12)
        value |= 0x4000; /* DT_WORDBREAK */
    if ((value & 0xff) == 1 && !(style & 0x80))
        value |= 0x2000; /* DT_MNEMONIC unless SS_NOPREFIX */
    return value;
}
static ULONG control_style(ULONG style, unsigned *kind)
{
    ULONG value = (style & win_style_visible ? WS_VISIBLE : 0) |
                  (style & win_style_disabled ? WS_DISABLED : 0) |
                  (style & win_style_tabstop ? 0x20000 : 0) |
                  (style & win_style_group ? 0x10000 : 0);
    if (*kind == 3)
        return value | button_style(style, kind);
    if (*kind == 5)
        return value | static_style(style);
    if (*kind == 10)
        return value | 1 | (style & win_style_border ? 2 : 0) |
               (style & win_style_vscroll ? 4 : 0) | (style & win_style_hscroll ? 8 : 0) |
               (style & 0x800 ? 0x10 : 0);
    if (*kind == 6)
        return value | (style & 3) | (style & 0x80 ? 4 : 0) |
               (style & win_style_border ? 8 : 0) | (style & 0x800 ? 0x20 : 0) |
               (style & 0x20 ? 0x80 : 0);
    if (*kind == 7)
        return value | (style & 8 ? 1 : 0) | (style & 0x30 ? 2 : 0) | (style & 0x100 ? 4 : 0) |
               (style & win_style_hscroll ? 8 : 0) | (style & 0x800 ? 0x10 : 0);
    ULONG type = style & 3;
    return value | (type == 3 ? 4 : type);
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
        return 3;
    if (!strcasecmp(name, "EDIT"))
        return style & 4 ? 10 : 6;
    if (!strcasecmp(name, "MLE"))
        return 10;
    if (!strcasecmp(name, "STATIC"))
        return 5;
    if (!strcasecmp(name, "LISTBOX"))
        return 7;
    return !strcasecmp(name, "COMBOBOX") ? 2 : 0;
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
        case 2:
            return "COMBOBOX";
        case 3:
            return "BUTTON";
        case 5:
            return "STATIC";
        case 6:
            return "EDIT";
        case 7:
            return "LISTBOX";
        case 8:
            return "SCROLLBAR";
        case 10:
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
    if (!kind || !(style & win_style_child) || !parent)
        return FALSE;
    HWND owner = open32ClientWindow(parent);
    RECTL area;
    if (!WinQueryWindowRect(owner, &area))
    {
        open32SetError(1400);
        return TRUE;
    }
    NativeControl *control = calloc(1, sizeof *control);
    char *text = strdup(title ? title : "");
    if (!control || !text)
    {
        free(control);
        free(text);
        open32SetError(8);
        return TRUE;
    }
    for (char *p = text; *p; p++)
        *p = (char)WinCpTranslateChar(0, 1004, (UCHAR)*p, 437);
    ULONG pm_style = control_style(style, &kind);
    char pm_class[8];
    snprintf(pm_class, sizeof pm_class, "#%u", kind);
    HWND created = WinCreateWindow(owner, pm_class, text, pm_style, x,
                                   area.yTop - area.yBottom - y - height, width, height, owner,
                                   HWND_TOP, id, NULL, NULL);
    free(text);
    if (!created)
    {
        free(control);
        open32SetError(1407);
        return TRUE;
    }
    control->window.handle = created;
    control->window.instance = GLoaderState.main_module ? GLoaderState.main_module->handle : 0;
    control->original = WinQueryWindowULong(created, -3);
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
        case -4:
        case 4:
        {
            if (!prepare_bridges() || (set && !open32ValidProcedure((ULONG)value)))
            {
                open32SetError(87);
                return 0;
            }
            ULONG previous = control->subclassed ? control->window.procedure
                                                 : lx_execution_mapping_address(bridges);
            if (set)
            {
                if (!control->subclassed)
                    WinSubclassWindow(
                        handle, (PFNWP)(uintptr_t)(lx_execution_mapping_address(bridges) + 256));
                control->window.procedure = (ULONG)value;
                control->subclassed = TRUE;
            }
            return (LONG)previous;
        }
        case -21:
            field = &control->window.user_data;
            break;
        case 0:
            field = &control->result;
            break;
        case 8:
            field = &control->dialog_user;
            break;
        case -6:
            field = &control->window.instance;
            break;
        case -12:
        {
            ULONG previous = WinQueryWindowUShort(handle, -1);
            if (set)
                WinSetWindowUShort(handle, -1, (USHORT)value);
            return (LONG)previous;
        }
        default:
            open32SetError(1413);
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
        lx_execution_unmap(GLoaderState.execution, bridges);
        bridges = NULL;
    }
}
