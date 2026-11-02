/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM key packets become Windows key/character messages in the active window. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/input.h"
#include "os2libs/pmwin/input.h"
#include "os2libs/pmwinx/internal.h"
static _Thread_local unsigned char down[256], toggled[256];
static const unsigned char scan_keys[] = {
    0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',  0xBD, 0xBB, 8,
    9,    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  0xDB, 0xDD, 13,   17,
    'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  0xBA, 0xDE, 0xC0, 16,   0xDC, 'Z',
    'X',  'C',  'V',  'B',  'N',  'M',  0xBC, 0xBE, 0xBF, 16,   0x6A, 18,   32,   20,   0x70,
    0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x90, 0x91, 0x67, 0x68, 0x69, 0x6D,
    0x64, 0x65, 0x66, 0x6B, 0x61, 0x62, 0x63, 0x60, 0x6E, 0,    0,    0xE2, 0x7A, 0x7B};
static unsigned virtual_key(ULONG value)
{
    static const unsigned char keys[32] = {0,  1,    2,  4,  3,  8,  9,    9,    13, 16, 17,
                                           18, 0xA5, 19, 20, 27, 32, 33,   34,   35, 36, 37,
                                           38, 39,   40, 44, 45, 46, 0x91, 0x90, 13, 44};
    return value < 32 ? keys[value] : value >= 0x20 && value <= 0x37 ? 0x70 + value - 0x20 : 0;
}
void open32DecodeKey(ULONG flags, ULONG data, Open32Key *decoded)
{
    ULONG scan = flags >> 24, key = flags & 2                 ? virtual_key(data >> 16)
                                    : scan < sizeof scan_keys ? scan_keys[scan]
                                                              : 0;
    ULONG character = (UCHAR)data;
    /* Keyboard control bytes retain their input meaning, independent of display glyphs. */
    if (character >= 32 && character != 127)
        character = WinCpTranslateChar(0, 437, (UCHAR)character, 1004);
    if (!key && character < 128)
        key = character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character;
    BOOL up = (flags & 0x40) != 0;
    if ((flags & 16) && character >= 'a' && character <= 'z')
        character = character - 'a' + 1;
    else if ((flags & 16) && character >= 'A' && character <= 'Z')
        character = character - 'A' + 1;
    BOOL extended = scan >= 0x60 && scan <= 0x69;
    static const unsigned char enhanced[] = {0x47, 0x48, 0x49, 0x4B, 0x4D,
                                             0x4F, 0x50, 0x51, 0x52, 0x53};
    if (extended)
        scan = enhanced[scan - 0x60];
    if (scan == 0x5A)
    {
        scan = 0x1C;
        extended = TRUE;
    }
    ULONG repeat = (flags >> 16) & 255;
    ULONG parameter = (repeat ? repeat : 1) | (scan & 255) << 16 | (extended ? 1u << 24 : 0) |
                      (flags & 32 ? 1u << 29 : 0) | ((up || (flags & 0x80)) ? 1u << 30 : 0) |
                      (up ? 1u << 31 : 0);
    *decoded = (Open32Key){
        (flags & 32 ? 0x104 : 0x100) + up, key, parameter, character, flags, !up && (flags & 1)};
}
void open32UpdateKey(const Open32Key *decoded)
{
    ULONG key = decoded->key, flags = decoded->flags;
    BOOL up = (flags & 0x40) != 0;
    down[16] = (flags & 8) != 0;
    down[17] = (flags & 16) != 0;
    down[18] = (flags & 32) != 0;
    if (key < 256)
    {
        if (!up && !down[key] && (key == 20 || key == 0x90 || key == 0x91))
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
    if (open32RunKeyboardHooks(0, key.key, key.parameter))
        return 0;
    ULONG result = open32WindowCallback(window, key.message, key.key, key.parameter);
    window = open32FindWindow(handle);
    if (window && key.text)
        result = open32WindowCallback(window, key.message == 0x104 ? 0x106 : 0x102, key.character,
                                      key.parameter);
    return result;
}
SHORT GetKeyState(LONG key)
{
    if (key < 0 || key > 255)
    {
        open32SetError(87);
        return 0;
    }
    return (SHORT)((down[key] ? 0x8000 : 0) | toggled[key]);
}

SHORT GetAsyncKeyState(LONG key)
{
    if (key < 0 || key > 255)
    {
        open32SetError(87);
        return 0;
    }
    if (!open32Presentation() || !lx_pm_poll_input())
        return 0;
    if (key == 1 || key == 2 || key == 4)
        return (SHORT)lx_pm_mouse_state(key == 4 ? 3 : (unsigned)key);
    if (key == 16)
        return (SHORT)(WinGetPhysKeyState(HWND_DESKTOP, 0x2A) |
                       WinGetPhysKeyState(HWND_DESKTOP, 0x36));
    if (key == 13)
        return (SHORT)(WinGetPhysKeyState(HWND_DESKTOP, 0x1C) |
                       WinGetPhysKeyState(HWND_DESKTOP, 0x5A));
    static const uint8 navigation[] = {0x24, 0x26, 0x21, 0x25, 0x27, 0x23, 0x28, 0x22, 0x2D, 0x2E};
    for (unsigned i = 0; i < sizeof navigation; i++)
        if (navigation[i] == key)
            return (SHORT)WinGetPhysKeyState(HWND_DESKTOP, 0x60 + i);
    for (unsigned scan = 1; scan < sizeof scan_keys; scan++)
        if (scan_keys[scan] == key)
            return (SHORT)WinGetPhysKeyState(HWND_DESKTOP, (LONG)scan);
    return 0;
}

BOOL GetKeyboardState(PVOID output)
{
    if (!output)
    {
        open32SetError(87);
        return FALSE;
    }
    uint8 *bytes = output;
    for (unsigned key = 0; key < 256; key++)
        bytes[key] = (down[key] ? 0x80 : 0) | toggled[key];
    return TRUE;
}
