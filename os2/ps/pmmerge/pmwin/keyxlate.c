/* Public domain. No rights reserved. Wholly AI-authored. */
/* Keyboard state and translation of host keys into WM_CHAR: virtual keys,
   scan codes and code-page characters. */
#include "os2/include/private/pmwinp.h"
#include "os2/ps/common/pm_text.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>

enum
{
    KEY_TABLE_SIZE = 256,
    /* The WinGetKeyState and WinGetPhysKeyState bit for a key that is down. */
    KEY_STATE_DOWN = 0x8000,
    /* WinSetKeyboardStateTable bytes. */
    KEY_TABLE_DOWN = 0x80,
    KEY_TABLE_TOGGLED = 0x01,
    /* physical_keys bits: pressed since the last WinGetPhysKeyState, and held now. */
    PHYS_KEY_PRESSED = 1,
    PHYS_KEY_HELD = 2,
    /* WM_BUTTONnDOWN, WM_BUTTONnUP and WM_BUTTONnDBLCLK repeat for each button. */
    BUTTON_MESSAGE_STRIDE = WM_BUTTON2DOWN - WM_BUTTON1DOWN,
    /* A WM_CHAR character byte that marks an enhanced (E0-prefixed) key. */
    EXTENDED_KEY_PREFIX = 0xe0,
    /* PC scan codes, with OS/2's physical code for the keypad Enter key. */
    SCAN_ENTER = 0x1c,
    SCAN_CTRL = 0x1d,
    SCAN_LEFT_SHIFT = 0x2a,
    SCAN_RIGHT_SHIFT = 0x36,
    SCAN_ALT = 0x38,
    SCAN_F1 = 0x3b,
    SCAN_F10 = 0x44,
    SCAN_F11 = 0x57,
    SCAN_F12 = 0x58,
    SCAN_KP_ENTER = 0x5a
};

LONG WinGetKeyState(HWND desktop, LONG key)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || key < 0 || key > KEY_TABLE_SIZE - 1)
        return 0;
    BOOL down = key == VK_SHIFT  ? (anchor->modifiers & KC_SHIFT) != 0
                : key == VK_CTRL ? (anchor->modifiers & KC_CTRL) != 0
                : key == VK_ALT  ? (anchor->modifiers & KC_ALT) != 0
                                 : anchor->key_down[key] != 0;
    return (SHORT)((down ? KEY_STATE_DOWN : 0) | anchor->key_toggled[key]);
}

LONG WinGetPhysKeyState(HWND desktop, LONG scan)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || scan < 0 || scan > KEY_TABLE_SIZE - 1)
        return 0;
    unsigned state = anchor->physical_keys[scan];
    anchor->physical_keys[scan] &= ~(unsigned)PHYS_KEY_PRESSED;
    return (SHORT)((state & PHYS_KEY_HELD ? KEY_STATE_DOWN : 0) | (state & PHYS_KEY_PRESSED));
}

void synchronizeInputState(AnchorBlock *anchor, const QMSG *message)
{
    if (message->msg == WM_CHAR)
    {
        ULONG flags = (ULONG)(uintptr_t)message->mp1;
        unsigned key = (unsigned)((uintptr_t)message->mp2 >> 16) & 255;
        if ((flags & KC_VIRTUALKEY) && key)
        {
            BOOL down = !(flags & KC_KEYUP);
            if (down && !anchor->key_down[key] && !(flags & KC_PREVDOWN))
                anchor->key_toggled[key] ^= 1;
            anchor->key_down[key] = down;
        }
        anchor->modifiers = flags & (KC_SHIFT | KC_CTRL | KC_ALT);
    }
    else if (message->msg >= WM_BUTTON1DOWN && message->msg <= WM_BUTTON3UP)
    {
        ULONG index = message->msg - WM_BUTTON1DOWN;
        if (index % BUTTON_MESSAGE_STRIDE < WM_BUTTON1DBLCLK - WM_BUTTON1DOWN)
        {
            unsigned key = index / BUTTON_MESSAGE_STRIDE + VK_BUTTON1;
            BOOL down = index % BUTTON_MESSAGE_STRIDE == 0;
            if (down && !anchor->key_down[key])
                anchor->key_toggled[key] ^= 1;
            anchor->key_down[key] = down;
        }
    }
}

static unsigned virtual_key(uint32 key)
{
    switch (key)
    {
        case LX_HOST_KEY_BACKSPACE:
            return VK_BACKSPACE;
        case LX_HOST_KEY_TAB:
            return VK_TAB;
        case LX_HOST_KEY_RETURN:
            return VK_NEWLINE;
        case LX_HOST_KEY_KP_ENTER:
            return VK_ENTER;
        case LX_HOST_KEY_ESCAPE:
            return VK_ESC;
        case LX_HOST_KEY_HOME:
            return VK_HOME;
        case LX_HOST_KEY_END:
            return VK_END;
        case LX_HOST_KEY_LEFT:
            return VK_LEFT;
        case LX_HOST_KEY_RIGHT:
            return VK_RIGHT;
        case LX_HOST_KEY_UP:
            return VK_UP;
        case LX_HOST_KEY_DOWN:
            return VK_DOWN;
        case LX_HOST_KEY_DELETE:
            return VK_DELETE;
        case LX_HOST_KEY_INSERT:
            return VK_INSERT;
        case LX_HOST_KEY_PAGEUP:
            return VK_PAGEUP;
        case LX_HOST_KEY_PAGEDOWN:
            return VK_PAGEDOWN;
        case LX_HOST_KEY_SHIFT:
            return VK_SHIFT;
        case LX_HOST_KEY_CTRL:
            return VK_CTRL;
        case LX_HOST_KEY_ALT:
            return VK_ALT;
        default:
            break;
    }
    return key >= LX_HOST_KEY_F1 && key < LX_HOST_KEY_F1 + (VK_F24 - VK_F1 + 1)
               ? VK_F1 + key - LX_HOST_KEY_F1
               : 0;
}

typedef struct NavigationKey
{
    unsigned key, scan, character, control_character;
} NavigationKey;

/* Enhanced keys use OS/2 physical positions and BIOS extended translations. */
static const NavigationKey navigation_keys[] = {
    {LX_HOST_KEY_HOME, 0x60, 0x47, 0x77},   {LX_HOST_KEY_UP, 0x61, 0x48, 0x8d},
    {LX_HOST_KEY_PAGEUP, 0x62, 0x49, 0x84}, {LX_HOST_KEY_LEFT, 0x63, 0x4b, 0x73},
    {LX_HOST_KEY_RIGHT, 0x64, 0x4d, 0x74},  {LX_HOST_KEY_END, 0x65, 0x4f, 0x75},
    {LX_HOST_KEY_DOWN, 0x66, 0x50, 0x91},   {LX_HOST_KEY_PAGEDOWN, 0x67, 0x51, 0x76},
    {LX_HOST_KEY_INSERT, 0x68, 0x52, 0x92}, {LX_HOST_KEY_DELETE, 0x69, 0x53, 0x93},
};

static unsigned cp437(uint32 point)
{
    if (point < 128)
        return point;
    for (unsigned i = 128; i <= 255; i++)
        if (lx_pm_codepoint((uint8)i) == point)
            return i;
    return '?';
}

/* WM_CHAR carries the queue's code page, chosen with WinSetCp. */
static unsigned queue_character(AnchorBlock *anchor, uint32 point)
{
    unsigned character = cp437(point);
    ULONG codepage = anchor->message_queue.codepage;
    if (character < 128 || !codepage || codepage == 437)
        return character;
    UCHAR translated = WinCpTranslateChar(anchor->hab, 437, (UCHAR)character, codepage);
    return translated ? translated : character;
}

BOOL releaseInputState(AnchorBlock *anchor, ULONG timestamp)
{
    Window *window = getWindowFromHWND(anchor, anchor->focus);
    BOOL posted = FALSE;
    if (window)
    {
        for (unsigned scan = 1; scan < KEY_TABLE_SIZE; scan++)
            if (anchor->physical_keys[scan] & PHYS_KEY_HELD)
            {
                unsigned key = scan == SCAN_CTRL                                     ? VK_CTRL
                               : scan == SCAN_ALT                                    ? VK_ALT
                               : scan == SCAN_LEFT_SHIFT || scan == SCAN_RIGHT_SHIFT ? VK_SHIFT
                               : scan == SCAN_ENTER                                  ? VK_NEWLINE
                               : scan == SCAN_KP_ENTER                               ? VK_ENTER
                               : scan >= SCAN_F1 && scan <= SCAN_F10  ? VK_F1 + scan - SCAN_F1
                               : scan == SCAN_F11 || scan == SCAN_F12 ? VK_F11 + scan - SCAN_F11
                                                                      : 0;
                for (unsigned i = 0; i < sizeof navigation_keys / sizeof *navigation_keys; i++)
                    if (navigation_keys[i].scan == scan)
                        key = virtual_key(navigation_keys[i].key);
                posted |= postTimestampedMsg(anchor, window->hwnd, WM_CHAR,
                                             (MPARAM)(uintptr_t)(KC_KEYUP | KC_SCANCODE |
                                                                 (key ? KC_VIRTUALKEY : 0) |
                                                                 1u << 16 | scan << 24),
                                             (MPARAM)(uintptr_t)(key << 16), timestamp);
            }
        for (unsigned button = VK_BUTTON1; button <= VK_BUTTON3; button++)
            if (anchor->key_down[button])
                posted |= postTimestampedMsg(anchor, window->hwnd,
                                             WM_BUTTON1UP + (button - 1) * BUTTON_MESSAGE_STRIDE, 0,
                                             0, timestamp);
    }
    memset(anchor->physical_keys, 0, sizeof anchor->physical_keys);
    memset(anchor->key_down, 0, sizeof anchor->key_down);
    anchor->modifiers = anchor->text_modifiers = anchor->text_scancode = anchor->text_repeat = 0;
    return posted;
}

BOOL processKeyboardEvent(AnchorBlock *anchor, const LxHostEvent *event)
{
    const NavigationKey *navigation = NULL;
    for (unsigned i = 0; i < sizeof navigation_keys / sizeof navigation_keys[0]; i++)
        if (navigation_keys[i].key == event->key)
            navigation = &navigation_keys[i];
    unsigned scan = navigation                           ? navigation->scan
                    : event->key == LX_HOST_KEY_KP_ENTER ? SCAN_KP_ENTER
                                                         : event->scancode;
    if ((event->type == LX_HOST_EVENT_KEY_DOWN || event->type == LX_HOST_EVENT_KEY_UP) && scan &&
        scan < KEY_TABLE_SIZE)
    {
        uint8 *state = &anchor->physical_keys[scan];
        *state = event->type == LX_HOST_EVENT_KEY_DOWN ? PHYS_KEY_HELD | PHYS_KEY_PRESSED
                                                       : (*state & PHYS_KEY_PRESSED);
    }
    Window *window = getWindowFromHWND(anchor, anchor->focus);
    if (!window || window->destroying)
        return FALSE;
    if (!windowAcceptsInput(window))
        return FALSE;
    ULONG flags = (event->modifiers & LX_HOST_MOD_SHIFT ? KC_SHIFT : 0) |
                  (event->modifiers & LX_HOST_MOD_CTRL ? KC_CTRL : 0) |
                  (event->modifiers & LX_HOST_MOD_ALT ? KC_ALT : 0);
    if (event->type == LX_HOST_EVENT_TEXT)
        flags = anchor->text_modifiers;

    if (event->type == LX_HOST_EVENT_TEXT)
    {
        const uint8 *p = (const uint8 *)event->text;
        BOOL posted = FALSE;
        for (unsigned i = 0; i < sizeof event->text && p[i];)
        {
            uint32 point = p[i++];
            unsigned extra = point >= 0xf0 ? 3 : point >= 0xe0 ? 2 : point >= 0xc0 ? 1 : 0;
            if (extra)
                point &= (1u << (6 - extra)) - 1;
            while (extra && i < sizeof event->text && (p[i] & 0xc0) == 0x80)
            {
                point = point << 6 | (p[i++] & 0x3f);
                extra--;
            }
            if (extra)
                continue;
            posted |= postTimestampedMsg(
                anchor, window->hwnd, WM_CHAR,
                (MPARAM)(uintptr_t)(flags | KC_CHAR | (anchor->text_scancode ? KC_SCANCODE : 0) |
                                    (anchor->text_repeat ? KC_PREVDOWN : 0) | 1u << 16 |
                                    anchor->text_scancode << 24),
                (MPARAM)(uintptr_t)queue_character(anchor, point), (ULONG)event->timestamp);
        }
        return posted;
    }
    unsigned key = virtual_key(event->key);
    if (!key && event->type == LX_HOST_EVENT_KEY_DOWN && !(flags & (KC_CTRL | KC_ALT)))
    {
        anchor->text_scancode = event->scancode;
        anchor->text_modifiers = flags;
        anchor->text_repeat = event->repeat;
        return FALSE; /* Printable keydowns arrive as text. */
    }
    if (event->type == LX_HOST_EVENT_KEY_UP)
        flags |= KC_KEYUP;
    if (event->repeat)
        flags |= KC_PREVDOWN;
    ULONG character = event->key;
    if ((flags & KC_SHIFT) && character >= 'a' && character <= 'z')
        character -= 'a' - 'A';
    ULONG parameter = key ? key << 16 : character;
    unsigned control = event->key == LX_HOST_KEY_RETURN || event->key == LX_HOST_KEY_KP_ENTER ? '\r'
                       : event->key == LX_HOST_KEY_TAB                                        ? '\t'
                       : event->key == LX_HOST_KEY_BACKSPACE                                  ? '\b'
                       : event->key == LX_HOST_KEY_ESCAPE ? '\033'
                                                          : 0;
    if (control)
    {
        if (event->type == LX_HOST_EVENT_KEY_DOWN)
            flags |= KC_CHAR; /* Control characters are valid only on keydown. */
        parameter |= control;
        if (event->key == LX_HOST_KEY_ESCAPE || event->type == LX_HOST_EVENT_KEY_UP)
            parameter |= (event->key == LX_HOST_KEY_KP_ENTER ? EXTENDED_KEY_PREFIX : scan) << 8;
    }
    if (navigation)
        parameter |=
            ((flags & KC_CTRL ? navigation->control_character : navigation->character) << 8) |
            EXTENDED_KEY_PREFIX;
    BOOL posted =
        postTimestampedMsg(anchor, window->hwnd, WM_CHAR,
                           (MPARAM)(uintptr_t)(flags | (key ? KC_VIRTUALKEY : KC_CHAR) |
                                               (scan ? KC_SCANCODE : 0) | 1u << 16 | scan << 24),
                           (MPARAM)(uintptr_t)parameter, (ULONG)event->timestamp);
    if (key && event->type != LX_HOST_EVENT_KEY_UP)
        posted |= postSemanticKey(anchor, window->hwnd, key, flags, (ULONG)event->timestamp);
    return posted;
}

BOOL WinSetKeyboardStateTable(HWND desktop, PBYTE table, BOOL set)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || desktop != HWND_DESKTOP || !table)
        return FALSE;
    for (unsigned i = 0; i < KEY_TABLE_SIZE; i++)
    {
        if (set)
        {
            a->key_down[i] = (table[i] & KEY_TABLE_DOWN) != 0;
            a->key_toggled[i] = (table[i] & KEY_TABLE_TOGGLED) != 0;
        }
        else
            table[i] =
                (a->key_down[i] ? KEY_TABLE_DOWN : 0) | (a->key_toggled[i] ? KEY_TABLE_TOGGLED : 0);
    }
    return TRUE;
}
