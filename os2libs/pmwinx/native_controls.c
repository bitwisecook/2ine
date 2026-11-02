/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows control messages and subclass chains retain the underlying PM controls. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/input.h"
typedef struct NativeControl
{
    Open32Window window;
    ULONG original, result, dialog_user;
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
        /* Native controls copy strings synchronously; these are host buffers. */
        LONG result = (LONG)(intptr_t)WinSendMsg(handle, 0x161, (MPARAM)(intptr_t)index, text);
        free(text);
        return result;
    }
    else if (kind == 2 && (message == 0x14c || message == 0x158 || message == 0x14d))
    {
        char *text = ansi_text(second);
        if (!text)
            return -1;
        ULONG search = (message == 0x158 ? 4 : 6) | (ULONG)(USHORT)first << 16;
        LONG result = (LONG)(intptr_t)WinSendMsg(handle, 0x16b, (MPARAM)(uintptr_t)search, text);
        free(text);
        if (result < 0)
            return -1;
        if (message == 0x14d)
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
