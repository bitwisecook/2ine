# Public domain. No rights reserved. Wholly AI-authored.
"""Generated guests for Open32 pixels, fonts, file IO and variadic formatting."""

import struct

from images import FlatGuest, linear_image

API = {
    "CreateBitmap": 58,
    "SetDIBits": 608,
    "GetTextExtentPoint": 368,
    "PtInRect": 531,
    "InflateRect": 417,
    "OffsetRect": 498,
    "SubtractRect": 680,
    "IntersectRect": 425,
    "UnionRect": 701,
    "EqualRect": 187,
    "IsRectEmpty": 447,
    "CopyRect": 55,
    "SetRectEmpty": 640,
    "SetRect": 639,
    "InvertRect": 428,
    "FrameRect": 220,
    "GetClipBox": 245,
    "GetAsyncKeyState": 1012,
    "GetSystemMenu": 358,
    "GetCurrentProcessId": 257,
    "GetWindowThreadProcessId": 391,
    "GetMessagePos": 313,
    "GetMessageTime": 314,
    "PostQuitMessage": 528,
    "PostMessage": 527,
    "DispatchMessage": 149,
    "TranslateMessage": 699,
    "GetMessage": 311,
    "PeekMessage": 513,
    "Rectangle": 539,
    "GetROP2": 346,
    "SetROP2": 638,
    "LineTo": 456,
    "MoveToEx": 491,
    "CreatePen": 87,
    "BringWindowToTop": 19,
    "GetCaretBlinkTime": 236,
    "SetCaretBlinkTime": 598,
    "GetCaretPos": 237,
    "SetCaretPos": 599,
    "ShowCaret": 667,
    "HideCaret": 414,
    "DestroyCaret": 142,
    "CreateCaret": 60,
    "GetWindow": 382,
    "GetClassName": 242,
    "GetClassLong": 241,
    "ShowScrollBar": 670,
    "GetScrollPos": 351,
    "SetScrollPos": 642,
    "GetScrollRange": 352,
    "SetScrollRange": 643,
    "GetUpdateRect": 376,
    "ValidateRect": 707,
    "InvalidateRect": 426,
    "IsWindow": 448,
    "IsWindowVisible": 450,
    "IsWindowEnabled": 449,
    "EnableWindow": 168,
    "GetTextExtentPoint32": 369,
    "ExcludeClipRect": 190,
    "IntersectClipRect": 424,
    "GetMapMode": 304,
    "SetMapMode": 626,
    "lstrcpy": 481,
    "lstrcat": 478,
    "lstrlen": 482,
    "LoadString": 464,
    "GetDIBits": 267,
    "GlobalReAlloc": 406,
    "GlobalHandle": 403,
    "GlobalSize": 407,
    "GlobalFlags": 400,
    "GlobalUnlock": 408,
    "GlobalLock": 404,
    "GlobalFree": 401,
    "GlobalAlloc": 396,
    "LocalReAlloc": 472,
    "LocalHandle": 470,
    "LocalSize": 473,
    "LocalFlags": 468,
    "LocalUnlock": 474,
    "LocalLock": 471,
    "LocalFree": 469,
    "LocalAlloc": 465,
    "GetWindowTextLength": 390,
    "GetWindowText": 389,
    "SetWindowText": 663,
    "FindClose": 210,
    "FindNextFile": 212,
    "FindFirstFile": 211,
    "CreateFont": 75,
    "GetParent": 333,
    "EndPaint": 173,
    "BeginPaint": 16,
    "RedrawWindow": 540,
    "DrawMenuBar": 161,
    "GetMenuString": 310,
    "IsIconic": 445,
    "GetKeyState": 297,
    "UnhookWindowsHookEx": 700,
    "CallNextHookEx": 21,
    "SetWindowsHookEx": 665,
    "SetClipboardViewer": 603,
    "GetClipboardViewer": 251,
    "ChangeClipboardChain": 23,
    "CreateRectRgn": 93,
    "CombineRgn": 47,
    "GetRgnBox": 349,
    "SelectClipRgn": 584,
    "CreateIC": 78,
    "GetDeviceCaps": 269,
    "SetProp": 637,
    "GetProp": 344,
    "RemoveProp": 566,
    "GlobalFindAtom": 399,
    "SendMessage": 588,
    "CallWindowProc": 22,
    "GetClientRect": 244,
    "GetWindowRect": 388,
    "ClientToScreen": 39,
    "ScreenToClient": 579,
    "GetModuleHandle": 319,
    "GetProcAddress": 341,
    "RegisterClass": 556,
    "CreateWindowEx": 99,
    "SetWindowLong": 659,
    "DestroyWindow": 146,
    "UnregisterClass": 705,
    "GetCapture": 235,
    "SetCapture": 597,
    "ReleaseCapture": 559,
    "IsMenu": 446,
    "RemoveMenu": 565,
    "DeleteMenu": 138,
    "CreateMenu": 82,
    "CreatePopupMenu": 91,
    "DestroyMenu": 145,
    "InsertMenu": 420,
    "AppendMenu": 9,
    "GetMenuItemCount": 307,
    "GetMenuItemID": 308,
    "GetSubMenu": 355,
    "GetMenuState": 309,
    "CheckMenuItem": 33,
    "EnableMenuItem": 166,
    "CreatePalette": 85,
    "SelectPalette": 586,
    "RealizePalette": 536,
    "GetPaletteEntries": 332,
    "GetNearestPaletteIndex": 321,
    "GetCurrentDirectory": 253,
    "SetCurrentDirectory": 604,
    "RegisterClipboardFormat": 557,
    "GetLocalTime": 301,
    "GlobalAddAtom": 395,
    "GlobalDeleteAtom": 397,
    "CreateSolidBrush": 97,
    "FillRect": 207,
    "GetDC": 263,
    "ReleaseDC": 560,
    "CreateCompatibleDC": 62,
    "CreateCompatibleBitmap": 61,
    "LoadBitmap": 458,
    "GetObject": 326,
    "SelectObject": 585,
    "GetPixel": 335,
    "BitBlt": 18,
    "DeleteDC": 135,
    "DeleteObject": 140,
    "CreateFontIndirect": 76,
    "GetStockObject": 353,
    "DrawText": 162,
    "SetBkMode": 594,
    "SetTextColor": 649,
    "TextOut": 689,
    "OpenFile": 504,
    "ReadFile": 535,
    "WriteFile": 717,
    "CloseHandle": 44,
    "_llseek": 725,
    "_lwrite": 728,
    "_lread": 727,
    "_lclose": 723,
    "wsprintf": 720,
    "wvsprintf": 721,
    "GetLastError": 300,
    "GetTextMetrics": 371,
    "EnumFontFamilies": 179,
    "InitializeCriticalSection": 419,
    "EnterCriticalSection": 175,
    "LeaveCriticalSection": 454,
    "DeleteCriticalSection": 134,
    "GetCurrentThreadId": 259,
    "CreateThread": 98,
    "GetExitCodeThread": 1006,
    "ResumeThread": 573,
    "WaitForSingleObject": 711,
    "ExitThread": 193,
    "Sleep": 673,
    "GetSystemTime": 361,
    "GetLocaleInfo": 2055,
    "SystemTimeToFileTime": 684,
}


def store(g, address):
    g.code.extend(b"\xa3" + struct.pack("<I", address))


def call(g, name, *arguments):
    g.call(2, API[name], *arguments)


def drawing_program():
    g = FlatGuest()
    screen, source, target, bitmap, canvas, old_source, old_target, font, old_font = (
        g.field(bytes(4)) for _ in range(9)
    )
    info = g.field(bytes(60))
    rectangle = g.field(struct.pack("<4i", 0, 0, 100, 40))
    text = g.field(b"Hello\0")
    logfont = g.field(
        struct.pack("<5i8B32s", -16, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, b"Courier")
    )
    driver = g.field(b"DISPLAY\0")
    information = g.field(bytes(4))
    call(g, "CreateIC", driver, 0, 0, 0)
    store(g, information)
    call(g, "GetDeviceCaps", (information,), 88)
    g.result(96, 123)
    call(g, "TextOut", (information,), 0, 0, text, 5)
    g.result(0, 124)
    call(g, "DeleteDC", (information,))
    g.result(1, 125)
    call(g, "GetDC", 0)
    store(g, screen)
    for address in (source, target):
        call(g, "CreateCompatibleDC", (screen,))
        store(g, address)
    call(g, "LoadBitmap", 0, 99)
    store(g, bitmap)
    call(g, "GetObject", (bitmap,), 24, info)
    g.result(24, 1)
    g.word(info + 4, 2, 2)
    g.word(info + 8, 2, 3)
    call(g, "SelectObject", (source,), (bitmap,))
    store(g, old_source)
    for x, y, pixel in ((0, 0, 0xFF0000), (0, 1, 0xFF), (1, 0, 0xFFFFFF), (1, 1, 0xFF00)):
        call(g, "GetPixel", (source,), x, y)
        g.result(pixel, 4)
    call(g, "DeleteObject", (bitmap,))
    g.result(0, 5)
    call(g, "CreateCompatibleBitmap", (screen,), 128, 64)
    store(g, canvas)
    call(g, "SelectObject", (target,), (canvas,))
    store(g, old_target)
    call(g, "BitBlt", (target,), 1, 1, 2, 2, (source,), 0, 0, 0xCC0020)
    g.result(1, 6)
    for x, y, pixel in ((0, 0, 0), (1, 1, 0xFF0000), (1, 2, 0xFF), (2, 1, 0xFFFFFF)):
        call(g, "GetPixel", (target,), x, y)
        g.result(pixel, 7)
    brush, previous_brush = (g.field(bytes(4)) for _ in range(2))
    fill = g.field(struct.pack("<4i", 0, 50, 4, 54))
    call(g, "CreateSolidBrush", 0xFF)
    store(g, brush)
    call(g, "GetObject", (brush,), 12, info)
    g.result(12, 110)
    g.word(info, 0, 111)
    g.word(info + 4, 0xFF, 112)
    call(g, "SelectObject", (target,), (brush,))
    store(g, previous_brush)
    call(g, "DeleteObject", (brush,))
    g.result(0, 113)
    call(g, "FillRect", (target,), fill, (brush,))
    g.result(1, 114)
    call(g, "GetPixel", (target,), 1, 50)
    g.result(0xFF, 115)
    call(g, "GetPixel", (target,), 1, 49)
    g.result(0, 116)
    call(g, "GetStockObject", 5)
    store(g, old_font)
    call(g, "FillRect", (target,), fill, (old_font,))
    g.result(1, 117)
    call(g, "GetPixel", (target,), 1, 50)
    g.result(0xFF, 118)
    call(g, "DeleteObject", (old_font,))
    g.result(0, 119)
    call(g, "SelectObject", (target,), (previous_brush,))
    call(g, "DeleteObject", (brush,))
    g.result(1, 120)
    call(g, "CreateFontIndirect", logfont)
    store(g, font)
    call(g, "SelectObject", (target,), (font,))
    store(g, old_font)
    call(g, "GetObject", (font,), 60, info)
    g.result(60, 8)
    g.word(info, 0xFFFFFFF0, 9)
    g.word(info + 28, int.from_bytes(b"Cour", "little"), 10)
    call(g, "DeleteObject", (font,))
    g.result(0, 11)
    wide = g.field(struct.pack("<4i", 0, 0, 100, 40))
    call(g, "DrawText", (target,), text, 5, wide, 0x20)
    call(g, "GetPixel", (target,), 0, 0)
    g.result(0xFFFFFF, 121)
    call(g, "GetPixel", (target,), 99, 39)
    g.result(0, 122)
    call(g, "DrawText", (target,), text, 5, rectangle, 0x420)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 12)
    call(g, "SetBkMode", (target,), 1)
    g.result(2, 13)
    call(g, "SetTextColor", (target,), 0xFF)
    g.result(0, 14)
    call(g, "TextOut", (target,), 4, 16, text, 5)
    g.result(1, 15)
    # Scan the text band for actual red glyph coverage.
    x, y, colored = g.field(bytes(4)), g.field(struct.pack("<I", 16)), g.field(bytes(4))
    loop = len(g.code)
    call(g, "GetPixel", (target,), (x,), (y,))
    g.code.extend(b"\x25\xff\0\0\0\x09\xc0\x74\x0a\xc7\x05")
    g.code.extend(struct.pack("<II", colored, 1))
    g.code.extend(b"\xff\x05" + struct.pack("<I", x))
    g.code.extend(b"\x81\x3d" + struct.pack("<II", x, 80) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", x, 0))
    g.code.extend(b"\xff\x05" + struct.pack("<I", y))
    g.code.extend(b"\x81\x3d" + struct.pack("<II", y, 40) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.word(colored, 1, 16)
    call(g, "SelectObject", (target,), (old_font,))
    call(g, "DeleteObject", (font,))
    g.result(1, 17)
    for address in (source, target):
        call(g, "DeleteDC", (address,))
        g.result(1, 18)
    for address in (bitmap, canvas):
        call(g, "DeleteObject", (address,))
        g.result(1, 19)
    call(g, "ReleaseDC", 0, (screen,))
    g.result(1, 20)
    g.exit(0)
    bits = bytes.fromhex("0000ff00ff000000ff0000ffffff0000")
    image = struct.pack("<2sIHHI", b"BM", 42, 0, 0, 26)
    image += struct.pack("<IHHHH", 12, 2, 2, 1, 24) + bits
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX"], "DRAW32", data=g.data, resources=[(2, 99, image)]
    )


def file_program():
    g = FlatGuest()
    name = g.field(b"io.txt\0")
    structure, handle, count, buffer = (g.field(bytes(n)) for n in (136, 4, 4, 32))
    text = g.field(b"hello\0")
    call(g, "OpenFile", name, structure, 0x1002)
    store(g, handle)
    call(g, "WriteFile", (handle,), text, 5, count, 0)
    g.result(1, 30)
    g.word(count, 5, 31)
    call(g, "_llseek", (handle,), 0, 0)
    g.result(0, 32)
    call(g, "ReadFile", (handle,), buffer, 9, count, 0)
    g.result(1, 33)
    g.word(count, 5, 34)
    g.word(buffer, int.from_bytes(b"hell", "little"), 35)
    call(g, "ReadFile", (handle,), buffer, 9, count, 0)
    g.result(1, 36)
    g.word(count, 0, 37)
    call(g, "_llseek", (handle,), 2, 0)
    g.result(2, 38)
    call(g, "_lwrite", (handle,), 0, 0)
    g.result(0, 39)
    call(g, "CloseHandle", (handle,))
    g.result(1, 40)
    call(g, "_lclose", (handle,))
    g.result(0xFFFFFFFF, 41)
    call(g, "WriteFile", (handle,), 0, 0, count, 0)
    g.result(0, 44)
    call(g, "GetLastError")
    g.result(6, 45)

    call(g, "OpenFile", 0xDEADBEEF, structure, 0x8000)
    store(g, handle)
    call(g, "_lread", (handle,), buffer, 9)
    g.result(2, 42)
    call(g, "_lclose", (handle,))
    g.result(0, 43)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FILE32", data=g.data)


def format_program():
    g = FlatGuest()
    text = g.field(b"hello\0")
    fmt = g.field(b"%s %04x %d %u %%\0")
    short = g.field(b"%*.*s\0")
    output = g.field(bytes(1024))
    args = g.field(struct.pack("<4I", text, 42, 0xFFFFFFF9, 0xFFFFFFFF))
    expected = b"hello 002a -7 4294967295 %\0"
    for name, tail in (("wsprintf", (text, 42, 0xFFFFFFF9, 0xFFFFFFFF)), ("wvsprintf", (args,))):
        call(g, name, output, fmt, *tail)
        g.result(len(expected) - 1, 50)
        for offset in range(0, len(expected) - 3, 4):
            g.word(output + offset, int.from_bytes(expected[offset : offset + 4], "little"), 51)
    call(g, "wsprintf", output, short, 5, 3, text)
    g.result(5, 52)
    g.word(output, int.from_bytes(b"  he", "little"), 53)
    call(g, "wvsprintf", output, fmt, 0xDEADBEEF)
    g.result(0xFFFFFFFF, 54)
    call(g, "GetLastError")
    g.result(87, 55)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FORMAT32", data=g.data)


def font_program():
    g = FlatGuest()
    screen, count, stopping = (g.field(bytes(4)) for _ in range(3))
    metrics, output = g.field(bytes(56)), g.field(bytes(16))
    family, missing, fmt = g.field(b"Courier\0"), g.field(b"AbsentFont\0"), g.field(b"%u\0")
    call(g, "GetDC", 0)
    store(g, screen)
    call(g, "GetTextMetrics", (screen,), metrics)
    g.result(1, 60)
    g.load(metrics)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 61)
    font, previous = g.field(bytes(4)), g.field(bytes(4))
    logfont = g.field(bytes(60))
    call(g, "CreateFont", 0xFFFFFFEC, 0, 0, 0, 700, 1, 1, 0, 0, 0, 0, 0, 1, family)
    store(g, font)
    call(g, "GetObject", (font,), 60, logfont)
    g.result(60, 56)
    g.word(logfont, 0xFFFFFFEC, 57)
    g.word(logfont + 16, 700, 58)
    g.word(logfont + 20, 0x101, 59)
    call(g, "SelectObject", (screen,), (font,))
    store(g, previous)
    call(g, "GetTextMetrics", (screen,), metrics)
    g.result(1, 60)
    extent = g.field(bytes(8))
    measured = g.field(struct.pack("<4I", 0, 0, 1000, 1000))
    word = g.field(b"Workbook\0")
    call(g, "GetTextExtentPoint32", (screen,), word, 8, extent)
    g.result(1, 80)
    call(g, "DrawText", (screen,), word, 8, measured, 0x420)
    g.load(extent)
    g.require(b"\x3b\x05" + struct.pack("<I", measured + 8), 81)
    g.load(extent + 4)
    g.require(b"\x3b\x05" + struct.pack("<I", measured + 12), 82)
    call(g, "SelectObject", (screen,), (previous,))
    call(g, "DeleteObject", (font,))
    g.result(1, 61)
    call(g, "EnumFontFamilies", (screen,), family, 0x77665544, 42)
    g.result(1, 62)
    g.word(count, 4, 63)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 0))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", stopping, 2))
    call(g, "EnumFontFamilies", (screen,), 0, 0x77665544, 42)
    g.result(0, 64)
    g.word(count, 2, 65)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 0))
    call(g, "EnumFontFamilies", (screen,), missing, 0x77665544, 42)
    g.result(0, 66)
    g.word(count, 0, 67)
    call(g, "ReleaseDC", 0, (screen,))
    g.result(1, 68)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.require(b"\x81\x7c\x24\x10" + struct.pack("<I", 42), 69)
    g.code.extend(b"\x8b\x44\x24\x04")
    g.require(b"\x81\x38" + struct.pack("<I", 0xFFFFFFF0), 70)
    g.code.extend(b"\x8b\x44\x24\x08\x8b\x00")
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 71)
    call(g, "wsprintf", output, fmt, 42)
    g.result(2, 72)
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    g.load(count)
    g.code.extend(b"\x3b\x05" + struct.pack("<I", stopping) + b"\x0f\x95\xc0\x0f\xb6\xc0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x77665544), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FONTS32", data=g.data)


def critical_program():
    g = FlatGuest()
    section = g.field(bytes(24))
    call(g, "InitializeCriticalSection", section)
    g.word(section + 4, 0xFFFFFFFF, 80)
    call(g, "GetCurrentThreadId")
    g.result(1, 81)
    call(g, "EnterCriticalSection", section)
    call(g, "EnterCriticalSection", section)
    g.word(section + 4, 1, 82)
    g.word(section + 8, 2, 83)
    g.word(section + 12, 1, 84)
    call(g, "DeleteCriticalSection", section)
    call(g, "GetLastError")
    g.result(170, 85)
    call(g, "LeaveCriticalSection", section)
    call(g, "LeaveCriticalSection", section)
    g.word(section + 4, 0xFFFFFFFF, 86)
    g.word(section + 12, 0, 87)
    call(g, "DeleteCriticalSection", section)
    call(g, "EnterCriticalSection", section)
    call(g, "GetLastError")
    g.result(87, 88)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "CRIT32", data=g.data)


def thread_program():
    g = FlatGuest()
    section, handle, tid, result = (g.field(bytes(n)) for n in (24, 4, 4, 4))
    before, after = g.field(bytes(4)), g.field(bytes(4))
    call(g, "InitializeCriticalSection", section)
    call(g, "EnterCriticalSection", section)
    call(g, "CreateThread", 0, 0, 0x66554433, 0xBEEF, 4, tid)
    store(g, handle)
    g.word(tid, 2, 90)
    call(g, "WaitForSingleObject", (handle,), 0)
    g.result(258, 91)
    call(g, "GetExitCodeThread", (handle,), result)
    g.result(1, 92)
    g.word(result, 259, 93)
    call(g, "ResumeThread", (handle,))
    g.result(1, 94)
    # The worker announces its attempt before waiting on our critical section.
    loop = len(g.code)
    call(g, "Sleep", 1)
    g.load(before)
    g.code.extend(b"\x85\xc0\x0f\x84" + struct.pack("<i", loop - len(g.code) - 8))
    g.word(after, 0, 95)
    call(g, "LeaveCriticalSection", section)
    call(g, "WaitForSingleObject", (handle,), 3000)
    g.result(0, 96)
    g.word(after, 1, 97)
    call(g, "GetExitCodeThread", (handle,), result)
    g.result(1, 98)
    g.word(result, 43, 99)
    call(g, "CloseHandle", (handle,))
    g.result(1, 100)
    call(g, "GetExitCodeThread", (handle,), result)
    g.result(0, 101)
    call(g, "DeleteCriticalSection", section)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.require(b"\x81\x7c\x24\x04" + struct.pack("<I", 0xBEEF), 102)
    call(g, "GetCurrentThreadId")
    g.result(2, 103)
    call(g, "GetLastError")
    g.result(0, 104)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", before, 1))
    call(g, "EnterCriticalSection", section)
    g.word(section + 12, 2, 105)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", after, 1))
    call(g, "LeaveCriticalSection", section)
    g.code.extend(b"\xb8\x2b\0\0\0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x66554433), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "THREAD32", data=g.data)


def time_program(local=False):
    g = FlatGuest()
    output, actual = g.field(bytes(16)), g.field(bytes(4))
    call(g, "GetLocalTime" if local else "GetSystemTime", output)
    g.call(1, 282, 1, output, 16, actual)
    g.result(0, 130)
    g.word(actual, 16, 131)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "TIME32", data=g.data)


def filetime_program(fields, expected):
    g = FlatGuest()
    input = g.field(struct.pack("<8H", *fields))
    output = g.field(b"unchange")
    call(g, "SystemTimeToFileTime", input, output)
    g.result(expected is not None, 140)
    if expected is None:
        call(g, "GetLastError")
        g.result(87, 141)
        g.word(output, int.from_bytes(b"unch", "little"), 142)
    else:
        g.word(output, expected & 0xFFFFFFFF, 143)
        g.word(output + 4, expected >> 32, 144)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FTIME32", data=g.data)


def locale_program(kind, text):
    g = FlatGuest()
    output = g.field(bytes(256))
    wire = text.encode("latin1") + b"\0"
    call(g, "GetLocaleInfo", 0x400, kind, 0, 0)
    g.result(len(wire), 150)
    call(g, "GetLocaleInfo", 0x400, kind, output, len(wire))
    g.result(len(wire), 151)
    for i, value in enumerate(wire):
        g.require(b"\x80\x3d" + struct.pack("<I", output + i) + bytes([value]), 152)
    if len(wire) > 1:
        call(g, "GetLocaleInfo", 0x400, kind, output, len(wire) - 1)
        g.result(0, 153)
        call(g, "GetLastError")
        g.result(122, 154)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "LOCALE32", data=g.data)


def palette_program():
    g = FlatGuest()
    dc, palette, old = (g.field(bytes(4)) for _ in range(3))
    entries = g.field(struct.pack("<HH", 0x300, 3) + bytes.fromhex("00000000ff0000000000ff00"))
    output = g.field(bytes(16))
    call(g, "GetDC", 0)
    store(g, dc)
    call(g, "CreatePalette", entries)
    store(g, palette)
    call(g, "GetObject", (palette,), 2, output)
    g.result(2, 160)
    g.word(output, 3, 161)
    call(g, "GetPaletteEntries", (palette,), 0, 0, 0)
    g.result(3, 162)
    call(g, "GetPaletteEntries", (palette,), 1, 9, output)
    g.result(2, 163)
    g.word(output, 0xFF, 164)
    g.word(output + 4, 0xFF0000, 165)
    g.word(output + 8, 0, 166)
    call(g, "GetNearestPaletteIndex", (palette,), 0x0101FE)
    g.result(1, 167)
    call(g, "SelectPalette", (dc,), (palette,), 0)
    store(g, old)
    call(g, "GetStockObject", 15)
    g.require(b"\x3b\x05" + struct.pack("<I", old), 168)
    call(g, "DeleteObject", (palette,))
    g.result(0, 169)
    call(g, "RealizePalette", (dc,))
    g.result(0, 170)
    call(g, "SelectPalette", (dc,), (old,), 0)
    call(g, "DeleteObject", (palette,))
    g.result(1, 171)
    call(g, "DeleteObject", (old,))
    g.result(0, 172)
    call(g, "ReleaseDC", 0, (dc,))
    g.result(1, 173)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "PAL32", data=g.data)


def directory_program():
    g = FlatGuest()
    output = g.field(bytes(1024))
    length, required, actual = (g.field(bytes(4)) for _ in range(3))
    sub = g.field(b".\\subdir\\\0")
    absent = g.field(b"no-such-directory\0")
    name = g.field(b"TestFormat\0")
    lower = g.field(b"testformat\0")
    call(g, "RegisterClipboardFormat", name)
    store(g, length)
    call(g, "GlobalAddAtom", name)
    call(g, "GlobalDeleteAtom", (length,))
    call(g, "RegisterClipboardFormat", lower)
    g.require(b"\x3b\x05" + struct.pack("<I", length), 180)
    call(g, "GetCurrentDirectory", 0, 0)
    store(g, required)
    call(g, "GetCurrentDirectory", 1, output)
    g.require(b"\x3b\x05" + struct.pack("<I", required), 181)
    g.word(output, 0, 182)
    call(g, "SetCurrentDirectory", sub)
    g.result(1, 183)
    call(g, "SetCurrentDirectory", absent)
    g.result(0, 184)
    call(g, "GetCurrentDirectory", 1024, output)
    store(g, length)
    g.call(1, 282, 1, output, (length,), actual)
    g.result(0, 185)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "DIR32", data=g.data)


def menu_program():
    g = FlatGuest()
    menu, sub = g.field(bytes(4)), g.field(bytes(4))
    label = g.field(b"&File\0")
    item = g.field(b"&Open\tCtrl+O\0")
    call(g, "CreateMenu")
    store(g, menu)
    call(g, "CreatePopupMenu")
    store(g, sub)
    call(g, "AppendMenu", (sub,), 0, 7, item)
    g.result(1, 190)
    call(g, "InsertMenu", (menu,), 0, 0x410, (sub,), label)
    g.result(1, 191)
    buffer = g.field(bytes(32))
    call(g, "GetMenuString", (menu,), 7, 0, 0, 0)
    g.result(12, 180)
    call(g, "GetMenuString", (menu,), 7, buffer, 32, 0)
    g.result(12, 181)
    g.word(buffer, int.from_bytes(b"&Ope", "little"), 182)
    call(g, "GetMenuString", (menu,), 0, buffer, 4, 0x400)
    g.result(3, 183)
    g.word(buffer, int.from_bytes(b"&Fi\0", "little"), 184)
    call(g, "GetSubMenu", (menu,), 0)
    g.require(b"\x3b\x05" + struct.pack("<I", sub), 192)
    call(g, "GetMenuItemCount", (menu,))
    g.result(1, 193)
    call(g, "GetMenuItemID", (menu,), 0)
    g.result(0xFFFFFFFF, 194)
    call(g, "GetMenuState", (menu,), 0, 0x400)
    g.result(0x110, 195)
    call(g, "EnableMenuItem", (menu,), 7, 1)
    g.result(0, 196)
    call(g, "CheckMenuItem", (menu,), 7, 8)
    g.result(0, 197)
    call(g, "GetMenuState", (menu,), 7, 0)
    g.result(9, 198)
    call(g, "EnableMenuItem", (sub,), 0, 0x400)
    g.result(1, 199)
    call(g, "GetMenuState", (menu,), 7, 0)
    g.result(8, 200)
    call(g, "AppendMenu", (menu,), 0x800, 0, 0)
    g.result(1, 201)
    call(g, "GetMenuState", (menu,), 1, 0x400)
    g.result(0x800, 202)
    call(g, "AppendMenu", (sub,), 0x10, (menu,), label)
    g.result(0, 203)
    call(g, "RemoveMenu", (menu,), 0, 0x400)
    g.result(1, 207)
    call(g, "IsMenu", (sub,))
    g.result(1, 208)
    call(g, "InsertMenu", (menu,), 0, 0x410, (sub,), label)
    g.result(1, 209)
    call(g, "DeleteMenu", (menu,), 0, 0x400)
    g.result(1, 210)
    call(g, "IsMenu", (sub,))
    g.result(0, 211)
    call(g, "DestroyMenu", (menu,))
    g.result(1, 204)
    call(g, "GetMenuItemCount", (sub,))
    g.result(0xFFFFFFFF, 205)
    call(g, "DestroyMenu", (menu,))
    g.result(0, 206)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "MENU32", data=g.data)


def api_window_program():
    g = FlatGuest()
    library = g.field(b"PMWINX\0")
    name = g.field(b"NativeApiProcedure\0")
    procedure, atom, window = (g.field(bytes(4)) for _ in range(3))
    record = g.field(struct.pack("<10I", 0, 0, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "GetModuleHandle", library)
    store(g, atom)
    call(g, "GetProcAddress", (atom,), 131)
    store(g, procedure)
    store(g, record + 4)
    call(g, "RegisterClass", record)
    store(g, atom)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 212)
    call(g, "CreateWindowEx", 0, (atom,), name, 0, 10, 20, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 213)
    call(g, "SendMessage", (window,), 14, 0, 0)
    g.result(len("NativeApiProcedure"), 234)
    call(g, "IsWindow", (window,))
    g.result(1, 201)
    call(g, "IsWindowEnabled", (window,))
    g.result(1, 202)
    call(g, "EnableWindow", (window,), 0)
    g.result(0, 203)
    call(g, "IsWindowEnabled", (window,))
    g.result(0, 204)
    call(g, "EnableWindow", (window,), 1)
    g.result(1, 205)
    call(g, "IsWindowEnabled", (window,))
    g.result(1, 206)
    limits = g.field(bytes(8))
    call(g, "SetScrollRange", (window,), 1, 0xFFFFFFF6, 20, 0)
    g.result(1, 191)
    call(g, "GetScrollRange", (window,), 1, limits, limits + 4)
    g.result(1, 192)
    g.word(limits, 0xFFFFFFF6, 193)
    g.word(limits + 4, 20, 194)
    call(g, "SetScrollPos", (window,), 1, 100, 0)
    g.result(0, 195)
    call(g, "GetScrollPos", (window,), 1)
    g.result(20, 196)
    call(g, "SetScrollRange", (window,), 1, 0, 5, 0)
    g.result(1, 197)
    call(g, "GetScrollPos", (window,), 1)
    g.result(5, 198)
    call(g, "SetScrollRange", (window,), 1, 10, 5, 0)
    g.result(0, 199)
    call(g, "ShowScrollBar", (window,), 3, 0)
    g.result(1, 200)
    text = g.field(b"Spreadsheet title\0")
    text_output = g.field(bytes(32))
    call(g, "GetClassLong", (window,), 0xFFFFFFE8)
    g.require(b"\x3b\x05" + struct.pack("<I", procedure), 180)
    call(g, "GetClassName", (window,), text_output, 7)
    g.result(6, 181)
    g.word(text_output, int.from_bytes(b"Nati", "little"), 182)
    call(g, "SetWindowText", (window,), text)
    g.result(1, 210)
    call(g, "GetWindowTextLength", (window,))
    g.result(17, 211)
    call(g, "GetWindowText", (window,), text_output, 5)
    g.result(4, 212)
    g.word(text_output, int.from_bytes(b"Spre", "little"), 213)
    g.require(b"\x80\x3d" + struct.pack("<I", text_output + 4) + b"\0", 214)
    geometry = g.field(bytes(16))
    point = g.field(struct.pack("<2i", 5, 7))
    call(g, "GetClientRect", (window,), geometry)
    g.result(1, 223)
    g.word(geometry + 8, 100, 224)
    g.word(geometry + 12, 50, 225)
    call(g, "GetWindowRect", (window,), geometry)
    g.result(1, 226)
    for index, value in enumerate((10, 20, 110, 70)):
        g.word(geometry + 4 * index, value, 227)
    call(g, "ClientToScreen", (window,), point)
    g.result(1, 228)
    g.word(point, 15, 229)
    g.word(point + 4, 27, 230)
    call(g, "ScreenToClient", (window,), point)
    g.result(1, 231)
    g.word(point, 5, 232)
    g.word(point + 4, 7, 233)
    popup = g.field(bytes(4))
    call(g, "CreateWindowEx", 0, name, 0, 0x80000000, 40, 60, 80, 20, (window,), 0, 0xFFFFFFFF, 0)
    store(g, popup)
    call(g, "GetWindowRect", (popup,), geometry)
    g.result(1, 171)
    for index, value in enumerate((40, 60, 120, 80)):
        g.word(geometry + index * 4, value, 172)
    call(g, "GetWindow", (popup,), 4)
    g.require(b"\x3b\x05" + struct.pack("<I", window), 173)
    call(g, "GetWindow", (window,), 5)
    g.result(0, 174)
    call(g, "GetWindow", (popup,), 2)
    g.require(b"\x3b\x05" + struct.pack("<I", window), 175)
    children = [g.field(bytes(4)) for _ in range(2)]
    for index, child_handle in enumerate(children):
        call(
            g,
            "CreateWindowEx",
            0,
            name,
            0,
            0x40000000,
            0,
            0,
            10,
            10,
            (window,),
            20 + index,
            0xFFFFFFFF,
            0,
        )
        store(g, child_handle)
    call(g, "GetWindow", (window,), 5)
    g.require(b"\x3b\x05" + struct.pack("<I", children[1]), 161)
    call(g, "BringWindowToTop", (children[0],))
    g.result(1, 162)
    call(g, "GetWindow", (window,), 5)
    g.require(b"\x3b\x05" + struct.pack("<I", children[0]), 163)
    call(g, "GetWindow", (children[0],), 2)
    g.require(b"\x3b\x05" + struct.pack("<I", children[1]), 164)
    for child_handle in children:
        call(g, "DestroyWindow", (child_handle,))
        g.result(1, 165)
    call(g, "DestroyWindow", (popup,))
    g.result(1, 176)
    call(g, "SetWindowLong", (window,), 0xFFFFFFFC, (procedure,))
    g.require(b"\x3b\x05" + struct.pack("<I", procedure), 214)
    call(g, "SetWindowLong", (window,), 0xFFFFFFFC, 0xDEADBEEF)
    g.result(0, 215)
    call(g, "GetCapture")
    g.result(0, 216)
    call(g, "SetCapture", (window,))
    g.result(0, 217)
    call(g, "GetCapture")
    g.require(b"\x3b\x05" + struct.pack("<I", window), 218)
    call(g, "ReleaseCapture")
    g.result(1, 219)
    call(g, "GetCapture")
    g.result(0, 220)
    call(g, "SetClipboardViewer", (window,))
    g.result(0, 253)
    call(g, "GetClipboardViewer")
    g.require(b"\x3b\x05" + struct.pack("<I", window), 254)
    call(g, "ChangeClipboardChain", (window,), 0)
    g.result(1, 255)
    call(g, "GetClipboardViewer")
    g.result(0, 253)
    property_name = g.field(b"CellState\0")
    property_atom = g.field(bytes(4))
    call(g, "SetProp", (window,), property_name, 0xABCDEF)
    g.result(1, 248)
    call(g, "GlobalAddAtom", property_name)
    store(g, property_atom)
    call(g, "GetProp", (window,), (property_atom,))
    g.result(0xABCDEF, 249)
    call(g, "GlobalDeleteAtom", (property_atom,))
    call(g, "SetProp", (window,), property_name, 0x12345678)
    g.result(1, 250)
    call(g, "RemoveProp", (window,), property_name)
    g.result(0x12345678, 251)
    call(g, "GlobalFindAtom", property_name)
    g.result(0, 252)
    mdi_name = g.field(b"MDICLIENT\0")
    mdi_parameter = g.field(struct.pack("<2I", 0, 100))
    mdi, child, maximized = (g.field(bytes(4)) for _ in range(3))
    definition = g.field(
        struct.pack("<9I", name, name, 0xFFFFFFFF, 0, 0, 0x80000000, 0x80000000, 0, 0)
    )
    call(
        g,
        "CreateWindowEx",
        0,
        mdi_name,
        0,
        0x40000000,
        0,
        0,
        100,
        50,
        (window,),
        1,
        0xFFFFFFFF,
        mdi_parameter,
    )
    store(g, mdi)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 235)
    call(g, "SendMessage", (mdi,), 0x220, 0, definition)
    store(g, child)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 239)
    call(g, "SendMessage", (mdi,), 0x229, 0, maximized)
    g.require(b"\x3b\x05" + struct.pack("<I", child), 240)
    g.word(maximized, 0, 241)
    call(g, "GetClientRect", (child,), geometry)
    g.result(1, 242)
    g.word(geometry + 8, 100, 243)
    g.word(geometry + 12, 50, 244)
    call(g, "SendMessage", (mdi,), 0x221, (child,), 0)
    g.result(1, 245)
    call(g, "SendMessage", (mdi,), 0x229, 0, 0)
    g.result(0, 246)
    call(g, "DestroyWindow", (mdi,))
    g.result(1, 247)
    call(g, "DestroyWindow", (window,))
    g.result(1, 221)
    call(g, "UnregisterClass", (atom,), 0xFFFFFFFF)
    g.result(1, 222)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "APIPROC", data=g.data)


def region_program():
    g = FlatGuest()
    screen, dc, bitmap, a, b, combined, brush = (g.field(bytes(4)) for _ in range(7))
    rectangle = g.field(struct.pack("<4i", 0, 0, 8, 8))
    box = g.field(bytes(16))
    call(g, "GetDC", 0)
    store(g, screen)
    call(g, "SetMapMode", (screen,), 1)
    g.result(1, 21)
    call(g, "SetMapMode", (screen,), 8)
    g.result(0, 22)
    call(g, "GetLastError")
    g.result(50, 23)
    call(g, "GetMapMode", (screen,))
    g.result(1, 24)
    call(g, "CreateCompatibleDC", (screen,))
    store(g, dc)
    call(g, "CreateCompatibleBitmap", (screen,), 8, 8)
    store(g, bitmap)
    call(g, "SelectObject", (dc,), (bitmap,))
    call(g, "CreateRectRgn", 2, 1, 6, 4)
    store(g, a)
    call(g, "CreateRectRgn", 4, 2, 7, 6)
    store(g, b)
    call(g, "CreateRectRgn", 0, 0, 0, 0)
    store(g, combined)
    call(g, "GetRgnBox", (combined,), box)
    g.result(1, 1)
    call(g, "CombineRgn", (combined,), (a,), (b,), 1)
    g.result(2, 2)
    call(g, "GetRgnBox", (combined,), box)
    g.result(2, 3)
    for i, value in enumerate((4, 2, 6, 4)):
        g.word(box + 4 * i, value, 4)
    call(g, "CombineRgn", (combined,), (combined,), (b,), 3)
    g.result(3, 5)
    call(g, "SelectClipRgn", (dc,), (a,))
    g.result(2, 6)
    call(g, "DeleteObject", (a,))
    g.result(1, 7)
    call(g, "CreateSolidBrush", 0xFF)
    store(g, brush)
    call(g, "FillRect", (dc,), rectangle, (brush,))
    g.result(1, 8)
    call(g, "SelectClipRgn", (dc,), 0)
    g.result(2, 9)
    for x, y, value in ((2, 1, 0xFF), (5, 3, 0xFF), (6, 1, 0), (2, 0, 0), (2, 4, 0)):
        call(g, "GetPixel", (dc,), x, y)
        g.result(value, 10)
    call(g, "IntersectClipRect", (dc,), 1, 1, 7, 7)
    g.result(2, 25)
    call(g, "ExcludeClipRect", (dc,), 3, 2, 5, 5)
    g.result(3, 26)
    call(g, "GetClipBox", (dc,), box)
    g.result(3, 30)
    for index, value in enumerate((1, 1, 7, 7)):
        g.word(box + index * 4, value, 31)
    blue = g.field(bytes(4))
    call(g, "CreateSolidBrush", 0xFF0000)
    store(g, blue)
    call(g, "FillRect", (dc,), rectangle, (blue,))
    g.result(1, 27)
    call(g, "SelectClipRgn", (dc,), 0)
    for x, y, value in ((1, 1, 0xFF0000), (3, 2, 0xFF), (4, 4, 0), (5, 5, 0xFF0000), (7, 2, 0)):
        call(g, "GetPixel", (dc,), x, y)
        g.result(value, 28)
    call(g, "DeleteObject", (blue,))
    g.result(1, 29)
    call(g, "DeleteDC", (dc,))
    g.result(1, 11)
    for object in (bitmap, b, combined, brush):
        call(g, "DeleteObject", (object,))
        g.result(1, 12)
    call(g, "ReleaseDC", 0, (screen,))
    g.result(1, 13)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "REGION32", data=g.data)


def keyboard_program():
    g = FlatGuest()
    name = g.field(b"KeyboardContract\0")
    window, hook_a, hook_b, sequence, key, parameter, delivered, character = (
        g.field(bytes(4)) for _ in range(8)
    )
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, 0, 0, 0, 0, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    call(g, "IsIconic", (window,))
    g.result(0, 1)
    call(g, "SetWindowsHookEx", 2, 0x77665544, 0, 1)
    store(g, hook_b)
    call(g, "SetWindowsHookEx", 2, 0x66554433, 0, 1)
    store(g, hook_a)
    # F5 goes through newest hook A, then B; B consumes it.
    g.call(3, 920, (window,), 0x7A, 0x3F010006, 0x240000)
    g.word(sequence, 12, 2)
    g.word(key, 0x74, 3)
    g.word(parameter, 0x3F0001, 4)
    g.word(delivered, 0, 5)
    call(g, "GetKeyState", 0x74)
    g.code.extend(b"\x25\xff\xff\0\0")
    g.result(0x8000, 6)
    call(g, "UnhookWindowsHookEx", (hook_b,))
    g.result(1, 7)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", sequence, 0))
    g.call(3, 920, (window,), 0x7A, 0x3F010046, 0x240000)
    g.word(sequence, 1, 8)
    g.word(delivered, 1, 9)
    call(g, "GetKeyState", 0x74)
    g.result(0, 10)
    call(g, "UnhookWindowsHookEx", (hook_a,))
    g.result(1, 11)
    # A printable key reaches WM_KEYDOWN and WM_CHAR after hooks are removed.
    g.call(3, 920, (window,), 0x7A, 0x1E010005, ord("a"))
    g.word(delivered, 2, 12)
    g.word(character, ord("a"), 13)
    g.word(sequence, 1, 14)
    call(g, "DestroyWindow", (window,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    callbacks = []
    for digit in (1, 2):
        callbacks.append(0x10000 + len(g.code))
        # The callback logs chain order and preserves the supplied key packet.
        g.load(sequence)
        g.code.extend(b"\x6b\xc0\x0a\x83\xc0" + bytes([digit]))
        store(g, sequence)
        for offset, destination in ((8, key), (12, parameter)):
            g.code.extend(b"\x8b\x44\x24" + bytes([offset]))
            store(g, destination)
        if digit == 1:
            call(g, "CallNextHookEx", 0, 0, (key,), (parameter,))
            g.code.extend(b"\xc3")
        else:
            g.code.extend(b"\xb8\x11\0\0\0\xc3")
    procedure = 0x10000 + len(g.code)
    # Count Windows key messages, and retain WM_CHAR independently.
    g.code.extend(b"\x8b\x44\x24\x08\x3d\x00\x01\0\0\x74\x07")
    g.code.extend(b"\x3d\x01\x01\0\0\x75\x06\xff\x05" + struct.pack("<I", delivered))
    g.code.extend(b"\x3d\x02\x01\0\0\x75\x09\x8b\x44\x24\x0c\xa3" + struct.pack("<I", character))
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    for sentinel, target in ((0x77665544, callbacks[1]), (0x66554433, callbacks[0])):
        g.code = g.code.replace(struct.pack("<I", sentinel), struct.pack("<I", target))
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", procedure))
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "KEYBOARD32", data=g.data
    )


def repaint_program():
    g = FlatGuest()
    name = g.field(b"RepaintContract\0")
    window, count, erased, menu = (g.field(bytes(4)) for _ in range(4))
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    paint = g.field(bytes(64))
    rectangle = g.field(struct.pack("<4i", 2, 3, 10, 20))
    query = g.field(bytes(16))
    clip_box = g.field(bytes(16))
    call(g, "RegisterClass", record)
    call(g, "CreateMenu")
    store(g, menu)
    text = g.field(b"&File\0")
    call(g, "AppendMenu", (menu,), 0, 42, text)
    call(g, "CreateWindowEx", 0, name, 0, 0x10000000, 10, 20, 100, 50, 0, (menu,), 0xFFFFFFFF, 0)
    store(g, window)
    call(g, "BeginPaint", (window,), paint)
    call(g, "EndPaint", (window,), paint)
    call(g, "RedrawWindow", (window,), rectangle, 0, 1)
    g.result(1, 1)
    g.call(3, 831, (window,), query)
    g.result(1, 2)
    for index, value in enumerate((2, 30, 10, 47)):
        g.word(query + index * 4, value, 3)
    call(g, "RedrawWindow", (window,), rectangle, 0, 8)
    g.result(1, 4)
    g.call(3, 831, (window,), query)
    g.result(0, 5)
    call(g, "RedrawWindow", (window,), rectangle, 0, 0x105)
    g.result(1, 6)
    g.word(count, 1, 7)
    for index, value in enumerate((2, 3, 10, 20)):
        g.word(clip_box + index * 4, value, 20)
    for index, value in enumerate((2, 3, 10, 20)):
        g.word(paint + 8 + index * 4, value, 8)
    g.word(erased, 2, 9)  # Initial paint, followed by the explicit erased repaint.
    g.call(3, 831, (window,), query)
    g.result(0, 10)
    call(g, "InvalidateRect", (window,), rectangle, 0)
    g.result(1, 14)
    call(g, "GetUpdateRect", (window,), query, 0)
    g.result(1, 15)
    for index, value in enumerate((2, 3, 10, 20)):
        g.word(query + index * 4, value, 16)
    call(g, "ValidateRect", (window,), rectangle)
    g.result(1, 17)
    call(g, "GetUpdateRect", (window,), query, 0)
    g.result(0, 18)
    for index in range(4):
        g.word(query + index * 4, 0, 19)
    call(g, "DrawMenuBar", (window,))
    g.result(1, 11)
    g.call(3, 834, (menu,), 5)
    g.require(b"\x3b\x05" + struct.pack("<I", window), 12)
    call(g, "DestroyWindow", (window,))
    g.result(1, 13)
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    procedure = 0x10000 + len(g.code)
    g.code.extend(b"\x83\x7c\x24\x08\x0f\x75")
    branch = len(g.code)
    g.code.extend(b"\0")
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    call(g, "BeginPaint", (window,), paint)
    call(g, "GetClipBox", (paint,), clip_box)
    g.result(2, 21)
    call(g, "EndPaint", (window,), paint)
    g.code.extend(b"\x31\xc0\xc3")
    g.code[branch] = len(g.code) - branch - 1
    g.code.extend(b"\x83\x7c\x24\x08\x14\x75\x06\xff\x05" + struct.pack("<I", erased))
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", procedure))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "REPAINT32", data=g.data)


def find_program(pattern, missing=0):
    g = FlatGuest()
    name = g.field(pattern.encode() + b"\0")
    output = g.field(bytes(320))
    handle, count = g.field(bytes(4)), g.field(bytes(4))
    call(g, "FindFirstFile", name, output)
    if missing:
        g.result(0xFFFFFFFF, 1)
        call(g, "GetLastError")
        g.result(missing, 2)
    else:
        store(g, handle)
        g.require(b"\x3d\xff\xff\xff\xff\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 3)
        loop = len(g.code)
        g.call(1, 282, 1, output, 320, count)
        g.result(0, 4)
        call(g, "FindNextFile", (handle,), output)
        g.code.extend(b"\x85\xc0\x0f\x85" + struct.pack("<i", loop - len(g.code) - 8))
        call(g, "GetLastError")
        g.result(18, 5)
        call(g, "FindClose", (handle,))
        g.result(1, 6)
        call(g, "FindClose", (handle,))
        g.result(0, 7)
        call(g, "GetLastError")
        g.result(6, 8)
        call(g, "FindNextFile", (handle,), output)
        g.result(0, 9)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "FIND32", data=g.data)


def memory_program():
    g = FlatGuest()
    fixed, movable, pointer = (g.field(bytes(4)) for _ in range(3))
    call(g, "LocalAlloc", 0x40, 32)
    store(g, fixed)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 1)
    g.load(fixed)
    g.require(b"\x83\x38\0", 2)
    g.code.extend(b"\xc7\x00\x78\x56\x34\x12")
    call(g, "LocalReAlloc", (fixed,), 8192, 0)
    g.result(0, 3)
    call(g, "LocalSize", (fixed,))
    g.result(32, 4)
    call(g, "LocalReAlloc", (fixed,), 8192, 2)
    store(g, fixed)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 5)
    g.load(fixed)
    g.require(b"\x81\x38\x78\x56\x34\x12", 6)
    call(g, "LocalFree", (fixed,))
    g.result(0, 7)
    call(g, "LocalAlloc", 0x42, 16)
    store(g, movable)
    call(g, "LocalLock", (movable,))
    store(g, pointer)
    g.code.extend(b"\xc7\x00\xef\xbe\xad\xde")
    call(g, "LocalLock", (movable,))
    g.require(b"\x3b\x05" + struct.pack("<I", pointer), 8)
    call(g, "LocalFlags", (movable,))
    g.result(2, 9)
    call(g, "LocalReAlloc", (movable,), 8192, 2)
    g.result(0, 10)
    call(g, "LocalFree", (movable,))
    g.require(b"\x3b\x05" + struct.pack("<I", movable), 11)
    call(g, "LocalUnlock", (movable,))
    g.result(1, 12)
    call(g, "LocalUnlock", (movable,))
    g.result(0, 13)
    call(g, "GetLastError")
    g.result(0, 14)
    call(g, "LocalReAlloc", (movable,), 8192, 0x42)
    g.require(b"\x3b\x05" + struct.pack("<I", movable), 15)
    call(g, "LocalLock", (movable,))
    g.require(b"\x81\x38\xef\xbe\xad\xde", 16)
    g.require(b"\x83\x78\x10\0", 17)
    call(g, "LocalHandle", (pointer,))
    g.result(0, 18)  # The old address is no longer associated after relocation.
    call(g, "LocalUnlock", (movable,))
    call(g, "LocalReAlloc", (movable,), 0, 2)
    g.require(b"\x3b\x05" + struct.pack("<I", movable), 19)
    call(g, "LocalFlags", (movable,))
    g.result(0x4000, 20)
    call(g, "LocalLock", (movable,))
    g.result(0, 21)
    call(g, "GetLastError")
    g.result(157, 22)
    call(g, "LocalReAlloc", (movable,), 8, 0x42)
    g.require(b"\x3b\x05" + struct.pack("<I", movable), 23)
    call(g, "LocalLock", (movable,))
    g.require(b"\x83\x38\0", 24)
    call(g, "LocalUnlock", (movable,))
    call(g, "LocalFree", (movable,))
    g.result(0, 25)
    call(g, "LocalFree", (movable,))
    g.require(b"\x3b\x05" + struct.pack("<I", movable), 26)
    call(g, "GetLastError")
    g.result(6, 27)
    call(g, "GlobalAlloc", 0x2040, 8)
    store(g, fixed)
    call(g, "GlobalLock", (fixed,))
    g.require(b"\x3b\x05" + struct.pack("<I", fixed), 28)
    call(g, "GlobalHandle", (fixed,))
    g.require(b"\x3b\x05" + struct.pack("<I", fixed), 29)
    call(g, "GlobalSize", (fixed,))
    g.result(8, 30)
    call(g, "GlobalFlags", (fixed,))
    g.result(0, 31)
    call(g, "GlobalFree", (fixed,))
    g.result(0, 32)
    call(g, "LocalFree", 0)
    g.result(0, 33)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "MEMORY32", data=g.data)


def dib_program():
    g = FlatGuest()
    dc, bitmap, memory = (g.field(bytes(4)) for _ in range(3))
    query = g.field(struct.pack("<I", 40) + bytes(36))
    info = g.field(struct.pack("<IiiHH6I", 40, 3, 2, 1, 4, 0, 0, 0, 0, 16, 0) + bytes(64))
    pixels = g.field(bytes(32) + b"guard123")
    call(g, "GetDC", 0)
    store(g, dc)
    call(g, "LoadBitmap", 0, 91)
    store(g, bitmap)
    call(g, "GetDIBits", (dc,), (bitmap,), 0, 0, 0, query, 0)
    g.result(2, 1)
    g.word(query + 4, 3, 2)
    g.word(query + 8, 2, 3)
    g.word(query + 12, 0x40001, 4)
    call(g, "GetDIBits", (dc,), (bitmap,), 0, 2, pixels, info, 0)
    g.result(2, 5)
    g.word(pixels, 0x21, 6)
    g.word(pixels + 4, 0x1030, 7)
    g.word(info + 40, 0x0000FF, 8)
    g.word(info + 44, 0x00FF00, 9)
    g.word(info + 48, 0xFF0000, 10)
    g.word(info + 52, 0xFFFFFF, 11)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", info + 8, 0xFFFFFFFE))
    call(g, "GetDIBits", (dc,), (bitmap,), 0, 99, pixels, info, 0)
    g.result(2, 12)
    g.word(pixels, 0x1030, 13)
    g.word(pixels + 4, 0x21, 14)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", info + 12, 0x180001))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", info + 32, 0))
    call(g, "GetDIBits", (dc,), (bitmap,), 1, 1, pixels, info, 0)
    g.result(1, 15)
    expected = bytes.fromhex("0000ff00ff00ff0000000000")
    for i in range(0, len(expected), 4):
        g.word(pixels + i, int.from_bytes(expected[i : i + 4], "little"), 16)
    g.word(pixels + 32, int.from_bytes(b"guar", "little"), 17)
    call(g, "CreateCompatibleDC", (dc,))
    store(g, memory)
    call(g, "SelectObject", (memory,), (bitmap,))
    call(g, "GetDIBits", (dc,), (bitmap,), 0, 1, pixels, info, 0)
    g.result(0, 18)
    call(g, "GetLastError")
    g.result(87, 19)
    call(g, "DeleteDC", (memory,))
    call(g, "DeleteObject", (bitmap,))
    g.result(1, 20)
    call(g, "ReleaseDC", 0, (dc,))
    g.exit(0)
    colors = [0x0000FF, 0x00FF00, 0xFF0000, 0xFFFFFF] + [0] * 12
    palette = b"".join(bytes([rgb & 255, rgb >> 8 & 255, rgb >> 16 & 255]) for rgb in colors)
    bitmap_data = (
        struct.pack("<IHHHH", 12, 3, 2, 1, 4) + palette + bytes.fromhex("2100000030100000")
    )
    resource = struct.pack("<2sIHHI", b"BM", 14 + len(bitmap_data), 0, 0, 74) + bitmap_data
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX"],
        "DIB32",
        data=g.data,
        resources=[(2, 91, resource)],
    )


def string_resource_program():
    g = FlatGuest()
    output = g.field(bytes(32))
    first, second = g.field(b"Cell\0"), g.field(b" A1\0")
    call(g, "lstrcpy", output, first)
    g.result(output, 11)
    call(g, "lstrcat", output, second)
    g.result(output, 12)
    call(g, "lstrlen", output)
    g.result(7, 13)
    g.word(output, int.from_bytes(b"Cell", "little"), 14)
    g.word(output + 4, int.from_bytes(b" A1\0", "little"), 15)
    call(g, "lstrlen", 0)
    g.result(0, 16)
    call(g, "lstrcpy", 0xDEADBEEF, first)
    g.result(0, 17)
    call(g, "LoadString", 0xFFFFFFFF, 37, output, 32)
    g.result(8, 1)
    g.word(output, int.from_bytes(b"Work", "little"), 2)
    g.word(output + 4, int.from_bytes(b"book", "little"), 3)
    g.word(output + 8, 0, 4)
    call(g, "LoadString", 0xFFFFFFFF, 0xFF000025, output, 4)
    g.result(3, 5)
    g.word(output, int.from_bytes(b"Wor\0", "little"), 6)
    call(g, "LoadString", 0xFFFFFFFF, 36, output, 32)
    g.result(0, 7)
    g.word(output, int.from_bytes(b"\0or\0", "little"), 8)
    call(g, "LoadString", 0, 37, output, 32)
    g.result(0, 9)
    call(g, "GetLastError")
    g.result(6, 10)
    g.exit(0)
    bundle = struct.pack("<H", 437)
    for i in range(16):
        text = b"Workbook\0" if i == 5 else b""
        bundle += bytes([len(text)]) + text
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX"],
        "STRINGS32",
        data=g.data,
        resources=[(5, 3, bundle)],
    )


def caret_program():
    g = FlatGuest()
    library = g.field(b"PMWINX\0")
    procedure = g.field(b"DEFWINDOWPROC\0")
    name = g.field(b"CaretContract\0")
    module, window, dc, brush = (g.field(bytes(4)) for _ in range(4))
    record = g.field(struct.pack("<10I", 0, 0, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    rectangle = g.field(struct.pack("<4i", 0, 0, 100, 50))
    point, paint, message = g.field(bytes(8)), g.field(bytes(64)), g.field(bytes(32))
    call(g, "GetModuleHandle", library)
    store(g, module)
    call(g, "GetProcAddress", (module,), procedure)
    store(g, record + 4)
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, 0, 0x10000000, 10, 10, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    call(g, "GetDC", (window,))
    store(g, dc)
    call(g, "CreateSolidBrush", 0xFFFFFF)
    store(g, brush)
    call(g, "FillRect", (dc,), rectangle, (brush,))
    g.result(1, 1)
    call(g, "HideCaret", (window,))
    g.result(0, 2)
    call(g, "SetCaretBlinkTime", 0xFFFFFFFF)
    g.result(1, 3)
    call(g, "CreateCaret", (window,), 0, 2, 8)
    g.result(1, 4)
    call(g, "SetCaretPos", 5, 5)
    g.result(1, 5)
    call(g, "ShowCaret", (window,))
    g.result(1, 6)
    call(g, "GetPixel", (dc,), 5, 5)
    g.result(0, 7)
    call(g, "HideCaret", (window,))
    g.result(1, 8)
    call(g, "GetPixel", (dc,), 5, 5)
    g.result(0xFFFFFF, 9)
    call(g, "ShowCaret", (window,))
    call(g, "ShowCaret", (window,))
    call(g, "HideCaret", (window,))
    call(g, "GetPixel", (dc,), 5, 5)
    g.result(0, 10)
    call(g, "SetCaretPos", 10, 7)
    call(g, "GetPixel", (dc,), 5, 5)
    g.result(0xFFFFFF, 11)
    call(g, "GetPixel", (dc,), 10, 7)
    g.result(0, 12)
    call(g, "GetCaretPos", point)
    g.result(1, 13)
    g.word(point, 10, 14)
    g.word(point + 4, 7, 15)
    call(g, "BeginPaint", (window,), paint)
    call(g, "GetPixel", (dc,), 10, 7)
    g.result(0xFFFFFF, 16)
    call(g, "EndPaint", (window,), paint)
    g.result(1, 17)
    call(g, "GetPixel", (dc,), 10, 7)
    g.result(0, 18)
    call(g, "DestroyCaret")
    g.result(1, 19)
    call(g, "GetPixel", (dc,), 10, 7)
    g.result(0xFFFFFF, 20)
    call(g, "SetCaretBlinkTime", 1)
    call(g, "CreateCaret", (window,), 0, 2, 8)
    call(g, "SetCaretPos", 10, 7)
    call(g, "ShowCaret", (window,))
    call(g, "Sleep", 20)
    g.call(3, 918, 1, message, 0, 0x24, 0x24, 0)
    g.result(0, 21)  # Private caret timers never produce an application WM_TIMER.
    call(g, "GetPixel", (dc,), 10, 7)
    g.result(0xFFFFFF, 22)
    call(g, "DestroyCaret")
    g.result(1, 23)
    call(g, "ReleaseDC", (window,), (dc,))
    g.result(1, 24)
    call(g, "DeleteObject", (brush,))
    call(g, "DestroyWindow", (window,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "CARET32", data=g.data)


def stroke_program():
    g = FlatGuest()
    screen, dc, bitmap, pen, previous, brush = (g.field(bytes(4)) for _ in range(6))
    point, info = g.field(bytes(8)), g.field(bytes(16))
    rectangle = g.field(struct.pack("<4i", 0, 0, 16, 16))
    call(g, "GetDC", 0)
    store(g, screen)
    call(g, "CreateCompatibleDC", (screen,))
    store(g, dc)
    call(g, "CreateCompatibleBitmap", (screen,), 16, 16)
    store(g, bitmap)
    call(g, "SelectObject", (dc,), (bitmap,))
    call(g, "CreateSolidBrush", 0xFFFFFF)
    store(g, brush)
    call(g, "FillRect", (dc,), rectangle, (brush,))
    call(g, "CreatePen", 0, 1, 0xFF)
    store(g, pen)
    call(g, "GetObject", (pen,), 16, info)
    g.result(16, 1)
    g.word(info + 4, 1, 2)
    g.word(info + 12, 0xFF, 3)
    call(g, "SelectObject", (dc,), (pen,))
    store(g, previous)
    call(g, "DeleteObject", (pen,))
    g.result(0, 4)
    call(g, "MoveToEx", (dc,), 1, 2, point)
    g.result(1, 5)
    g.word(point, 0, 6)
    g.word(point + 4, 0, 7)
    call(g, "LineTo", (dc,), 8, 2)
    g.result(1, 8)
    for x, y, color in ((1, 2, 0xFF), (7, 2, 0xFF), (8, 2, 0xFFFFFF), (1, 1, 0xFFFFFF)):
        call(g, "GetPixel", (dc,), x, y)
        g.result(color, 9)
    call(g, "MoveToEx", (dc,), 1, 2, point)
    g.word(point, 8, 10)
    g.word(point + 4, 2, 11)
    call(g, "SetROP2", (dc,), 7)
    g.result(13, 12)
    call(g, "LineTo", (dc,), 8, 2)
    call(g, "GetPixel", (dc,), 4, 2)
    g.result(0, 13)
    call(g, "MoveToEx", (dc,), 1, 2, 0)
    call(g, "LineTo", (dc,), 8, 2)
    call(g, "GetPixel", (dc,), 4, 2)
    g.result(0xFF, 14)
    call(g, "SetROP2", (dc,), 13)
    call(g, "IntersectClipRect", (dc,), 3, 1, 6, 4)
    call(g, "MoveToEx", (dc,), 1, 3, 0)
    call(g, "LineTo", (dc,), 8, 3)
    call(g, "SelectClipRgn", (dc,), 0)
    for x, color in ((2, 0xFFFFFF), (3, 0xFF), (5, 0xFF), (6, 0xFFFFFF)):
        call(g, "GetPixel", (dc,), x, 3)
        g.result(color, 15)
    call(g, "SelectObject", (dc,), (previous,))
    call(g, "DeleteObject", (pen,))
    g.result(1, 16)
    call(g, "CreatePen", 0, 3, 0xFF0000)
    store(g, pen)
    call(g, "SelectObject", (dc,), (pen,))
    call(g, "MoveToEx", (dc,), 4, 8, 0)
    call(g, "LineTo", (dc,), 10, 8)
    for x, y, color in ((5, 7, 0xFF0000), (5, 9, 0xFF0000), (5, 10, 0xFFFFFF)):
        call(g, "GetPixel", (dc,), x, y)
        g.result(color, 17)
    call(g, "SetROP2", (dc,), 7)
    call(g, "MoveToEx", (dc,), 4, 8, 0)
    call(g, "LineTo", (dc,), 10, 8)
    call(g, "GetPixel", (dc,), 5, 8)
    g.result(0, 18)
    call(g, "GetStockObject", 8)
    store(g, previous)
    call(g, "SelectObject", (dc,), (previous,))
    call(g, "DeleteObject", (pen,))
    g.result(1, 19)
    call(g, "MoveToEx", (dc,), 0, 0, 0)
    call(g, "LineTo", (dc,), 10, 10)
    call(g, "GetPixel", (dc,), 0, 0)
    g.result(0xFFFFFF, 20)
    call(g, "MoveToEx", (dc,), 0, 0, point)
    g.word(point, 10, 21)
    g.word(point + 4, 10, 22)
    call(g, "SetROP2", (dc,), 13)
    call(g, "CreatePen", 0, 1, 0xFF)
    store(g, pen)
    call(g, "SelectObject", (dc,), (pen,))
    call(g, "SelectObject", (dc,), (brush,))
    call(g, "Rectangle", (dc,), 2, 5, 8, 10)
    g.result(1, 25)
    for x, y, color in ((2, 5, 0xFF), (7, 9, 0xFF), (3, 6, 0xFFFFFF), (8, 5, 0xFFFFFF)):
        call(g, "GetPixel", (dc,), x, y)
        g.result(color, 26)
    call(g, "SetROP2", (dc,), 7)
    call(g, "GetStockObject", 5)
    store(g, previous)
    call(g, "SelectObject", (dc,), (previous,))
    call(g, "Rectangle", (dc,), 2, 5, 8, 10)
    call(g, "GetPixel", (dc,), 2, 5)
    g.result(0, 27)
    call(g, "GetPixel", (dc,), 3, 6)
    g.result(0xFFFFFF, 28)
    call(g, "GetROP2", (dc,))
    g.result(7, 29)
    framed = g.field(struct.pack("<4i", 1, 11, 7, 15))
    framed_brush = g.field(bytes(4))
    call(g, "CreateSolidBrush", 0xFF)
    store(g, framed_brush)
    call(g, "FrameRect", (dc,), framed, (framed_brush,))
    g.result(1, 31)
    for x, y, color in ((1, 11, 0xFF), (6, 14, 0xFF), (2, 12, 0xFFFFFF), (7, 14, 0xFFFFFF)):
        call(g, "GetPixel", (dc,), x, y)
        g.result(color, 32)
    call(g, "InvertRect", (dc,), framed)
    g.result(1, 33)
    call(g, "GetPixel", (dc,), 2, 12)
    g.result(0, 34)
    call(g, "InvertRect", (dc,), framed)
    call(g, "GetPixel", (dc,), 2, 12)
    g.result(0xFFFFFF, 35)
    call(g, "DeleteObject", (framed_brush,))
    call(g, "DeleteDC", (dc,))
    g.result(1, 23)
    call(g, "DeleteObject", (pen,))
    g.result(1, 30)
    call(g, "DeleteObject", (bitmap,))
    call(g, "DeleteObject", (brush,))
    call(g, "ReleaseDC", 0, (screen,))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "STROKE32", data=g.data)


def message_program():
    g = FlatGuest()
    name = g.field(b"MessageContract\0")
    (
        window,
        previous_pm,
        native_paints,
        native_keys,
        windows_paints,
        windows_keys,
        windows_chars,
        mouse_count,
        hook_count,
        hook_code,
        hook_handle,
    ) = (g.field(bytes(4)) for _ in range(11))
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    message, paint, pid, menu = (
        g.field(bytes(28)),
        g.field(bytes(64)),
        g.field(bytes(4)),
        g.field(bytes(4)),
    )
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, 0, 0x10080000, 10, 10, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    call(g, "GetWindowThreadProcessId", (window,), pid)
    g.result(1, 1)
    call(g, "GetCurrentProcessId")
    g.require(b"\x3b\x05" + struct.pack("<I", pid), 2)
    call(g, "GetSystemMenu", (window,), 0)
    store(g, menu)
    call(g, "GetMenuItemCount", (menu,))
    g.result(7, 3)
    call(g, "DeleteMenu", (menu,), 0xF060, 0)
    g.result(1, 4)
    call(g, "GetSystemMenu", (window,), 1)
    g.result(0, 5)
    call(g, "GetSystemMenu", (window,), 0)
    store(g, menu)
    call(g, "GetMenuItemCount", (menu,))
    g.result(7, 6)
    g.call(3, 929, (window,), 0x77665544)
    store(g, previous_pm)
    call(g, "InvalidateRect", (window,), 0, 0)
    call(g, "PeekMessage", message, (window,), 15, 15, 1)
    g.result(1, 7)
    call(g, "DispatchMessage", message)
    g.word(native_paints, 1, 8)
    g.word(windows_paints, 1, 9)
    call(g, "PeekMessage", message, (window,), 15, 15, 0)
    g.result(0, 10)
    call(g, "SetWindowsHookEx", 2, 0x66554433, 0, 1)
    store(g, hook_handle)
    g.call(3, 919, (window,), 0x7A, 0x1E010005, ord("a"))
    for expected_count in (1, 2):
        call(g, "PeekMessage", message, (window,), 0x100, 0x100, 0)
        g.result(1, 11)
        g.word(hook_code, 3, 12)
        g.word(hook_count, expected_count, 13)
        g.word(windows_keys, 0, 14)
    call(g, "PeekMessage", message, (window,), 0x100, 0x100, 1)
    g.result(1, 15)
    g.word(hook_code, 0, 16)
    g.word(hook_count, 3, 17)
    g.word(message + 8, ord("A"), 18)
    call(g, "GetKeyState", ord("A"))
    g.code.extend(b"\x25\xff\xff\0\0")
    g.result(0x8000, 19)
    call(g, "GetAsyncKeyState", ord("A"))
    g.result(0, 51)  # A posted key packet changes queued state without a physical key press.
    call(g, "TranslateMessage", message)
    g.result(1, 20)
    call(g, "DispatchMessage", message)
    g.word(windows_keys, 1, 21)
    g.word(native_keys, 1, 22)
    call(g, "GetMessage", message, 0, 0, 0)
    g.result(1, 23)
    g.word(message + 4, 0x102, 24)
    g.word(message + 8, ord("a"), 25)
    call(g, "DispatchMessage", message)
    g.word(windows_chars, 1, 26)
    g.word(native_keys, 1, 27)
    g.call(3, 919, (window,), 0x7A, 0x1E010044, 0)
    call(g, "GetMessage", message, 0, 0, 0)
    g.result(1, 28)
    g.word(message + 4, 0x101, 29)
    call(g, "DispatchMessage", message)
    call(g, "GetKeyState", ord("A"))
    g.result(0, 30)
    g.word(native_keys, 2, 31)
    call(g, "UnhookWindowsHookEx", (hook_handle,))
    g.call(3, 919, (window,), 0x71, (45 << 16) | 3, 0)
    call(g, "GetMessage", message, 0, 0, 0)
    g.result(1, 32)
    g.word(message + 4, 0x201, 33)
    g.word(message + 12, (4 << 16) | 3, 34)
    call(g, "DispatchMessage", message)
    g.word(mouse_count, 1, 35)
    call(g, "PostMessage", (window,), 0x403, 0x1234, 0x5678)
    g.result(1, 36)
    call(g, "PeekMessage", message, (window,), 0x403, 0x403, 0)
    g.result(1, 37)
    call(g, "GetMessageTime")
    g.require(b"\x3b\x05" + struct.pack("<I", message + 16), 38)
    call(g, "PeekMessage", message, (window,), 0x403, 0x403, 1)
    g.result(1, 39)
    g.word(message + 8, 0x1234, 40)
    g.word(message + 12, 0x5678, 41)
    call(g, "DispatchMessage", message)
    g.call(3, 884, 1, (window,), 202, 2)
    g.result(202, 42)
    call(g, "GetMessage", message, 0, 0x113, 0x113)
    g.result(1, 43)
    g.word(message + 4, 0x113, 44)
    g.word(message + 8, 202, 45)
    g.call(3, 885, 1, (window,), 202)
    g.result(1, 46)
    call(g, "DispatchMessage", message)
    call(g, "PostQuitMessage", 17)
    call(g, "GetMessage", message, (window,), 0xFFFF, 0xFFFF)
    g.result(0, 47)
    g.word(message + 4, 18, 48)
    g.word(message + 8, 17, 49)
    call(g, "DestroyWindow", (window,))
    g.result(1, 50)
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    window_procedure = 0x10000 + len(g.code)
    g.code.extend(b"\x83\x7c\x24\x08\x0f\x75")
    branch = len(g.code)
    g.code.extend(b"\0")
    g.code.extend(b"\xff\x05" + struct.pack("<I", windows_paints))
    call(g, "BeginPaint", (window,), paint)
    call(g, "EndPaint", (window,), paint)
    g.code.extend(b"\x31\xc0\xc3")
    g.code[branch] = len(g.code) - branch - 1
    for code, destination in ((0x100, windows_keys), (0x102, windows_chars), (0x201, mouse_count)):
        g.code.extend(
            b"\x81\x7c\x24\x08"
            + struct.pack("<I", code)
            + b"\x75\x06\xff\x05"
            + struct.pack("<I", destination)
        )
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    pm_procedure = 0x10000 + len(g.code)
    for code, destination in ((0x23, native_paints), (0x7A, native_keys)):
        g.code.extend(
            b"\x81\x7c\x24\x08"
            + struct.pack("<I", code)
            + b"\x75\x06\xff\x05"
            + struct.pack("<I", destination)
        )
    g.code.extend(b"\xff\x25" + struct.pack("<I", previous_pm))
    hook_procedure = 0x10000 + len(g.code)
    g.code.extend(b"\xff\x05" + struct.pack("<I", hook_count))
    g.code.extend(b"\x8b\x44\x24\x04\xa3" + struct.pack("<I", hook_code))
    g.code.extend(b"\x31\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", window_procedure))
    for sentinel, address in ((0x77665544, pm_procedure), (0x66554433, hook_procedure)):
        g.code = g.code.replace(struct.pack("<I", sentinel), struct.pack("<I", address))
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "MESSAGES32", data=g.data
    )


def rectangle_program():
    g = FlatGuest()
    a = g.field(struct.pack("<4i", 0, 0, 10, 10))
    b = g.field(struct.pack("<4i", 5, -3, 15, 6))
    output = g.field(bytes(16))
    call(g, "UnionRect", output, a, b)
    g.result(1, 1)
    for index, value in enumerate((0, 0xFFFFFFFD, 15, 10)):
        g.word(output + index * 4, value, 2)
    call(g, "IntersectRect", output, a, b)
    g.result(1, 3)
    for index, value in enumerate((5, 0, 10, 6)):
        g.word(output + index * 4, value, 4)
    call(g, "CopyRect", output, a)
    g.result(1, 5)
    call(g, "EqualRect", output, a)
    g.result(1, 6)
    call(g, "SetRect", b, 2, 2, 8, 8)
    call(g, "SubtractRect", output, a, b)
    g.result(1, 7)
    call(g, "EqualRect", output, a)
    g.result(1, 8)
    call(g, "SetRect", b, 0, 0, 4, 10)
    call(g, "SubtractRect", output, a, b)
    g.result(1, 9)
    g.word(output, 4, 10)
    g.word(output + 8, 10, 11)
    call(g, "SubtractRect", output, a, a)
    g.result(0, 12)
    for index in range(4):
        g.word(output + index * 4, 0, 13)
    for x, y, result in ((0, 0, 1), (9, 9, 1), (10, 9, 0), (9, 10, 0)):
        call(g, "PtInRect", a, x, y)
        g.result(result, 14)
    call(g, "OffsetRect", a, 3, 0xFFFFFFFE)
    call(g, "InflateRect", a, 2, 1)
    for index, value in enumerate((1, 0xFFFFFFFD, 15, 9)):
        g.word(a + index * 4, value, 15)
    call(g, "UnionRect", a, a, b)
    g.result(1, 16)
    g.word(a, 0, 17)
    call(g, "SetRectEmpty", a)
    call(g, "IsRectEmpty", a)
    g.result(1, 18)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "RECTANGLES32", data=g.data)


def bitmap_program():
    g = FlatGuest()
    dc, memory, bitmap, previous = (g.field(bytes(4)) for _ in range(4))
    mono = g.field(bytes.fromhex("a0004000"))
    info = g.field(struct.pack("<IiiHH6I", 40, 3, 2, 1, 1, 0, 0, 0, 0, 2, 0) + struct.pack("<2I", 0, 0xffffff))
    replacement = g.field(bytes.fromhex("40000000a0000000"))
    output = g.field(bytes(24))
    call(g, "GetDC", 0)
    store(g, dc)
    call(g, "CreateCompatibleDC", (dc,))
    store(g, memory)
    call(g, "CreateBitmap", 3, 2, 1, 1, mono)
    store(g, bitmap)
    call(g, "GetObject", (bitmap,), 24, output)
    g.result(24, 1)
    g.word(output + 12, 2, 2)
    call(g, "SelectObject", (memory,), (bitmap,))
    store(g, previous)
    for y, values in enumerate(((0, 0xffffff, 0), (0xffffff, 0, 0xffffff))):
        for x, value in enumerate(values):
            call(g, "GetPixel", (memory,), x, y)
            g.result(value, 3)
    call(g, "SetDIBits", (dc,), (bitmap,), 0, 2, replacement, info, 0)
    g.result(0, 4)
    call(g, "SelectObject", (memory,), (previous,))
    call(g, "SetDIBits", (dc,), (bitmap,), 0, 2, replacement, info, 0)
    g.result(2, 5)
    call(g, "SelectObject", (memory,), (bitmap,))
    for y, values in enumerate(((0xffffff, 0, 0xffffff), (0, 0xffffff, 0))):
        for x, value in enumerate(values):
            call(g, "GetPixel", (memory,), x, y)
            g.result(value, 6)
    call(g, "SelectObject", (memory,), (previous,))
    call(g, "DeleteObject", (bitmap,))
    call(g, "CreateBitmap", 10, 19, 1, 4, 0)
    store(g, bitmap)
    call(g, "GetObject", (bitmap,), 24, output)
    g.result(24, 7)
    g.word(output + 12, 6, 8)
    g.word(output + 16, 0x40001, 9)
    call(g, "DeleteObject", (bitmap,))
    call(g, "DeleteDC", (memory,))
    call(g, "ReleaseDC", 0, (dc,))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "BITMAPS32", data=g.data)
