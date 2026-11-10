/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM key packets become Windows key/character messages in the active window. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/private/pmp.h"
#include "os2/ps/awe/internal.h"
#include "host/input.h"
#include <stdio.h>

enum
{
    /* WM_CHAR mp1 carries the repeat count in byte 2 and the scan code in byte 3; mp2 carries
       the PM virtual key in its high word. */
    PM_KEY_REPEAT_SHIFT = 16,
    PM_KEY_SCAN_SHIFT = 24,
    PM_KEY_VIRTUAL_SHIFT = 16,
    /* PM scan codes the Windows key messages treat specially. */
    SCAN_ENTER = 0x1C,
    SCAN_LEFT_SHIFT = 0x2A,
    SCAN_RIGHT_SHIFT = 0x36,
    SCAN_PAD_ENTER = 0x5A,
    /* PM gives the grey Home..Delete keys codes of their own; Windows reports them as the
       keypad keys' scan codes with KF_EXTENDED. */
    SCAN_GREY_FIRST = 0x60,
    SCAN_GREY_LAST = 0x69,
    /* Windows key message lParam: repeat count in the low word, scan code in bits 16-23 and
       the KF_* flags in the high word. */
    KEY_PARAMETER_SCAN_SHIFT = 16,
    KEY_PARAMETER_FLAGS_SHIFT = 16,
    /* GetKeyState's down bit, and GetKeyboardState's per-key down bit. */
    KEY_STATE_DOWN = 0x8000,
    KEYBOARD_STATE_DOWN = 0x80,
    /* VkKeyScan returns the shift state in its high byte; bit 0 is Shift. */
    VK_KEY_SCAN_SHIFT = 0x100,
    VIRTUAL_KEY_COUNT = 256,
    /* lx_pm_mouse_state numbers the middle button 3, as PM does. */
    PM_MIDDLE_BUTTON = 3,
    /* Guests type Windows Latin 1; PM key packets carry code page 437. */
    PM_KEY_CODEPAGE = 437,
    CHARACTER_ESCAPE = 0x1B,
    CHARACTER_DELETE = 0x7F,
    CHARACTER_ASCII_LIMIT = 0x80
};

static _Thread_local unsigned char down[VIRTUAL_KEY_COUNT], toggled[VIRTUAL_KEY_COUNT];
/* Windows virtual key for each PM scan code, in US keyboard positions. */
static const unsigned char scan_keys[] = {
    0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',  0xBD, 0xBB, 8,
    9,    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  0xDB, 0xDD, 13,   17,
    'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  0xBA, 0xDE, 0xC0, 16,   0xDC, 'Z',
    'X',  'C',  'V',  'B',  'N',  'M',  0xBC, 0xBE, 0xBF, 16,   0x6A, 18,   32,   20,   0x70,
    0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x90, 0x91, 0x67, 0x68, 0x69, 0x6D,
    0x64, 0x65, 0x66, 0x6B, 0x61, 0x62, 0x63, 0x60, 0x6E, 0,    0,    0xE2, 0x7A, 0x7B};

/* PM VK_* codes to Windows VK_* codes. (PMMERGE's keyxlate.c maps host keys to PM VK_*.)
   The table covers the PM keys below VK_F1; the function keys map as a range. */
static unsigned virtual_key(ULONG value)
{
    static const unsigned char keys[VK_F1] = {0,  1,    2,  4,  3,  8,  9,    9,    13, 16, 17,
                                              18, 0xA5, 19, 20, 27, 32, 33,   34,   35, 36, 37,
                                              38, 39,   40, 44, 45, 46, 0x91, 0x90, 13, 44};
    return value < VK_F1                       ? keys[value]
           : value >= VK_F1 && value <= VK_F24 ? WIN32_VK_F1 + value - VK_F1
                                               : 0;
}

void open32DecodeKey(ULONG flags, ULONG data, Open32Key *decoded)
{
    ULONG scan = flags >> PM_KEY_SCAN_SHIFT, key = flags & KC_VIRTUALKEY
                                                       ? virtual_key(data >> PM_KEY_VIRTUAL_SHIFT)
                                                   : scan < sizeof scan_keys ? scan_keys[scan]
                                                                             : 0;
    ULONG character = (UCHAR)data;
    /* Keyboard control bytes retain their input meaning, independent of display glyphs. */
    if (character >= ' ' && character != CHARACTER_DELETE)
        character = WinCpTranslateChar(0, PM_KEY_CODEPAGE, (UCHAR)character, WIN32_CP_WINANSI);
    if (!key && character < CHARACTER_ASCII_LIMIT)
        key = character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character;
    BOOL up = (flags & KC_KEYUP) != 0;
    /* Windows sends Ctrl+letter as the matching control character. */
    if ((flags & KC_CTRL) && character >= 'a' && character <= 'z')
        character = character - 'a' + 1;
    else if ((flags & KC_CTRL) && character >= 'A' && character <= 'Z')
        character = character - 'A' + 1;
    BOOL extended = scan >= SCAN_GREY_FIRST && scan <= SCAN_GREY_LAST;
    /* Keypad scan codes for the grey keys, from SCAN_GREY_FIRST on. */
    static const unsigned char enhanced[] = {0x47, 0x48, 0x49, 0x4B, 0x4D,
                                             0x4F, 0x50, 0x51, 0x52, 0x53};
    if (extended)
        scan = enhanced[scan - SCAN_GREY_FIRST];
    if (scan == SCAN_PAD_ENTER)
    {
        scan = SCAN_ENTER;
        extended = TRUE;
    }
    ULONG repeat = (flags >> PM_KEY_REPEAT_SHIFT) & 255;
    ULONG parameter =
        (repeat ? repeat : 1) | (scan & 255) << KEY_PARAMETER_SCAN_SHIFT |
        (extended ? (ULONG)WIN32_KF_EXTENDED << KEY_PARAMETER_FLAGS_SHIFT : 0) |
        (flags & KC_ALT ? (ULONG)WIN32_KF_ALTDOWN << KEY_PARAMETER_FLAGS_SHIFT : 0) |
        ((up || (flags & KC_PREVDOWN)) ? (ULONG)WIN32_KF_REPEAT << KEY_PARAMETER_FLAGS_SHIFT : 0) |
        (up ? (ULONG)WIN32_KF_UP << KEY_PARAMETER_FLAGS_SHIFT : 0);
    /* WM_KEYUP and WM_SYSKEYUP each follow their key-down message. */
    *decoded = (Open32Key){(flags & KC_ALT ? WIN32_WM_SYSKEYDOWN : WIN32_WM_KEYDOWN) + up,
                           key,
                           parameter,
                           character,
                           flags,
                           !up && (flags & KC_CHAR)};
}

void open32UpdateKey(const Open32Key *decoded)
{
    ULONG key = decoded->key, flags = decoded->flags;
    BOOL up = (flags & KC_KEYUP) != 0;
    down[WIN32_VK_SHIFT] = (flags & KC_SHIFT) != 0;
    down[WIN32_VK_CONTROL] = (flags & KC_CTRL) != 0;
    down[WIN32_VK_MENU] = (flags & KC_ALT) != 0;
    if (key < VIRTUAL_KEY_COUNT)
    {
        /* Lock keys toggle on the press, not on auto-repeat. */
        if (!up && !down[key] &&
            (key == WIN32_VK_CAPITAL || key == WIN32_VK_NUMLOCK || key == WIN32_VK_SCROLL))
            toggled[key] ^= 1;
        down[key] = !up;
    }
}

ULONG open32KeyboardMessage(ULONG handle, ULONG flags, ULONG data)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
        return 0;
    Open32Key key;
    open32DecodeKey(flags, data, &key);
    open32UpdateKey(&key);
    if (open32RunKeyboardHooks(WIN32_HC_ACTION, key.key, key.parameter))
        return 0;
    ULONG result = open32WindowCallback(window, key.message, key.key, key.parameter);
    window = open32FindWindow(handle);
    if (window && key.text)
        result = open32WindowCallback(
            window, key.message == WIN32_WM_SYSKEYDOWN ? WIN32_WM_SYSCHAR : WIN32_WM_CHAR,
            key.character, key.parameter);
    return result;
}

SHORT GetKeyState(LONG key)
{
    if (key < 0 || key >= VIRTUAL_KEY_COUNT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (SHORT)((down[key] ? KEY_STATE_DOWN : 0) | toggled[key]);
}

SHORT GetAsyncKeyState(LONG key)
{
    if (key < 0 || key >= VIRTUAL_KEY_COUNT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!open32Presentation() || !lx_pm_poll_input())
        return 0;
    if (key == WIN32_VK_LBUTTON || key == WIN32_VK_RBUTTON || key == WIN32_VK_MBUTTON)
        return (SHORT)lx_pm_mouse_state(key == WIN32_VK_MBUTTON ? PM_MIDDLE_BUTTON : (unsigned)key);
    if (key == WIN32_VK_SHIFT)
        return (SHORT)(WinGetPhysKeyState(HWND_DESKTOP, SCAN_LEFT_SHIFT) |
                       WinGetPhysKeyState(HWND_DESKTOP, SCAN_RIGHT_SHIFT));
    if (key == WIN32_VK_RETURN)
        return (SHORT)(WinGetPhysKeyState(HWND_DESKTOP, SCAN_ENTER) |
                       WinGetPhysKeyState(HWND_DESKTOP, SCAN_PAD_ENTER));
    /* The grey keys in PM scan code order, from SCAN_GREY_FIRST on. */
    static const uint8 navigation[] = {
        WIN32_VK_HOME, WIN32_VK_UP,   WIN32_VK_PRIOR, WIN32_VK_LEFT,   WIN32_VK_RIGHT,
        WIN32_VK_END,  WIN32_VK_DOWN, WIN32_VK_NEXT,  WIN32_VK_INSERT, WIN32_VK_DELETE};
    for (unsigned i = 0; i < sizeof navigation; i++)
        if (navigation[i] == key)
            return (SHORT)WinGetPhysKeyState(HWND_DESKTOP, SCAN_GREY_FIRST + i);
    for (unsigned scan = 1; scan < sizeof scan_keys; scan++)
        if (scan_keys[scan] == key)
            return (SHORT)WinGetPhysKeyState(HWND_DESKTOP, (LONG)scan);
    return 0;
}

BOOL GetKeyboardState(PVOID output)
{
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8 *bytes = output;
    for (unsigned key = 0; key < VIRTUAL_KEY_COUNT; key++)
        bytes[key] = (down[key] ? KEYBOARD_STATE_DOWN : 0) | toggled[key];
    return TRUE;
}

/* The active host layout decides what each physical key types, and so which letter key
   it is. Keys without a character keep the position's virtual key. */
static ULONG layout_key(ULONG scan)
{
    ULONG character = scan < sizeof scan_keys ? lx_host_key_character(scan, 0) : 0;
    if (character >= 'a' && character <= 'z')
        return character - 'a' + 'A';
    return scan < sizeof scan_keys ? scan_keys[scan] : 0;
}

ULONG MapVirtualKey(ULONG code, ULONG kind)
{
    if (kind == WIN32_MAPVK_VK_TO_VSC)
    {
        for (ULONG scan = 1; scan < sizeof scan_keys; scan++)
            if (layout_key(scan) == code)
                return scan;
        return 0;
    }
    if (kind == WIN32_MAPVK_VSC_TO_VK || kind == WIN32_MAPVK_VSC_TO_VK_EX)
        return layout_key(code);
    if (kind == WIN32_MAPVK_VK_TO_CHAR)
    {
        if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == WIN32_VK_SPACE)
            return code;
        for (ULONG scan = 1; scan < sizeof scan_keys; scan++)
            if (layout_key(scan) == code)
            {
                ULONG character = lx_host_key_character(scan, 0);
                return character < CHARACTER_ASCII_LIMIT ? character : 0;
            }
    }
    return 0;
}

SHORT VkKeyScan(ULONG character)
{
    for (ULONG scan = 1; scan < sizeof scan_keys; scan++)
        for (int shifted = 0; shifted < 2; shifted++)
            if (lx_host_key_character(scan, shifted) == character && layout_key(scan))
                return (SHORT)((shifted ? VK_KEY_SCAN_SHIFT : 0) | layout_key(scan));
    if (character == '\t' || character == '\r' || character == '\b' ||
        character == CHARACTER_ESCAPE)
        return character;
    return -1;
}

ULONG GetKeyboardLayout(ULONG thread)
{
    /* The original forms the layout from the locale's LOCALE_ILANGUAGE, in both halves. */
    (void)thread;
    ULONG language = GetUserDefaultLCID() & 0xFFFF;
    return language << 16 | language;
}

LONG GetKeyNameText(ULONG parameter, PSZ output, LONG capacity)
{
    if (!output || capacity <= 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG scan = (parameter >> KEY_PARAMETER_SCAN_SHIFT) & 255,
          key = MapVirtualKey(scan, WIN32_MAPVK_VSC_TO_VK);
    const char *name = NULL;
    char text[16];
    switch (key)
    {
        case WIN32_VK_BACK:
            name = "Backspace";
            break;
        case WIN32_VK_TAB:
            name = "Tab";
            break;
        case WIN32_VK_RETURN:
            name = "Enter";
            break;
        case WIN32_VK_SHIFT:
            name = "Shift";
            break;
        case WIN32_VK_CONTROL:
            name = "Ctrl";
            break;
        case WIN32_VK_MENU:
            name = "Alt";
            break;
        case WIN32_VK_PAUSE:
            name = "Pause";
            break;
        case WIN32_VK_CAPITAL:
            name = "Caps Lock";
            break;
        case WIN32_VK_ESCAPE:
            name = "Esc";
            break;
        case WIN32_VK_SPACE:
            name = "Space";
            break;
        case WIN32_VK_NUMLOCK:
            name = "Num Lock";
            break;
        case WIN32_VK_SCROLL:
            name = "Scroll Lock";
            break;
        default:
            if (key >= WIN32_VK_F1 && key <= WIN32_VK_F24)
            {
                snprintf(text, sizeof text, "F%u", key - WIN32_VK_F1 + 1);
                name = text;
            }
            else
            {
                ULONG c = MapVirtualKey(key, WIN32_MAPVK_VK_TO_CHAR);
                if (c)
                {
                    text[0] = (char)c;
                    text[1] = 0;
                    name = text;
                }
            }
    }
    if (!name)
        name = "";
    size_t n = strlen(name);
    if (n >= (size_t)capacity)
        n = capacity - 1;
    memcpy(output, name, n);
    output[n] = 0;
    return (LONG)n;
}
